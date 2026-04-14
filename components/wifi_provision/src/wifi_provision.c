/**
 * @file wifi_provision.c
 * @brief 配网协调器，统一管理 AP 门户、BLE 配网和 STA 启动路径。
 */

#include "wifi_provision.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cJSON.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include "ble_server/ble_provision_protocol.h"
#include "ble_server/ble_provision_transport.h"
#include "wifi_manager.h"
#include "ws_server.h"

#define TAG "wifi_prov"
#define BLE_STATUS_JSON_LEN 256
#define BLE_NOTIFY_FLUSH_DELAY_MS 600
#define BLE_STOP_TASK_STACK_SIZE 4096
#define WIFI_PROVISION_AP_URL "http://192.168.100.1/"
#define WIFI_SCAN_MAX_VISIBLE_ITEMS 12
#define WIFI_SCAN_BATCH_SIZE 1

extern const uint8_t apcfg_html_start[] asm("_binary_apcfg_html_start");
extern const uint8_t apcfg_html_end[] asm("_binary_apcfg_html_end");

static EventGroupHandle_t prov_ev_group = NULL;

#define PROV_WIFI_CONNECTED_BIT BIT0
#define PROV_WIFI_FAIL_BIT BIT1
#define PROV_WIFI_SUCCESS_BIT BIT2

typedef enum
{
    WIFI_PROVISION_TRANSPORT_NONE = 0,
    WIFI_PROVISION_TRANSPORT_AP,
    WIFI_PROVISION_TRANSPORT_BLE,
} wifi_provision_transport_t;

static char current_ssid[33] = {0};
static char current_password[65] = {0};
static bool is_configuring = false;              // 当前是否在执行一次配网连接流程
static wifi_provision_cb_t user_callback = NULL; // 上层状态回调
static char ble_service_name[20] = "ESP32S3";    // BLE 广播设备名
static wifi_provision_transport_t current_transport =
    WIFI_PROVISION_TRANSPORT_NONE;
static bool s_provision_task_started = false;      // 配网任务是否已创建
static TaskHandle_t s_ble_stop_task_handle = NULL; // BLE 延时停止任务句柄

void ws_receive_handle(const char *data, int len);
static void wifi_scan_handle(wifi_ap_record_t *ap, int ap_count,
                             esp_err_t scan_result);

static int wifi_provision_compare_scan_items_desc(const void *left,
                                                  const void *right);
static size_t wifi_provision_collect_scan_items(
    const wifi_ap_record_t *ap, int ap_count, ble_prov_wifi_scan_item_t *items,
    size_t max_items);
static void wifi_provision_send_web_scan_results(
    const ble_prov_wifi_scan_item_t *items, size_t item_count);
static void wifi_provision_send_ble_wifi_scan_started(void);
static esp_err_t wifi_provision_send_ble_wifi_scan_batch(
    const ble_prov_wifi_scan_item_t *items, size_t item_count, bool more);
static void wifi_provision_send_ble_wifi_scan_done(size_t total);
static void wifi_provision_send_ble_wifi_scan_failed(const char *reason);
static void wifi_provision_ble_wifi_scan_handle(wifi_ap_record_t *ap,
                                                int ap_count,
                                                esp_err_t scan_result);

static void wifi_provision_cancel_scheduled_ble_stop(void)
{
    if (s_ble_stop_task_handle == NULL)
    {
        return;
    }

    vTaskDelete(s_ble_stop_task_handle);
    s_ble_stop_task_handle = NULL;
}

static void wifi_provision_delayed_ble_stop_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(BLE_NOTIFY_FLUSH_DELAY_MS));
    if (current_transport == WIFI_PROVISION_TRANSPORT_NONE && !is_configuring &&
        ble_provision_transport_is_active())
    {
        ESP_LOGI(TAG, "BLE 终态通知缓冲完成，关闭 BLE 配网");
        ble_provision_transport_stop();
    }

    s_ble_stop_task_handle = NULL;
    vTaskDelete(NULL);
}

