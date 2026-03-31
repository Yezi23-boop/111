/**
 * @file wifi_manager.c
 * @brief Wi-Fi 驱动包装层，负责 STA/AP 配网能力。
 */

#include "wifi_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "wifi_manager_private.h"

#define TAG "wifi_mgr"
#define WIFI_PROVISION_NVS_NAMESPACE "wifi_provision"
#define WIFI_PROVISION_NVS_KEY_SSID "ssid"
#define WIFI_PROVISION_NVS_KEY_PASSWORD "password"

static wifi_manager_config_internal_t g_config = WIFI_MANAGER_DEFAULT_CONFIG();
static int sta_connect_count = 0;
static esp_netif_t *ap_netif = NULL;
static p_wifi_state_callback wifi_state_cb = NULL;
static bool is_sta_connected = false;
static char stored_ssid[sizeof(((wifi_config_t *)0)->sta.ssid)] = {0};
static char stored_password[sizeof(((wifi_config_t *)0)->sta.password)] = {0};

static SemaphoreHandle_t scan_semaphore = NULL;
static TaskHandle_t scan_task_handle = NULL;

typedef struct {
    p_wifi_scan_callback cb;
} wifi_scan_task_ctx_t;

static const char *wifi_manager_get_fallback_ssid(void);
static const char *wifi_manager_get_fallback_password(void);
static void wifi_manager_copy_credentials(char *ssid_dst, size_t ssid_dst_len,
                                          char *password_dst,
                                          size_t password_dst_len,
                                          const char *ssid,
                                          const char *password);
static esp_err_t wifi_manager_ensure_nvs_ready(void);
static esp_err_t wifi_manager_load_credentials_from_nvs(void);
static esp_err_t wifi_manager_store_credentials_to_nvs(const char *ssid,
                                                       const char *password);
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data);
static void scan_task(void *pv_parameters);

static esp_err_t wifi_manager_ensure_nvs_ready(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 初始化需要擦除，正在重试");
        ret = nvs_flash_erase();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "擦除 NVS 失败: %s", esp_err_to_name(ret));
            return ret;
        }
        ret = nvs_flash_init();
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化 NVS 失败: %s", esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}

static void wifi_manager_copy_credentials(char *ssid_dst, size_t ssid_dst_len,
                                          char *password_dst,
                                          size_t password_dst_len,
                                          const char *ssid,
                                          const char *password) {
    snprintf(ssid_dst, ssid_dst_len, "%s", ssid != NULL ? ssid : "");
    snprintf(password_dst, password_dst_len, "%s",
             password != NULL ? password : "");
}

static esp_err_t wifi_manager_load_credentials_from_nvs(void) {
    nvs_handle_t handle = 0;
    esp_err_t ret =
        nvs_open(WIFI_PROVISION_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        stored_ssid[0] = '\0';
        stored_password[0] = '\0';
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "打开 Wi-Fi 凭据命名空间失败: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    size_t ssid_len = sizeof(stored_ssid);
    size_t password_len = sizeof(stored_password);
    ret = nvs_get_str(handle, WIFI_PROVISION_NVS_KEY_SSID, stored_ssid,
                      &ssid_len);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        stored_ssid[0] = '\0';
        stored_password[0] = '\0';
        ret = ESP_OK;
        goto cleanup;
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "读取保存的 SSID 失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    ret = nvs_get_str(handle, WIFI_PROVISION_NVS_KEY_PASSWORD, stored_password,
                      &password_len);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        stored_password[0] = '\0';
        ret = ESP_OK;
    } else if (ret != ESP_OK) {
        ESP_LOGW(TAG, "读取保存的密码失败: %s", esp_err_to_name(ret));
    }

cleanup:
    nvs_close(handle);
    return ret;
}

