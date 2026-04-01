#include "official_chat_service.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "network_service.h"
#include "official_chat.h"
#include "sdkconfig.h"

static const char *TAG = "official_chat_srv";
static const size_t kLastUserTextMaxBytes = 192;
static const size_t kLastAssistantTextMaxBytes = 256;
static const size_t kMessageHistoryCapacity = 8;

static TaskHandle_t s_service_task_handle = NULL;
static official_chat_handle_t s_chat_handle = NULL;
static volatile bool s_foreground_requested = false;
static volatile official_chat_service_state_t s_service_state =
    OFFICIAL_CHAT_SERVICE_STATE_STOPPED;
static volatile esp_err_t s_last_error = ESP_OK;
static StaticSemaphore_t s_text_mutex_buffer;
static SemaphoreHandle_t s_text_mutex = NULL;
static char s_last_user_text[192] = {0};
static char s_last_assistant_text[256] = {0};
static official_chat_service_message_t s_message_history[8] = {0};
static size_t s_message_count = 0;

static void official_chat_service_lock(void) {
    if (s_text_mutex != NULL) {
        xSemaphoreTake(s_text_mutex, portMAX_DELAY);
    }
}

static void official_chat_service_unlock(void) {
    if (s_text_mutex != NULL) {
        xSemaphoreGive(s_text_mutex);
    }
}

static void official_chat_service_store_text_locked(char *target,
                                                    size_t target_size,
                                                    const char *text) {
    if (target == NULL || target_size == 0 || text == NULL) {
        return;
    }

    snprintf(target, target_size, "%s", text);
}

static void official_chat_service_enqueue_message_locked(
    official_chat_service_message_role_t role, const char *text) {
    official_chat_service_message_t message = {
        .role = role,
    };

    snprintf(message.text, sizeof(message.text), "%s",
             text != NULL ? text : "");

    if (s_message_count == kMessageHistoryCapacity) {
        memmove(&s_message_history[0], &s_message_history[1],
                (kMessageHistoryCapacity - 1) *
                    sizeof(s_message_history[0]));
        s_message_history[kMessageHistoryCapacity - 1] = message;
        return;
    }

    s_message_history[s_message_count] = message;
    s_message_count++;
}

static esp_err_t official_chat_service_copy_text_locked(const char *source,
                                                        char *buffer,
                                                        size_t size) {
    if (buffer == NULL || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const bool has_text = source != NULL && source[0] != '\0';
    if (has_text) {
        snprintf(buffer, size, "%s", source);
    } else {
        buffer[0] = '\0';
    }

    return has_text ? ESP_OK : ESP_ERR_NOT_FOUND;
}

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
        case OFFICIAL_CHAT_EVENT_USER_TEXT:
            official_chat_service_lock();
            official_chat_service_store_text_locked(s_last_user_text,
                                                    kLastUserTextMaxBytes,
                                                    event->message);
            official_chat_service_enqueue_message_locked(
                OFFICIAL_CHAT_SERVICE_MESSAGE_ROLE_USER, event->message);
            official_chat_service_unlock();
            ESP_LOGI(TAG, "last_user_text=%s",
                     event->message != NULL ? event->message : "");
            break;
        case OFFICIAL_CHAT_EVENT_ASSISTANT_TEXT:
            official_chat_service_lock();
            official_chat_service_store_text_locked(s_last_assistant_text,
                                                    kLastAssistantTextMaxBytes,
                                                    event->message);
            official_chat_service_enqueue_message_locked(
                OFFICIAL_CHAT_SERVICE_MESSAGE_ROLE_ASSISTANT, event->message);
            official_chat_service_unlock();
            ESP_LOGI(TAG, "last_assistant_text=%s",
                     event->message != NULL ? event->message : "");
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

    if (s_text_mutex == NULL) {
        s_text_mutex = xSemaphoreCreateMutexStatic(&s_text_mutex_buffer);
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

size_t official_chat_service_get_message_count(void) {
    size_t message_count = 0;

    official_chat_service_lock();
    message_count = s_message_count;
    official_chat_service_unlock();

    return message_count;
}

esp_err_t official_chat_service_get_message(
    size_t index, official_chat_service_message_t *out_message) {
    if (out_message == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_ERR_NOT_FOUND;
    official_chat_service_lock();
    if (index < s_message_count) {
        *out_message = s_message_history[index];
        ret = ESP_OK;
    }
    official_chat_service_unlock();

    return ret;
}

esp_err_t official_chat_service_get_last_user_text(char *buffer, size_t size) {
    official_chat_service_lock();
    esp_err_t ret = official_chat_service_copy_text_locked(s_last_user_text,
                                                           buffer, size);
    official_chat_service_unlock();
    return ret;
}

esp_err_t official_chat_service_get_last_assistant_text(char *buffer,
                                                        size_t size) {
    official_chat_service_lock();
    esp_err_t ret = official_chat_service_copy_text_locked(
        s_last_assistant_text, buffer, size);
    official_chat_service_unlock();
    return ret;
}