static void wifi_provision_schedule_ble_stop(void)
{
    BaseType_t task_ret = pdPASS;

    if (s_ble_stop_task_handle != NULL)
    {
        return;
    }

    task_ret = xTaskCreatePinnedToCore(wifi_provision_delayed_ble_stop_task,
                                       "ble_stop_delay",
                                       BLE_STOP_TASK_STACK_SIZE, NULL, 2,
                                       &s_ble_stop_task_handle, 1);
    if (task_ret != pdPASS)
    {
        s_ble_stop_task_handle = NULL;
        ESP_LOGW(TAG, "创建 BLE 延迟停止任务失败，立即关闭 BLE");
        ble_provision_transport_stop();
    }
}

static void send_status_to_web(const char *status, const char *ssid,
                               const char *ip)
{
    // AP 门户状态上报统一走 JSON，便于前端状态机处理。
    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
    {
        return;
    }

    cJSON_AddStringToObject(root, "status", status);
    cJSON_AddStringToObject(root, "ssid", ssid != NULL ? ssid : "");
    if (ip != NULL)
    {
        cJSON_AddStringToObject(root, "ip", ip);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str != NULL)
    {
        ws_server_send((uint8_t *)json_str, strlen(json_str));
        cJSON_free(json_str);
    }
    cJSON_Delete(root);
}

static void wifi_provision_prepare_ble_service_name(void)
{
    uint8_t mac[6] = {0};

    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK)
    {
        snprintf(ble_service_name, sizeof(ble_service_name), "ESP32S3-%02X%02X",
                 mac[4], mac[5]);
    }
}

static const char *wifi_provision_get_ble_state_string(void)
{
    if (wifi_manager_is_connected())
    {
        return "connected";
    }
    if (current_transport == WIFI_PROVISION_TRANSPORT_AP)
    {
        return "ap_fallback";
    }
    if (current_transport == WIFI_PROVISION_TRANSPORT_BLE && is_configuring)
    {
        return "connecting";
    }
    if (ble_provision_transport_is_connected())
    {
        return "ble_connected";
    }
    if (ble_provision_transport_is_active())
    {
        return "idle";
    }
    return "idle";
}

static void wifi_provision_send_ble_payload(const char *payload)
{
    if (!ble_provision_transport_is_active())
    {
        return;
    }
    ble_provision_transport_notify_json(payload);
}

static void wifi_provision_send_ble_hello(void)
{
    char payload[BLE_STATUS_JSON_LEN] = {0};

    if (ble_provision_protocol_format_hello(payload, sizeof(payload),
                                            ble_service_name) == ESP_OK)
    {
        wifi_provision_send_ble_payload(payload);
    }
}

static void wifi_provision_send_ble_status(const char *state, const char *ssid,
                                           const char *ip, const char *reason,
                                           const char *url)
{
    char payload[BLE_STATUS_JSON_LEN] = {0};

    if (ble_provision_protocol_format_status(payload, sizeof(payload), state,
                                             ssid, ip, reason, url) == ESP_OK)
    {
        wifi_provision_send_ble_payload(payload);
    }
}

static int wifi_provision_compare_scan_items_desc(const void *left,
                                                  const void *right)
{
    const ble_prov_wifi_scan_item_t *left_item =
        (const ble_prov_wifi_scan_item_t *)left;
    const ble_prov_wifi_scan_item_t *right_item =
        (const ble_prov_wifi_scan_item_t *)right;

    if (left_item->rssi == right_item->rssi)
    {
        return strcmp(left_item->ssid, right_item->ssid);
    }

    return right_item->rssi - left_item->rssi;
}

