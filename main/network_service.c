#include "network_service.h"

#include <lwip/netdb.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_provision.h"

static const char *TAG = "NETWORK_SERVICE";
static const char *kProbeHosts[] = {"api.tenclass.net", "mqtt.xiaozhi.me"};
static const uint32_t kProbeAttemptMax = 15;

static TaskHandle_t s_network_task_handle = NULL;
static volatile network_service_state_t s_network_state =
    NETWORK_SERVICE_STATE_OFFLINE;
static char s_network_ip[16] = {0};
static bool s_portal_requested = false;

static void network_service_enter_portal_required_state(void) {
    esp_err_t ret = wifi_provision_start_apcfg();

    s_network_ip[0] = '\0';
    if (ret == ESP_OK) {
        s_portal_requested = true;
        s_network_state = NETWORK_SERVICE_STATE_PORTAL_REQUIRED;
    } else {
        ESP_LOGE(TAG, "start AP fallback failed: %s", esp_err_to_name(ret));
        s_portal_requested = false;
        s_network_state = NETWORK_SERVICE_STATE_ERROR;
    }
}

static bool resolve_hostname_once(const char *hostname) {
    struct addrinfo hints = {0};
    struct addrinfo *result = NULL;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int err = getaddrinfo(hostname, "443", &hints, &result);
    if (err == 0 && result != NULL) {
        freeaddrinfo(result);
        ESP_LOGI(TAG, "network service ready: %s", hostname);
        return true;
    }

    if (result != NULL) {
        freeaddrinfo(result);
    }

    ESP_LOGW(TAG, "network service not ready yet: host=%s err=%d", hostname,
             err);
    return false;
}

static esp_err_t probe_network_services_ready(void) {
    for (size_t host_index = 0;
         host_index < (sizeof(kProbeHosts) / sizeof(kProbeHosts[0]));
         ++host_index) {
        const char *hostname = kProbeHosts[host_index];
        for (uint32_t attempt = 1; attempt <= kProbeAttemptMax; ++attempt) {
            if (resolve_hostname_once(hostname)) {
                break;
            }

            if (attempt == kProbeAttemptMax) {
                ESP_LOGW(TAG,
                         "network service probe timed out: host=%s attempts=%u",
                         hostname, (unsigned)attempt);
                return ESP_ERR_TIMEOUT;
            }

            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    return ESP_OK;
}

static void refresh_connected_ip(void) {
    char ip[16] = {0};
    if (wifi_provision_get_ip(ip, sizeof(ip)) == ESP_OK) {
        if (strcmp(s_network_ip, ip) != 0) {
            strncpy(s_network_ip, ip, sizeof(s_network_ip) - 1);
            s_network_ip[sizeof(s_network_ip) - 1] = '\0';
            ESP_LOGI(TAG, "Wi-Fi connected, IP: %s", s_network_ip);
        }
    } else {
        s_network_ip[0] = '\0';
    }
}

static void network_service_task(void *pv_parameter) {
    (void)pv_parameter;

    esp_err_t ret = ESP_OK;

    if (wifi_provision_has_credentials()) {
        s_network_state = NETWORK_SERVICE_STATE_CONNECTING;
        ret = wifi_provision_start_auto();
    } else {
        s_network_state = NETWORK_SERVICE_STATE_BLE_PROVISIONING;
        ret = wifi_provision_start_blecfg();
    }

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "initial BLE provisioning start failed, fallback to AP: %s",
                 esp_err_to_name(ret));
        network_service_enter_portal_required_state();
    }

    while (1) {
        if (wifi_provision_is_connected()) {
            s_portal_requested = false;
            refresh_connected_ip();

            if (s_network_state != NETWORK_SERVICE_STATE_SERVICE_READY) {
                s_network_state = NETWORK_SERVICE_STATE_WIFI_READY;
                if (probe_network_services_ready() == ESP_OK) {
                    s_network_state = NETWORK_SERVICE_STATE_SERVICE_READY;
                } else {
                    s_network_state = NETWORK_SERVICE_STATE_WIFI_READY;
                }
            }
        } else if (wifi_provision_has_credentials()) {
            s_portal_requested = false;
            if (s_network_state != NETWORK_SERVICE_STATE_CONNECTING) {
                ESP_LOGI(TAG, "Wi-Fi not connected yet, waiting in background");
            }
            s_network_ip[0] = '\0';
            s_network_state = NETWORK_SERVICE_STATE_CONNECTING;
        } else if (s_portal_requested || wifi_provision_is_ap_active()) {
            s_portal_requested = true;
            s_network_ip[0] = '\0';
            s_network_state = NETWORK_SERVICE_STATE_PORTAL_REQUIRED;
        } else if (wifi_provision_is_ble_active()) {
            s_network_ip[0] = '\0';
            s_network_state = NETWORK_SERVICE_STATE_BLE_PROVISIONING;
        } else {
            ret = wifi_provision_start_blecfg();
            s_network_ip[0] = '\0';
            if (ret == ESP_OK) {
                s_network_state = NETWORK_SERVICE_STATE_BLE_PROVISIONING;
            } else {
                ESP_LOGW(TAG, "restart BLE provisioning failed, fallback to AP: %s",
                         esp_err_to_name(ret));
                network_service_enter_portal_required_state();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t network_service_start(void) {
    if (s_network_task_handle != NULL) {
        return ESP_OK;
    }

    BaseType_t result =
        xTaskCreatePinnedToCore(network_service_task, "network_service",
                                1024 * 6, NULL, 5, &s_network_task_handle, 0);
    if (result != pdPASS) {
        s_network_task_handle = NULL;
        s_network_state = NETWORK_SERVICE_STATE_ERROR;
        return ESP_FAIL;
    }

    return ESP_OK;
}

network_service_state_t network_service_get_state(void) {
    return s_network_state;
}

bool network_service_is_service_ready(void) {
    return s_network_state == NETWORK_SERVICE_STATE_SERVICE_READY;
}

esp_err_t network_service_get_ip(char *ip_str, size_t ip_str_len) {
    if (ip_str == NULL || ip_str_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_network_ip[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    strncpy(ip_str, s_network_ip, ip_str_len - 1);
    ip_str[ip_str_len - 1] = '\0';
    return ESP_OK;
}

void network_service_request_portal(void) {
    network_service_enter_portal_required_state();
}
