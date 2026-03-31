#include <stdio.h>
#include <string.h>
#include <lwip/netdb.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "official_chat.h"
#include "sdkconfig.h"
#include "wifi_provision.h"

#include "audio_codec.h"

static const char *TAG = "AI_CHAT_EXPERIMENT";
static const uint32_t kNetworkServiceMaxAttempts = 15;
static const char *kStartupProbeHosts[] = {"api.tenclass.net",
                                           "mqtt.xiaozhi.me"};

static const char *official_chat_state_to_string(official_chat_state_t state) {
    switch (state) {
        case OFFICIAL_CHAT_STATE_ACTIVATING:
            return "activating";
        case OFFICIAL_CHAT_STATE_UPGRADING:
            return "upgrading";
        case OFFICIAL_CHAT_STATE_IDLE:
            return "idle";
        case OFFICIAL_CHAT_STATE_CONNECTING:
            return "connecting";
        case OFFICIAL_CHAT_STATE_LISTENING:
            return "listening";
        case OFFICIAL_CHAT_STATE_SPEAKING:
            return "speaking";
        case OFFICIAL_CHAT_STATE_UNKNOWN:
        default:
            return "unknown";
    }
}

static void official_chat_event_cb(const official_chat_event_t *event,
                                   void *user_data) {
    (void)user_data;
    if (event == NULL) {
        return;
    }

    switch (event->type) {
        case OFFICIAL_CHAT_EVENT_STATE_CHANGED:
            ESP_LOGI(TAG, "official_chat state=%s",
                     official_chat_state_to_string(event->state));
            break;
        case OFFICIAL_CHAT_EVENT_ACTIVATION_CODE:
            ESP_LOGI(TAG, "activation code: %s",
                     event->message != NULL ? event->message : "");
            break;
        case OFFICIAL_CHAT_EVENT_ACTIVATION_MESSAGE:
            ESP_LOGI(TAG, "activation message: %s",
                     event->message != NULL ? event->message : "");
            break;
        case OFFICIAL_CHAT_EVENT_ASSETS_PROGRESS:
            ESP_LOGI(TAG, "assets progress=%d speed=%u", event->progress,
                     (unsigned)event->speed_bytes_per_sec);
            break;
        case OFFICIAL_CHAT_EVENT_UPGRADE_PROGRESS:
            ESP_LOGI(TAG, "upgrade progress=%d speed=%u", event->progress,
                     (unsigned)event->speed_bytes_per_sec);
            break;
        case OFFICIAL_CHAT_EVENT_ERROR:
            ESP_LOGE(TAG, "official_chat error=%s message=%s",
                     esp_err_to_name(event->error),
                     event->message != NULL ? event->message : "");
            break;
        case OFFICIAL_CHAT_EVENT_REBOOTING:
            ESP_LOGW(TAG, "official_chat requests reboot");
            break;
        default:
            break;
    }
}

static esp_err_t ai_chat_nvs_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS init failed, erasing and retrying");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

static esp_err_t wait_for_wifi_connected(void) {
    uint32_t wait_round = 0;
    while (!wifi_provision_is_connected()) {
        if ((wait_round % 10U) == 0U) {
            ESP_LOGI(TAG, "waiting for Wi-Fi connection...");
        }
        ++wait_round;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    char ip[16] = {0};
    if (wifi_provision_get_ip(ip, sizeof(ip)) == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi connected, IP: %s", ip);
    } else {
        ESP_LOGW(TAG, "Wi-Fi connected but IP not ready yet");
    }
    return ESP_OK;
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

static esp_err_t wait_for_network_services_ready(void) {
    for (size_t index = 0;
         index < (sizeof(kStartupProbeHosts) / sizeof(kStartupProbeHosts[0]));
         ++index) {
        const char *hostname = kStartupProbeHosts[index];
        for (uint32_t attempt = 1; attempt <= kNetworkServiceMaxAttempts;
             ++attempt) {
            if (resolve_hostname_once(hostname)) {
                break;
            }

            if (attempt == kNetworkServiceMaxAttempts) {
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

void app_main(void) {
    ESP_ERROR_CHECK(ai_chat_nvs_init());

    wifi_provision_init(NULL);
    ESP_ERROR_CHECK(wifi_provision_start_auto());
    ESP_ERROR_CHECK(wait_for_wifi_connected());
    if (wait_for_network_services_ready() != ESP_OK) {
        ESP_LOGW(TAG,
                 "network services not fully ready, continue official_chat startup");
    }

    ESP_ERROR_CHECK(audio_codec_init());
    audio_codec_set_volume(60);

    official_chat_config_t config = {
        .speak_volume = 60,
        .record_gain_db = 24.0f,
        .websocket_url = NULL,
        .access_token = NULL,
        .ota_url = CONFIG_OFFICIAL_CHAT_OTA_URL,
    };

    official_chat_handle_t chat = official_chat_create(&config);
    if (chat == NULL) {
        ESP_LOGE(TAG, "official_chat_create failed");
        return;
    }

    ESP_ERROR_CHECK(
        official_chat_set_event_callback(chat, official_chat_event_cb, NULL));
    ESP_ERROR_CHECK(official_chat_start(chat));

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