static esp_err_t wifi_manager_store_credentials_to_nvs(const char *ssid,
                                                       const char *password) {
    nvs_handle_t handle = 0;
    esp_err_t ret =
        nvs_open(WIFI_PROVISION_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "打开 Wi-Fi 凭据命名空间失败: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_str(handle, WIFI_PROVISION_NVS_KEY_SSID, ssid);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "保存 SSID 失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    ret = nvs_set_str(handle, WIFI_PROVISION_NVS_KEY_PASSWORD, password);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "保存密码失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    ret = nvs_commit(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "提交 Wi-Fi 凭据失败: %s", esp_err_to_name(ret));
    }

cleanup:
    nvs_close(handle);
    return ret;
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
    (void)arg;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "STA 已就绪，等待显式连接请求");
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "已连接到 AP");
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                if (is_sta_connected) {
                    is_sta_connected = false;
                    if (wifi_state_cb != NULL) {
                        wifi_state_cb(WIFI_STATE_DISCONNECTED);
                    }
                }

                if (sta_connect_count < g_config.max_retry) {
                    esp_wifi_connect();
                    sta_connect_count++;
                    ESP_LOGI(TAG, "重试 Wi-Fi 连接... (%d/%d)",
                             sta_connect_count, g_config.max_retry);
                } else if (wifi_state_cb != NULL) {
                    wifi_state_cb(WIFI_STATE_CONNECT_FAIL);
                }
                break;
            case WIFI_EVENT_AP_STACONNECTED:
                ESP_LOGI(TAG, "客户端已连接到热点");
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "获取到 STA IP: " IPSTR, IP2STR(&event->ip_info.ip));
        is_sta_connected = true;
        sta_connect_count = 0;
        if (wifi_state_cb != NULL) {
            wifi_state_cb(WIFI_STATE_CONNECTED);
        }
    }
}

void wifi_manager_init(p_wifi_state_callback callback) {
    wifi_state_cb = callback;

    ESP_ERROR_CHECK(wifi_manager_ensure_nvs_ready());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();
    ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(wifi_manager_load_credentials_from_nvs());

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &event_handler, NULL));

    scan_semaphore = xSemaphoreCreateBinary();
    xSemaphoreGive(scan_semaphore);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi 管理器初始化成功");
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password) {
    if (ssid == NULL || password == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t wifi_config = {0};
    wifi_manager_copy_credentials(
        (char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid),
        (char *)wifi_config.sta.password, sizeof(wifi_config.sta.password),
        ssid, password);

    sta_connect_count = 0;

    wifi_mode_t mode = WIFI_MODE_NULL;
    ESP_ERROR_CHECK(esp_wifi_get_mode(&mode));
    if (mode != WIFI_MODE_APSTA && mode != WIFI_MODE_STA) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    }

    esp_wifi_disconnect();
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    return esp_wifi_connect();
}

esp_err_t wifi_manager_connect_saved(void) {
    const char *ssid = stored_ssid;
    const char *password = stored_password;

    if (ssid[0] == '\0') {
        ssid = wifi_manager_get_fallback_ssid();
        password = wifi_manager_get_fallback_password();
    }
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    if (ssid != stored_ssid || password != stored_password) {
        wifi_manager_copy_credentials(stored_ssid, sizeof(stored_ssid),
                                      stored_password, sizeof(stored_password),
                                      ssid, password);
    }
    return wifi_manager_connect(ssid, password);
}

esp_err_t wifi_manager_ap(void) {
    wifi_mode_t mode = WIFI_MODE_NULL;
    ESP_ERROR_CHECK(esp_wifi_get_mode(&mode));

    wifi_config_t wifi_config = {
        .ap =
            {
                .ssid_len = strlen(g_config.ap_ssid),
                .channel = 1,
                .max_connection = 4,
                .authmode = WIFI_AUTH_WPA2_PSK,
            },
    };
    wifi_manager_copy_credentials((char *)wifi_config.ap.ssid,
                                  sizeof(wifi_config.ap.ssid),
                                  (char *)wifi_config.ap.password,
                                  sizeof(wifi_config.ap.password),
                                  g_config.ap_ssid, g_config.ap_password);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    esp_netif_ip_info_t ip_info = {0};
    int ip[4] = {0};
    sscanf(g_config.ap_ip, "%d.%d.%d.%d", &ip[0], &ip[1], &ip[2], &ip[3]);
    IP4_ADDR(&ip_info.ip, ip[0], ip[1], ip[2], ip[3]);
    IP4_ADDR(&ip_info.gw, ip[0], ip[1], ip[2], ip[3]);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);

    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        esp_err_t dhcp_stop_ret = esp_netif_dhcps_stop(ap_netif);
        if (dhcp_stop_ret != ESP_OK &&
            dhcp_stop_ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
            ESP_LOGE(TAG, "停止 AP DHCP 失败: %s",
                     esp_err_to_name(dhcp_stop_ret));
            return dhcp_stop_ret;
        }
    }

    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif, &ip_info));

    if (mode != WIFI_MODE_APSTA) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    }
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        esp_err_t dhcp_restart = esp_netif_dhcps_start(ap_netif);
        if (dhcp_restart != ESP_OK &&
            dhcp_restart != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
            ESP_LOGE(TAG, "启动 AP DHCP 失败: %s",
                     esp_err_to_name(dhcp_restart));
            return dhcp_restart;
        }
    }

    if (esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK) {
        ESP_LOGI(TAG, "AP 配网门户已就绪: ssid=%s url=http://" IPSTR "/",
                 g_config.ap_ssid, IP2STR(&ip_info.ip));
    } else {
        ESP_LOGI(TAG, "AP 配网门户已就绪: ssid=%s url=http://%s/",
                 g_config.ap_ssid, g_config.ap_ip);
    }
    return ESP_OK;
}