static size_t wifi_provision_collect_scan_items(
    const wifi_ap_record_t *ap, int ap_count, ble_prov_wifi_scan_item_t *items,
    size_t max_items)
{
    size_t item_count = 0;

    if (ap == NULL || ap_count <= 0 || items == NULL || max_items == 0)
    {
        return 0;
    }

    memset(items, 0, sizeof(*items) * max_items);

    for (int index = 0; index < ap_count; ++index)
    {
        const char *ssid = (const char *)ap[index].ssid;
        bool encrypted = ap[index].authmode != WIFI_AUTH_OPEN;
        size_t existing_index = max_items;
        size_t weakest_index = 0;

        if (ssid[0] == '\0')
        {
            continue;
        }

        // 先按 SSID 去重，随后保留同 SSID 中 RSSI 更强的一条。
        for (size_t item_index = 0; item_index < item_count; ++item_index)
        {
            if (strcmp(items[item_index].ssid, ssid) == 0)
            {
                existing_index = item_index;
                break;
            }
        }

        if (existing_index < item_count)
        {
            bool combined_encrypted =
                items[existing_index].encrypted || encrypted;
            if (ap[index].rssi > items[existing_index].rssi)
            {
                items[existing_index].rssi = ap[index].rssi;
            }
            items[existing_index].encrypted = combined_encrypted;
            continue;
        }

        if (item_count < max_items)
        {
            snprintf(items[item_count].ssid, sizeof(items[item_count].ssid),
                     "%s", ssid);
            items[item_count].rssi = ap[index].rssi;
            items[item_count].encrypted = encrypted;
            ++item_count;
            continue;
        }

        for (size_t item_index = 1; item_index < item_count; ++item_index)
        {
            if (items[item_index].rssi < items[weakest_index].rssi)
            {
                weakest_index = item_index;
            }
        }

        if (ap[index].rssi <= items[weakest_index].rssi)
        {
            continue;
        }

        snprintf(items[weakest_index].ssid, sizeof(items[weakest_index].ssid),
                 "%s", ssid);
        items[weakest_index].rssi = ap[index].rssi;
        items[weakest_index].encrypted = encrypted;
    }

    if (item_count > 1)
    {
        qsort(items, item_count, sizeof(items[0]),
              wifi_provision_compare_scan_items_desc);
    }

    return item_count;
}

static void wifi_provision_send_web_scan_results(
    const ble_prov_wifi_scan_item_t *items, size_t item_count)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
    {
        return;
    }

    cJSON *wifi_list = cJSON_AddArrayToObject(root, "wifi_list");
    if (wifi_list == NULL)
    {
        cJSON_Delete(root);
        return;
    }

    for (size_t index = 0; index < item_count; ++index)
    {
        cJSON *ap_item = cJSON_CreateObject();
        if (ap_item == NULL)
        {
            continue;
        }

        cJSON_AddStringToObject(ap_item, "ssid", items[index].ssid);
        cJSON_AddNumberToObject(ap_item, "rssi", items[index].rssi);
        cJSON_AddBoolToObject(ap_item, "encrypted", items[index].encrypted);
        cJSON_AddItemToArray(wifi_list, ap_item);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str != NULL)
    {
        ws_server_send((uint8_t *)json_str, strlen(json_str));
        cJSON_free(json_str);
    }
    cJSON_Delete(root);
}

static void wifi_provision_send_ble_wifi_scan_started(void)
{
    char payload[BLE_STATUS_JSON_LEN] = {0};

    if (ble_provision_protocol_format_wifi_scan_started(payload,
                                                        sizeof(payload)) ==
        ESP_OK)
    {
        wifi_provision_send_ble_payload(payload);
    }
}

static esp_err_t wifi_provision_send_ble_wifi_scan_batch(
    const ble_prov_wifi_scan_item_t *items, size_t item_count, bool more)
{
    char payload[BLE_STATUS_JSON_LEN] = {0};
    esp_err_t ret = ble_provision_protocol_format_wifi_scan_batch(
        payload, sizeof(payload), items, item_count, more);

    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "格式化 BLE Wi-Fi 扫描 batch 失败: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    wifi_provision_send_ble_payload(payload);
    return ESP_OK;
}

static void wifi_provision_send_ble_wifi_scan_done(size_t total)
{
    char payload[BLE_STATUS_JSON_LEN] = {0};

    if (ble_provision_protocol_format_wifi_scan_done(payload, sizeof(payload),
                                                     total) == ESP_OK)
    {
        wifi_provision_send_ble_payload(payload);
    }
}

