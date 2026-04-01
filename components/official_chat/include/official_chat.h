#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct official_chat_handle *official_chat_handle_t;

typedef enum {
    OFFICIAL_CHAT_STATE_UNKNOWN = 0,
    OFFICIAL_CHAT_STATE_ACTIVATING,
    OFFICIAL_CHAT_STATE_UPGRADING,
    OFFICIAL_CHAT_STATE_IDLE,
    OFFICIAL_CHAT_STATE_CONNECTING,
    OFFICIAL_CHAT_STATE_LISTENING,
    OFFICIAL_CHAT_STATE_SPEAKING,
} official_chat_state_t;

typedef enum {
    OFFICIAL_CHAT_EVENT_STATE_CHANGED = 0,
    OFFICIAL_CHAT_EVENT_ACTIVATION_CODE,
    OFFICIAL_CHAT_EVENT_ACTIVATION_MESSAGE,
    OFFICIAL_CHAT_EVENT_USER_TEXT,
    OFFICIAL_CHAT_EVENT_ASSISTANT_TEXT,
    OFFICIAL_CHAT_EVENT_ASSETS_PROGRESS,
    OFFICIAL_CHAT_EVENT_UPGRADE_PROGRESS,
    OFFICIAL_CHAT_EVENT_ERROR,
    OFFICIAL_CHAT_EVENT_REBOOTING,
} official_chat_event_type_t;

typedef struct {
    official_chat_event_type_t type;
    official_chat_state_t state;
    const char *message;
    int progress;
    size_t speed_bytes_per_sec;
    esp_err_t error;
} official_chat_event_t;

typedef void (*official_chat_event_callback_t)(const official_chat_event_t *event,
                                               void *user_data);

typedef struct {
    int speak_volume;
    float record_gain_db;
    const char *websocket_url;
    const char *access_token;
    const char *ota_url;
} official_chat_config_t;

typedef struct {
    const char *endpoint;
    const char *publish_topic;
    const char *client_id;
    const char *username;
    const char *password;
    int keepalive;
} official_chat_mqtt_config_t;

typedef struct {
    const char *url;
    const char *token;
    int version;
} official_chat_websocket_config_t;

official_chat_handle_t official_chat_create(const official_chat_config_t *config);
void official_chat_destroy(official_chat_handle_t handle);
esp_err_t official_chat_set_event_callback(official_chat_handle_t handle,
                                           official_chat_event_callback_t callback,
                                           void *user_data);

esp_err_t official_chat_start(official_chat_handle_t handle);
esp_err_t official_chat_start_listening(official_chat_handle_t handle);
esp_err_t official_chat_start_synthetic_wakeword(official_chat_handle_t handle);
esp_err_t official_chat_toggle_chat(official_chat_handle_t handle);
esp_err_t official_chat_stop_listening(official_chat_handle_t handle);
esp_err_t official_chat_set_device_aec_enabled(official_chat_handle_t handle,
                                               bool enabled);
bool official_chat_get_device_aec_enabled(official_chat_handle_t handle);
official_chat_state_t official_chat_get_state(official_chat_handle_t handle);
esp_err_t official_chat_set_mqtt_protocol_config(
    const official_chat_mqtt_config_t *config);
esp_err_t official_chat_set_websocket_protocol_config(
    const official_chat_websocket_config_t *config);
esp_err_t official_chat_reload_protocol(official_chat_handle_t handle);

#ifdef __cplusplus
}
#endif