esp_err_t wifi_manager_stop_ap(void) {
    return esp_wifi_set_mode(WIFI_MODE_STA);
}

static void scan_task(void *pv_parameters) {
    wifi_scan_task_ctx_t *ctx = (wifi_scan_task_ctx_t *)pv_parameters;
    wifi_scan_config_t scan_config = {0};

    if (esp_wifi_scan_start(&scan_config, true) == ESP_OK) {
        uint16_t ap_num = 0;
        esp_wifi_scan_get_ap_num(&ap_num);
        if (ap_num > 0) {
            wifi_ap_record_t *ap_records =
                malloc(sizeof(wifi_ap_record_t) * ap_num);
            if (ap_records != NULL) {
                esp_wifi_scan_get_ap_records(&ap_num, ap_records);
                if (ctx->cb != NULL) {
                    ctx->cb(ap_records, ap_num);
                }
                free(ap_records);
            }
        } else if (ctx->cb != NULL) {
            ctx->cb(NULL, 0);
        }
    }

    xSemaphoreGive(scan_semaphore);
    scan_task_handle = NULL;
    free(ctx);
    vTaskDelete(NULL);
}

esp_err_t wifi_manager_scan(p_wifi_scan_callback callback) {
    if (xSemaphoreTake(scan_semaphore, 0) == pdTRUE) {
        wifi_scan_task_ctx_t *ctx = malloc(sizeof(wifi_scan_task_ctx_t));
        if (ctx == NULL) {
            xSemaphoreGive(scan_semaphore);
            return ESP_ERR_NO_MEM;
        }

        ctx->cb = callback;
        xTaskCreate(scan_task, "wifi_scan", 4096, ctx, 5, &scan_task_handle);
        return ESP_OK;
    }
    return ESP_ERR_INVALID_STATE;
}

esp_err_t wifi_manager_get_ip(char *ip_str, size_t ip_str_len) {
    if (!is_sta_connected || ip_str == NULL || ip_str_len < 16) {
        return ESP_FAIL;
    }

    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif == NULL) {
        return ESP_FAIL;
    }

    esp_netif_ip_info_t ip_info = {0};
    if (esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK) {
        snprintf(ip_str, ip_str_len, IPSTR, IP2STR(&ip_info.ip));
        return ESP_OK;
    }
    return ESP_FAIL;
}

bool wifi_manager_is_connected(void) {
    return is_sta_connected;
}

esp_err_t wifi_manager_set_power_save(bool enable) {
    return esp_wifi_set_ps(enable ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
}

esp_err_t wifi_manager_set_credentials(const char *ssid,
                                       const char *password) {
    if (ssid == NULL || password == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_manager_copy_credentials(stored_ssid, sizeof(stored_ssid),
                                  stored_password, sizeof(stored_password),
                                  ssid, password);
    return wifi_manager_store_credentials_to_nvs(ssid, password);
}

bool wifi_manager_has_credentials(void) {
    return stored_ssid[0] != '\0' || wifi_manager_get_fallback_ssid()[0] != '\0';
}

static const char *wifi_manager_get_fallback_ssid(void) {
#if defined(CONFIG_WIFI_SSID)
    return CONFIG_WIFI_SSID;
#else
    return "";
#endif
}

static const char *wifi_manager_get_fallback_password(void) {
#if defined(CONFIG_WIFI_PASSWD)
    return CONFIG_WIFI_PASSWD;
#else
    return "";
#endif
}