static void wifi_provision_send_ble_wifi_scan_failed(const char *reason)
{
    char payload[BLE_STATUS_JSON_LEN] = {0};

    if (ble_provision_protocol_format_wifi_scan_failed(payload,
                                                       sizeof(payload),
                                                       reason) == ESP_OK)
    {
        wifi_provision_send_ble_payload(payload);
    }
}

static void wifi_provision_ble_wifi_scan_handle(wifi_ap_record_t *ap,
                                                int ap_count,
                                                esp_err_t scan_result)
{
    ble_prov_wifi_scan_item_t items[WIFI_SCAN_MAX_VISIBLE_ITEMS] = {0};
    size_t item_count = 0;

    if (scan_result != ESP_OK)
    {
        ESP_LOGW(TAG, "BLE Wi-Fi 扫描失败: %s", esp_err_to_name(scan_result));
        wifi_provision_send_ble_wifi_scan_failed("scan_failed");
        return;
    }

    item_count = wifi_provision_collect_scan_items(ap, ap_count, items,
                                                   WIFI_SCAN_MAX_VISIBLE_ITEMS);

    ESP_LOGI(TAG, "BLE Wi-Fi 扫描完成: raw=%d visible=%u", ap_count,
             (unsigned)item_count);

    for (size_t offset = 0; offset < item_count; offset += WIFI_SCAN_BATCH_SIZE)
    {
        size_t batch_count = item_count - offset;
        bool more = false;

        if (batch_count > WIFI_SCAN_BATCH_SIZE)
        {
            batch_count = WIFI_SCAN_BATCH_SIZE;
        }
        more = (offset + batch_count) < item_count;
        if (wifi_provision_send_ble_wifi_scan_batch(items + offset, batch_count,
                                                    more) != ESP_OK)
        {
            wifi_provision_send_ble_wifi_scan_failed("scan_failed");
            return;
        }
    }

    wifi_provision_send_ble_wifi_scan_done(item_count);
}

static esp_err_t wifi_provision_switch_to_ap_fallback(void)
{
    esp_err_t ret = ESP_OK;

    wifi_provision_cancel_scheduled_ble_stop();
    if (ble_provision_transport_is_active())
    {
        ble_provision_transport_stop();
    }
    current_transport = WIFI_PROVISION_TRANSPORT_AP;
    is_configuring = true;
    ESP_LOGI(TAG, "切换到 AP 兜底配网");
    ret = wifi_manager_ap();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "启动 AP 模式失败: %s", esp_err_to_name(ret));
        current_transport = WIFI_PROVISION_TRANSPORT_NONE;
        is_configuring = false;
        return ret;
    }

    ws_server_config_t config = {
        .html_code = (const char *)apcfg_html_start,
        .cb = ws_receive_handle,
    };
    ret = ws_server_start(&config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "启动 AP 门户失败: %s", esp_err_to_name(ret));
        ws_server_stop();
        wifi_manager_stop_ap();
        current_transport = WIFI_PROVISION_TRANSPORT_NONE;
        is_configuring = false;
        return ret;
    }

    return ESP_OK;
}

