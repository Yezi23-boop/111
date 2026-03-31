/**
 * @file wifi_provision.c
 * @brief 配网协调器，统一管理 AP 门户和 STA 启动路径。
 */

#include "wifi_provision.h"

#include <string.h>

#include <cJSON.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include "wifi_manager.h"
#include "ws_server.h"

#define TAG "wifi_prov"

extern const uint8_t apcfg_html_start[] asm("_binary_apcfg_html_start");
extern const uint8_t apcfg_html_end[] asm("_binary_apcfg_html_end");

static EventGroupHandle_t prov_ev_group = NULL;

#define PROV_WIFI_CONNECTED_BIT BIT0
#define PROV_WIFI_FAIL_BIT BIT1
#define PROV_WIFI_SUCCESS_BIT BIT2

static char current_ssid[33] = {0};
static char current_password[65] = {0};
static bool is_configuring = false;
static wifi_provision_cb_t user_callback = NULL;

static void send_status_to_web(const char *status, const char *ssid,
                               const char *ip) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }

    cJSON_AddStringToObject(root, "status", status);
    cJSON_AddStringToObject(root, "ssid", ssid != NULL ? ssid : "");
    if (ip != NULL) {
        cJSON_AddStringToObject(root, "ip", ip);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str != NULL) {
        ws_server_send((uint8_t *)json_str, strlen(json_str));
        cJSON_free(json_str);
    }
    cJSON_Delete(root);
}

static void wifi_provision_task(void *arg) {
    (void)arg;

    const EventBits_t all_bits =
        PROV_WIFI_CONNECTED_BIT | PROV_WIFI_FAIL_BIT | PROV_WIFI_SUCCESS_BIT;
    while (1) {
        EventBits_t bits = xEventGroupWaitBits(prov_ev_group, all_bits, pdTRUE,
                                               pdFALSE, portMAX_DELAY);

        if ((bits & PROV_WIFI_CONNECTED_BIT) != 0) {
            ESP_LOGI(TAG, "开始连接 Wi-Fi: %s", current_ssid);
            wifi_manager_connect(current_ssid, current_password);
        }

        if ((bits & PROV_WIFI_FAIL_BIT) != 0) {
            ESP_LOGW(TAG, "Wi-Fi 配网连接失败");
            send_status_to_web("failed", current_ssid, NULL);
            is_configuring = false;
        }

        if ((bits & PROV_WIFI_SUCCESS_BIT) != 0) {
            char ip_str[16] = {0};
            wifi_manager_get_ip(ip_str, sizeof(ip_str));
            ESP_LOGI(TAG, "Wi-Fi 配网连接成功, IP: %s", ip_str);
            send_status_to_web("connected", current_ssid, ip_str);
            is_configuring = false;

            vTaskDelay(pdMS_TO_TICKS(2000));
            ws_server_stop();
            wifi_manager_stop_ap();
        }
    }
}

static void internal_wifi_cb(WIFI_STATE state) {
    switch (state) {
        case WIFI_STATE_CONNECTED:
            if (is_configuring) {
                xEventGroupSetBits(prov_ev_group, PROV_WIFI_SUCCESS_BIT);
            }
            if (user_callback != NULL) {
                user_callback(WIFI_PROVISION_STATE_CONNECTED);
            }
            break;
        case WIFI_STATE_DISCONNECTED:
            if (user_callback != NULL) {
                user_callback(WIFI_PROVISION_STATE_DISCONNECTED);
            }
            break;
        case WIFI_STATE_CONNECT_FAIL:
            if (is_configuring) {
                xEventGroupSetBits(prov_ev_group, PROV_WIFI_FAIL_BIT);
            } else {
                wifi_provision_start_apcfg();
            }
            if (user_callback != NULL) {
                user_callback(WIFI_PROVISION_STATE_CONNECT_FAIL);
            }
            break;
        default:
            break;
    }
}

void wifi_scan_handle(wifi_ap_record_t *ap, int ap_count) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }

    cJSON *wifi_list = cJSON_AddArrayToObject(root, "wifi_list");
    for (int i = 0; i < ap_count; ++i) {
        cJSON *ap_item = cJSON_CreateObject();
        if (ap_item == NULL) {
            continue;
        }

        cJSON_AddStringToObject(ap_item, "ssid", (const char *)ap[i].ssid);
        cJSON_AddNumberToObject(ap_item, "rssi", ap[i].rssi);
        cJSON_AddBoolToObject(ap_item, "encrypted",
                              ap[i].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(wifi_list, ap_item);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str != NULL) {
        ws_server_send((uint8_t *)json_str, strlen(json_str));
        cJSON_free(json_str);
    }
    cJSON_Delete(root);
}

void ws_receive_handle(const char *data, int len) {
    (void)len;

    cJSON *root = cJSON_Parse(data);
    if (root == NULL) {
        return;
    }

    cJSON *scan_js = cJSON_GetObjectItem(root, "scan");
    if (scan_js != NULL && cJSON_IsString(scan_js) &&
        strcmp(scan_js->valuestring, "start") == 0) {
        wifi_manager_scan(wifi_scan_handle);
    }

    cJSON *ssid_js = cJSON_GetObjectItem(root, "ssid");
    cJSON *pwd_js = cJSON_GetObjectItem(root, "password");
    if (ssid_js != NULL && pwd_js != NULL && cJSON_IsString(ssid_js) &&
        cJSON_IsString(pwd_js)) {
        snprintf(current_ssid, sizeof(current_ssid), "%s",
                 ssid_js->valuestring);
        snprintf(current_password, sizeof(current_password), "%s",
                 pwd_js->valuestring);

        if (wifi_manager_set_credentials(current_ssid, current_password) ==
            ESP_OK) {
            is_configuring = true;
            xEventGroupSetBits(prov_ev_group, PROV_WIFI_CONNECTED_BIT);
        } else {
            ESP_LOGE(TAG, "保存 Wi-Fi 凭据失败");
            send_status_to_web("failed", current_ssid, NULL);
        }
    }

    cJSON_Delete(root);
}

void wifi_provision_init(wifi_provision_cb_t callback) {
    user_callback = callback;
    wifi_manager_init(internal_wifi_cb);

    if (prov_ev_group == NULL) {
        prov_ev_group = xEventGroupCreate();
    }
    xTaskCreatePinnedToCore(wifi_provision_task, "prov_task", 4096, NULL, 3,
                            NULL, 1);
}

esp_err_t wifi_provision_start_auto(void) {
    if (wifi_provision_has_credentials()) {
        return wifi_manager_connect_saved();
    }

    wifi_provision_start_apcfg();
    return ESP_OK;
}

void wifi_provision_start_apcfg(void) {
    ESP_LOGI(TAG, "启动 AP 配网模式...");
    is_configuring = true;
    wifi_manager_ap();

    ws_server_config_t config = {
        .html_code = (const char *)apcfg_html_start,
        .cb = ws_receive_handle,
    };
    ws_server_start(&config);
}

bool wifi_provision_is_connected(void) {
    return wifi_manager_is_connected();
}

esp_err_t wifi_provision_get_ip(char *ip_str, size_t ip_str_len) {
    return wifi_manager_get_ip(ip_str, ip_str_len);
}

esp_err_t wifi_provision_set_power_save(bool enable) {
    return wifi_manager_set_power_save(enable);
}

esp_err_t wifi_provision_set_credentials(const char *ssid,
                                         const char *password) {
    return wifi_manager_set_credentials(ssid, password);
}

bool wifi_provision_has_credentials(void) {
    return wifi_manager_has_credentials();
}
