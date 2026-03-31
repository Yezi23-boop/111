#include "official_chat_service.h"

#include <stdbool.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_service.h"
#include "official_chat.h"
#include "sdkconfig.h"

static const char *TAG = "official_chat_srv";

static TaskHandle_t s_service_task_handle = NULL;
static official_chat_handle_t s_chat_handle = NULL;
static volatile bool s_foreground_requested = false;
static volatile official_chat_service_state_t s_service_state =
    OFFICIAL_CHAT_SERVICE_STATE_STOPPED;
static volatile esp_err_t s_last_error = ESP_OK;

static official_chat_service_state_t map_official_chat_state(
    official_chat_state_t state) {
    switch (state) {
        case OFFICIAL_CHAT_STATE_ACTIVATING:
            return OFFICIAL_CHAT_SERVICE_STATE_ACTIVATING;
        case OFFICIAL_CHAT_STATE_CONNECTING:
            return OFFICIAL_CHAT_SERVICE_STATE_CONNECTING;
        case OFFICIAL_CHAT_STATE_IDLE:
            return OFFICIAL_CHAT_SERVICE_STATE_IDLE;
        case OFFICIAL_CHAT_STATE_LISTENING:
            return OFFICIAL_CHAT_SERVICE_STATE_LISTENING;
        case OFFICIAL_CHAT_STATE_SPEAKING:
            return OFFICIAL_CHAT_SERVICE_STATE_SPEAKING;
        case OFFICIAL_CHAT_STATE_UPGRADING:
            return OFFICIAL_CHAT_SERVICE_STATE_STARTING;
        case OFFICIAL_CHAT_STATE_UNKNOWN:
        default:
            return OFFICIAL_CHAT_SERVICE_STATE_STARTING;
    }
}

const char *official_chat_service_state_to_string(
    official_chat_service_state_t state) {
    switch (state) {
        case OFFICIAL_CHAT_SERVICE_STATE_STOPPED:
            return "stopped";
        case OFFICIAL_CHAT_SERVICE_STATE_WAITING_NETWORK:
            return "waiting_network";
        case OFFICIAL_CHAT_SERVICE_STATE_STARTING:
            return "starting";
        case OFFICIAL_CHAT_SERVICE_STATE_ACTIVATING:
            return "activating";
        case OFFICIAL_CHAT_SERVICE_STATE_CONNECTING:
            return "connecting";
        case OFFICIAL_CHAT_SERVICE_STATE_IDLE:
            return "idle";
        case OFFICIAL_CHAT_SERVICE_STATE_LISTENING:
            return "listening";
        case OFFICIAL_CHAT_SERVICE_STATE_SPEAKING:
            return "speaking";
        case OFFICIAL_CHAT_SERVICE_STATE_ERROR:
        default:
            return "error";
    }
}

static void official_chat_service_event_cb(const official_chat_event_t *event,
                                           void *user_data) {
    (void)user_data;
    if (event == NULL) {
        return;
    }

    switch (event->type) {
        case OFFICIAL_CHAT_EVENT_STATE_CHANGED:
            s_service_state = map_official_chat_state(event->state);
            ESP_LOGI(TAG, "state=%s",
                     official_chat_service_state_to_string(s_service_state));
            break;
        case OFFICIAL_CHAT_EVENT_ERROR:
            s_last_error = event->error;
            s_service_state = OFFICIAL_CHAT_SERVICE_STATE_ERROR;
            ESP_LOGE(TAG, "error=%s message=%s", esp_err_to_name(event->error),
                     event->message != NULL ? event->message : "");
            break;
        case OFFICIAL_CHAT_EVENT_ACTIVATION_CODE:
        case OFFICIAL_CHAT_EVENT_ACTIVATION_MESSAGE:
        case OFFICIAL_CHAT_EVENT_ASSETS_PROGRESS:
        case OFFICIAL_CHAT_EVENT_UPGRADE_PROGRESS:
        case OFFICIAL_CHAT_EVENT_REBOOTING:
        default:
            break;
    }
}

static esp_err_t official_chat_service_start_internal(void) {
    official_chat_config_t config = {
        .speak_volume = 60,
        .record_gain_db = 24.0f,
        .websocket_url = NULL,
        .access_token = NULL,
        .ota_url = CONFIG_OFFICIAL_CHAT_OTA_URL,
    };

    s_service_state = OFFICIAL_CHAT_SERVICE_STATE_STARTING;
    s_last_error = ESP_OK;

    s_chat_handle = official_chat_create(&config);
    if (s_chat_handle == NULL) {
        s_last_error = ESP_FAIL;
        s_service_state = OFFICIAL_CHAT_SERVICE_STATE_ERROR;
        ESP_LOGE(TAG, "official_chat_create failed");
        return ESP_FAIL;
    }

    esp_err_t ret = official_chat_set_event_callback(
        s_chat_handle, official_chat_service_event_cb, NULL);
    if (ret != ESP_OK) {
        s_last_error = ret;
        s_service_state = OFFICIAL_CHAT_SERVICE_STATE_ERROR;
        ESP_LOGE(TAG, "official_chat_set_event_callback failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    ret = official_chat_start(s_chat_handle);
    if (ret != ESP_OK) {
        s_last_error = ret;
        s_service_state = OFFICIAL_CHAT_SERVICE_STATE_ERROR;
        ESP_LOGE(TAG, "official_chat_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

static void official_chat_service_task(void *arg) {
    (void)arg;

    while (1) {
        if (!s_foreground_requested) {
            if (s_chat_handle == NULL) {
                s_service_state = OFFICIAL_CHAT_SERVICE_STATE_STOPPED;
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        if (s_chat_handle != NULL) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        if (!network_service_is_service_ready()) {
            s_service_state = OFFICIAL_CHAT_SERVICE_STATE_WAITING_NETWORK;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (official_chat_service_start_internal() != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t official_chat_service_init(void) {
    if (s_service_task_handle != NULL) {
        return ESP_OK;
    }

    BaseType_t result = xTaskCreatePinnedToCore(
        official_chat_service_task, "official_chat_service", 1024 * 8, NULL, 5,
        &s_service_task_handle, 0);
    if (result != pdPASS) {
        s_service_task_handle = NULL;
        s_service_state = OFFICIAL_CHAT_SERVICE_STATE_ERROR;
        s_last_error = ESP_FAIL;
        return ESP_FAIL;
    }

    return ESP_OK;
}

void official_chat_service_enter_foreground(void) {
    s_foreground_requested = true;
}

void official_chat_service_leave_foreground(void) {
    s_foreground_requested = false;
}

official_chat_service_state_t official_chat_service_get_state(void) {
    return s_service_state;
}

esp_err_t official_chat_service_get_last_error(void) {
    return s_last_error;
}