static void wifi_provision_ble_receive_cb(const char *data, size_t len,
                                          void *user_data)
{
    ble_prov_request_t request = {0};
    esp_err_t ret = ESP_OK;

    (void)len;
    (void)user_data;

    ret = ble_provision_protocol_parse_request(data, &request);
    if (ret != ESP_OK)
    {
        wifi_provision_send_ble_status("failed", NULL, NULL, "invalid_request",
                                       NULL);
        return;
    }

    // BLE 协议命令分发：hello/status/scan_wifi/set_wifi/start_ap_fallback。
    switch (request.cmd)
    {
    case BLE_PROV_CMD_HELLO:
        wifi_provision_send_ble_hello();
        break;
    case BLE_PROV_CMD_STATUS:
        wifi_provision_send_ble_status(wifi_provision_get_ble_state_string(),
                                       current_ssid, NULL, NULL, NULL);
        break;
    case BLE_PROV_CMD_SCAN_WIFI:
        ESP_LOGI(TAG, "收到 BLE Wi-Fi 扫描请求");
        ret = wifi_manager_scan(wifi_provision_ble_wifi_scan_handle);
        if (ret == ESP_OK)
        {
            wifi_provision_send_ble_wifi_scan_started();
        }
        else if (ret == ESP_ERR_INVALID_STATE)
        {
            ESP_LOGW(TAG, "BLE Wi-Fi 扫描请求被拒绝: scan busy");
            wifi_provision_send_ble_wifi_scan_failed("scan_busy");
        }
        else
        {
            ESP_LOGE(TAG, "启动 BLE Wi-Fi 扫描失败: %s",
                     esp_err_to_name(ret));
            wifi_provision_send_ble_wifi_scan_failed("scan_start_failed");
        }
        break;
    case BLE_PROV_CMD_SET_WIFI:
        snprintf(current_ssid, sizeof(current_ssid), "%s", request.ssid);
        snprintf(current_password, sizeof(current_password), "%s",
                 request.password);
        current_transport = WIFI_PROVISION_TRANSPORT_BLE;
        is_configuring = true;

        ret = wifi_manager_set_credentials(current_ssid, current_password);
        if (ret != ESP_OK)
        {
            is_configuring = false;
            wifi_provision_send_ble_status("failed", current_ssid, NULL,
                                           "save_failed", NULL);
            return;
        }

        wifi_provision_send_ble_status("connecting", current_ssid, NULL,
                                       NULL, NULL);
        ret = wifi_manager_connect(current_ssid, current_password);
        if (ret != ESP_OK)
        {
            is_configuring = false;
            wifi_provision_send_ble_status("failed", current_ssid, NULL,
                                           "connect_start_failed", NULL);
        }
        break;
    case BLE_PROV_CMD_START_AP_FALLBACK:
        wifi_provision_send_ble_status("ap_fallback", NULL, NULL, NULL,
                                       WIFI_PROVISION_AP_URL);
        ret = wifi_provision_switch_to_ap_fallback();
        if (ret != ESP_OK)
        {
            wifi_provision_send_ble_status("failed", NULL, NULL,
                                           "ap_fallback_start_failed",
                                           NULL);
        }
        break;
    case BLE_PROV_CMD_INVALID:
    default:
        wifi_provision_send_ble_status("failed", NULL, NULL,
                                       "unsupported_cmd", NULL);
        break;
    }
}

static void wifi_provision_ble_state_cb(bool connected, void *user_data)
{
    (void)connected;
    (void)user_data;
}

static void wifi_provision_task(void *arg)
{
    (void)arg;

    const EventBits_t all_bits =
        PROV_WIFI_CONNECTED_BIT | PROV_WIFI_FAIL_BIT | PROV_WIFI_SUCCESS_BIT;
    while (1)
    {
        EventBits_t bits = xEventGroupWaitBits(prov_ev_group, all_bits, pdTRUE,
                                               pdFALSE, portMAX_DELAY);

        if ((bits & PROV_WIFI_CONNECTED_BIT) != 0)
        {
            ESP_LOGI(TAG, "开始连接 Wi-Fi: %s", current_ssid);
            wifi_manager_connect(current_ssid, current_password);
        }

        if ((bits & PROV_WIFI_FAIL_BIT) != 0)
        {
            ESP_LOGW(TAG, "Wi-Fi 配网连接失败");
            if (current_transport == WIFI_PROVISION_TRANSPORT_AP)
            {
                send_status_to_web("failed", current_ssid, NULL);
            }
            is_configuring = false;
        }

        if ((bits & PROV_WIFI_SUCCESS_BIT) != 0)
        {
            char ip_str[16] = {0};
            wifi_manager_get_ip(ip_str, sizeof(ip_str));
            ESP_LOGI(TAG, "Wi-Fi 配网连接成功, IP: %s", ip_str);
            if (current_transport == WIFI_PROVISION_TRANSPORT_AP)
            {
                send_status_to_web("connected", current_ssid, ip_str);
                vTaskDelay(pdMS_TO_TICKS(2000));
                ws_server_stop();
                wifi_manager_stop_ap();
                current_transport = WIFI_PROVISION_TRANSPORT_NONE;
            }
            is_configuring = false;
        }
    }
}

static void internal_wifi_cb(WIFI_STATE state)
{
    switch (state)
    {
    case WIFI_STATE_CONNECTED:
        if (current_transport == WIFI_PROVISION_TRANSPORT_BLE)
        {
            char ip_str[16] = {0};
            wifi_manager_get_ip(ip_str, sizeof(ip_str));
            wifi_provision_send_ble_status("connected", current_ssid, ip_str,
                                           NULL, NULL);
            is_configuring = false;
            current_transport = WIFI_PROVISION_TRANSPORT_NONE;
            wifi_provision_schedule_ble_stop();
        }
        else if (is_configuring)
        {
            xEventGroupSetBits(prov_ev_group, PROV_WIFI_SUCCESS_BIT);
        }
        if (user_callback != NULL)
        {
            user_callback(WIFI_PROVISION_STATE_CONNECTED);
        }
        break;
    case WIFI_STATE_DISCONNECTED:
        if (user_callback != NULL)
        {
            user_callback(WIFI_PROVISION_STATE_DISCONNECTED);
        }
        break;
    case WIFI_STATE_CONNECT_FAIL:
        if (current_transport == WIFI_PROVISION_TRANSPORT_BLE)
        {
            wifi_provision_send_ble_status("failed", current_ssid, NULL,
                                           "connect_fail", NULL);
            is_configuring = false;
        }
        else if (is_configuring)
        {
            xEventGroupSetBits(prov_ev_group, PROV_WIFI_FAIL_BIT);
        }
        else
        {
            wifi_provision_start_apcfg();
        }
        if (user_callback != NULL)
        {
            user_callback(WIFI_PROVISION_STATE_CONNECT_FAIL);
        }
        break;
    default:
        break;
    }
}

static void wifi_scan_handle(wifi_ap_record_t *ap, int ap_count,
                             esp_err_t scan_result)
{
    ble_prov_wifi_scan_item_t items[WIFI_SCAN_MAX_VISIBLE_ITEMS] = {0};
    size_t item_count = 0;

    if (scan_result != ESP_OK)
    {
        ESP_LOGW(TAG, "AP 页面 Wi-Fi 扫描失败: %s", esp_err_to_name(scan_result));
        wifi_provision_send_web_scan_results(NULL, 0);
        return;
    }

    item_count = wifi_provision_collect_scan_items(ap, ap_count, items,
                                                   WIFI_SCAN_MAX_VISIBLE_ITEMS);

    ESP_LOGI(TAG, "AP 页面 Wi-Fi 扫描完成: raw=%d visible=%u", ap_count,
             (unsigned)item_count);
    wifi_provision_send_web_scan_results(items, item_count);
}

void ws_receive_handle(const char *data, int len)
{
    (void)len;

    cJSON *root = cJSON_Parse(data);
    if (root == NULL)
    {
        return;
    }

    cJSON *scan_js = cJSON_GetObjectItem(root, "scan");
    if (scan_js != NULL && cJSON_IsString(scan_js) &&
        strcmp(scan_js->valuestring, "start") == 0)
    {
        esp_err_t scan_ret = wifi_manager_scan(wifi_scan_handle);
        if (scan_ret != ESP_OK)
        {
            ESP_LOGW(TAG, "AP 页面触发 Wi-Fi 扫描失败: %s",
                     esp_err_to_name(scan_ret));
        }
    }

    cJSON *ssid_js = cJSON_GetObjectItem(root, "ssid");
    cJSON *pwd_js = cJSON_GetObjectItem(root, "password");
    if (ssid_js != NULL && pwd_js != NULL && cJSON_IsString(ssid_js) &&
        cJSON_IsString(pwd_js))
    {
        snprintf(current_ssid, sizeof(current_ssid), "%s",
                 ssid_js->valuestring);
        snprintf(current_password, sizeof(current_password), "%s",
                 pwd_js->valuestring);

        if (wifi_manager_set_credentials(current_ssid, current_password) ==
            ESP_OK)
        {
            current_transport = WIFI_PROVISION_TRANSPORT_AP;
            is_configuring = true;
            xEventGroupSetBits(prov_ev_group, PROV_WIFI_CONNECTED_BIT);
        }
        else
        {
            ESP_LOGE(TAG, "保存 Wi-Fi 凭据失败");
            send_status_to_web("failed", current_ssid, NULL);
        }
    }

    cJSON_Delete(root);
}

esp_err_t wifi_provision_init(wifi_provision_cb_t callback)
{
    BaseType_t task_ret = pdPASS;

    user_callback = callback;
    wifi_manager_init(internal_wifi_cb);
    wifi_provision_prepare_ble_service_name();

    if (prov_ev_group == NULL)
    {
        prov_ev_group = xEventGroupCreate();
        if (prov_ev_group == NULL)
        {
            ESP_LOGE(TAG, "创建配网事件组失败");
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_provision_task_started)
    {
        return ESP_OK;
    }

    task_ret = xTaskCreatePinnedToCore(wifi_provision_task, "prov_task", 4096,
                                       NULL, 3, NULL, 1);
    if (task_ret != pdPASS)
    {
        ESP_LOGE(TAG, "创建配网任务失败");
        return ESP_FAIL;
    }

    s_provision_task_started = true;
    return ESP_OK;
}

esp_err_t wifi_provision_start_auto(void)
{
    if (wifi_provision_has_credentials())
    {
        return wifi_manager_connect_saved();
    }

    wifi_provision_start_apcfg();
    return ESP_OK;
}

esp_err_t wifi_provision_start_blecfg(void)
{
    wifi_provision_cancel_scheduled_ble_stop();
    if (current_transport == WIFI_PROVISION_TRANSPORT_AP)
    {
        ws_server_stop();
        wifi_manager_stop_ap();
    }
    wifi_provision_prepare_ble_service_name();
    current_transport = WIFI_PROVISION_TRANSPORT_BLE;
    is_configuring = false;

    return ble_provision_transport_start(ble_service_name,
                                         wifi_provision_ble_receive_cb,
                                         wifi_provision_ble_state_cb, NULL);
}

esp_err_t wifi_provision_stop_blecfg(void)
{
    wifi_provision_cancel_scheduled_ble_stop();
    if (current_transport == WIFI_PROVISION_TRANSPORT_BLE && !is_configuring)
    {
        current_transport = WIFI_PROVISION_TRANSPORT_NONE;
    }
    return ble_provision_transport_stop();
}

esp_err_t wifi_provision_start_apcfg(void)
{
    ESP_LOGI(TAG, "启动 AP 配网模式...");
    return wifi_provision_switch_to_ap_fallback();
}

bool wifi_provision_is_ble_active(void)
{
    return ble_provision_transport_is_active();
}

bool wifi_provision_is_ap_active(void)
{
    return current_transport == WIFI_PROVISION_TRANSPORT_AP;
}

esp_err_t wifi_provision_get_ble_service_name(char *service_name,
                                              size_t service_name_len)
{
    wifi_provision_prepare_ble_service_name();
    if (service_name == NULL || service_name_len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(service_name, service_name_len, "%s", ble_service_name);
    return ESP_OK;
}

bool wifi_provision_is_connected(void)
{
    return wifi_manager_is_connected();
}

esp_err_t wifi_provision_get_ip(char *ip_str, size_t ip_str_len)
{
    return wifi_manager_get_ip(ip_str, ip_str_len);
}

esp_err_t wifi_provision_set_power_save(bool enable)
{
    return wifi_manager_set_power_save(enable);
}

esp_err_t wifi_provision_set_credentials(const char *ssid,
                                         const char *password)
{
    return wifi_manager_set_credentials(ssid, password);
}

bool wifi_provision_has_credentials(void)
{
    return wifi_manager_has_credentials();
}
