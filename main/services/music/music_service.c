#include "music_service.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/idf_additions.h"
#include "freertos/portmacro.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "services/memory_watch/memory_watch_service.h"
#include "music_http_client.h"
#include "music_stream_player.h"

static const char *TAG = "music_service";
static const UBaseType_t kCommandQueueLength = 8U;
static const UBaseType_t kPlayerEventQueueLength = 8U;
/* QR HTTPS/TLS/JSON 同步执行于 owner task；16 KiB PSRAM 栈覆盖握手峰值。 */
static const uint32_t kTaskStackBytes = 16384U;
static const uint32_t kQrPollIntervalMs = 2000U;

typedef enum
{
    MUSIC_SERVICE_CMD_START = 0,
    MUSIC_SERVICE_CMD_TOGGLE,
    MUSIC_SERVICE_CMD_PAUSE,
    MUSIC_SERVICE_CMD_RESUME,
    MUSIC_SERVICE_CMD_PREVIOUS,
    MUSIC_SERVICE_CMD_NEXT,
    MUSIC_SERVICE_CMD_MODE,
    MUSIC_SERVICE_CMD_DESTROY,
    MUSIC_SERVICE_CMD_LOAD_SOURCE,
    MUSIC_SERVICE_CMD_QR_START,
    MUSIC_SERVICE_CMD_QR_CANCEL,
} music_service_command_type_t;

typedef struct
{
    music_service_command_type_t type;
    music_service_mode_t mode;
    uint32_t offset;
    char source_id[MUSIC_SERVICE_SOURCE_ID_MAX_BYTES];
    char track_id[MUSIC_SERVICE_TRACK_ID_MAX_BYTES];
} music_service_command_t;

static TaskHandle_t s_task_handle = NULL;
static QueueHandle_t s_command_queue = NULL;
static QueueHandle_t s_player_event_queue = NULL;
static StaticQueue_t s_command_queue_buffer;
static StaticQueue_t s_player_event_queue_buffer;
static uint8_t *s_command_queue_storage = NULL;
static uint8_t *s_player_event_queue_storage = NULL;
static music_stream_player_t *s_player = NULL;
/* 控制 JSON 与媒体流分别占用独立 HTTP 连接。 */
static music_http_control_client_t s_control_client = {0};
static music_service_catalog_snapshot_t *s_catalog = NULL;
static const uint32_t kPlayerStopTimeoutMs = 6000U;
static music_service_account_snapshot_t *s_account = NULL;
static uint8_t *s_qr_data = NULL;
static bool s_qr_polling = false;
static TickType_t s_qr_next_poll = 0;
static uint32_t s_command_sequence = 0U;
static portMUX_TYPE s_snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
static music_service_snapshot_t s_snapshot = {
    .state = MUSIC_SERVICE_STATE_STOPPED,
    .mode = MUSIC_SERVICE_MODE_REPEAT_ALL,
};

static void music_service_handle_track_action(const char *action);

static void music_service_set_account_error(const char *error_code)
{
    taskENTER_CRITICAL(&s_snapshot_lock);
    if (s_account != NULL)
    {
        s_account->state = MUSIC_SERVICE_ACCOUNT_ERROR;
        snprintf(s_account->error_code, sizeof(s_account->error_code), "%s",
                 error_code != NULL ? error_code : "music_account_failed");
    }
    taskEXIT_CRITICAL(&s_snapshot_lock);
}

static void music_service_apply_account_result(
    const music_http_account_result_t *result)
{
    taskENTER_CRITICAL(&s_snapshot_lock);
    if (s_account != NULL)
    {
        s_account->state = result->state;
        if (result->expires_at_ms != 0U)
        {
            s_account->expires_at_ms = result->expires_at_ms;
        }
        if (result->login_id[0] != '\0')
        {
            snprintf(s_account->login_id, sizeof(s_account->login_id), "%s",
                     result->login_id);
        }
        snprintf(s_account->error_code, sizeof(s_account->error_code), "%s",
                 result->error_code);
        if (result->qr_bytes > 0U && result->qr_data != NULL && s_qr_data != NULL)
        {
            memcpy(s_qr_data, result->qr_data, result->qr_bytes);
            s_account->qr_size = result->qr_size;
            s_account->qr_bytes = result->qr_bytes;
        }
    }
    taskEXIT_CRITICAL(&s_snapshot_lock);
}

static const char *music_service_mode_text(music_service_mode_t mode)
{
    switch (mode)
    {
    case MUSIC_SERVICE_MODE_REPEAT_ONE:
        return "repeat_one";
    case MUSIC_SERVICE_MODE_SHUFFLE:
        return "shuffle";
    case MUSIC_SERVICE_MODE_REPEAT_ALL:
    default:
        return "repeat_all";
    }
}

static void music_service_set_error(const char *error_code)
{
    taskENTER_CRITICAL(&s_snapshot_lock);
    s_snapshot.state = MUSIC_SERVICE_STATE_ERROR;
    s_snapshot.music_active = false;
    snprintf(s_snapshot.error_code, sizeof(s_snapshot.error_code), "%s",
             error_code != NULL ? error_code : "music_request_failed");
    taskEXIT_CRITICAL(&s_snapshot_lock);
}

static void music_service_catalog_set_loading(const char *source_id,
                                              uint32_t offset)
{
    if (s_catalog == NULL)
    {
        return;
    }
    taskENTER_CRITICAL(&s_snapshot_lock);
    s_catalog->valid = false;
    s_catalog->loading = true;
    snprintf(s_catalog->source_id, sizeof(s_catalog->source_id), "%s",
             source_id != NULL ? source_id : "");
    s_catalog->offset = offset;
    taskEXIT_CRITICAL(&s_snapshot_lock);
}

static void music_service_copy_result(
    const music_http_session_result_t *result)
{
    taskENTER_CRITICAL(&s_snapshot_lock);
    s_snapshot.state = result->state;
    s_snapshot.mode = result->mode;
    s_snapshot.position_ms = result->position_ms;
    snprintf(s_snapshot.music_session_id, sizeof(s_snapshot.music_session_id),
             "%s", result->music_session_id);
    snprintf(s_snapshot.stream_id, sizeof(s_snapshot.stream_id), "%s",
             result->stream_id);
    snprintf(s_snapshot.source_id, sizeof(s_snapshot.source_id), "%s",
             result->source_id);
    snprintf(s_snapshot.track_id, sizeof(s_snapshot.track_id), "%s",
             result->track_id);
    snprintf(s_snapshot.title, sizeof(s_snapshot.title), "%s", result->title);
    snprintf(s_snapshot.artist, sizeof(s_snapshot.artist), "%s",
             result->artist);
    snprintf(s_snapshot.error_code, sizeof(s_snapshot.error_code), "%s",
             result->error_code);
    s_snapshot.music_active = false;
    taskEXIT_CRITICAL(&s_snapshot_lock);
}

static esp_err_t music_service_copy_http_config(
    music_http_client_config_t *out_config)
{
    memory_watch_service_endpoint_snapshot_t endpoint = {0};
    esp_err_t ret = memory_watch_service_copy_endpoint_config(&endpoint);
    if (ret != ESP_OK)
    {
        return ret;
    }
    memset(out_config, 0, sizeof(*out_config));
    snprintf(out_config->base_url, sizeof(out_config->base_url), "%s",
             endpoint.base_url);
    snprintf(out_config->device_id, sizeof(out_config->device_id), "%s",
             endpoint.device_id);
    snprintf(out_config->device_token, sizeof(out_config->device_token), "%s",
             endpoint.device_token);
    out_config->timeout_ms = endpoint.timeout_ms;
    out_config->allow_insecure_http = endpoint.allow_insecure_http;
    return out_config->base_url[0] != '\0' && out_config->device_id[0] != '\0' &&
                   out_config->device_token[0] != '\0'
               ? ESP_OK
               : ESP_ERR_INVALID_STATE;
}

static void music_service_make_command_id(char *output, size_t output_size)
{
    const uint32_t sequence = ++s_command_sequence;
    snprintf(output, output_size, "watch-music-%08lx-%08lx",
             (unsigned long)esp_random(), (unsigned long)sequence);
}

static esp_err_t music_service_stop_player(void)
{
    if (s_player == NULL)
    {
        return ESP_OK;
    }
    const esp_err_t ret =
        music_stream_player_stop(s_player, kPlayerStopTimeoutMs);
    if (ret != ESP_OK)
    {
        return ret;
    }
    music_stream_player_release(s_player);
    s_player = NULL;
    music_stream_player_event_t stale_event;
    while (xQueueReceive(s_player_event_queue, &stale_event, 0U) == pdTRUE)
    {
        /* 停止事件已在 owner task 内收敛；丢弃旧 worker 的重复通知。 */
    }
    return ESP_OK;
}

static esp_err_t music_service_start_player(
    const music_http_client_config_t *config,
    const music_http_session_result_t *result)
{
    if (result->stream_id[0] == '\0')
    {
        return ESP_ERR_INVALID_RESPONSE;
    }
    const esp_err_t ret = music_stream_player_start(
        config, result->stream_id, s_player_event_queue, &s_player);
    if (ret == ESP_OK)
    {
        taskENTER_CRITICAL(&s_snapshot_lock);
        s_snapshot.state = MUSIC_SERVICE_STATE_BUFFERING;
        s_snapshot.buffered_bytes = 0U;
        taskEXIT_CRITICAL(&s_snapshot_lock);
    }
    return ret;
}

static void music_service_process_player_events(void)
{
    music_stream_player_event_t event;
    while (xQueueReceive(s_player_event_queue, &event, 0U) == pdTRUE)
    {
        switch (event.type)
        {
        case MUSIC_STREAM_PLAYER_EVENT_BUFFERING:
            taskENTER_CRITICAL(&s_snapshot_lock);
            s_snapshot.state = MUSIC_SERVICE_STATE_BUFFERING;
            s_snapshot.buffered_bytes = event.buffered_bytes;
            taskEXIT_CRITICAL(&s_snapshot_lock);
            break;
        case MUSIC_STREAM_PLAYER_EVENT_PLAYING:
            taskENTER_CRITICAL(&s_snapshot_lock);
            s_snapshot.state = MUSIC_SERVICE_STATE_PLAYING;
            s_snapshot.music_active = true;
            s_snapshot.buffered_bytes = event.buffered_bytes;
            taskEXIT_CRITICAL(&s_snapshot_lock);
            break;
        case MUSIC_STREAM_PLAYER_EVENT_ENDED:
            if (s_player != NULL && music_stream_player_is_stopped(s_player))
            {
                music_stream_player_release(s_player);
                s_player = NULL;
            }
            music_service_handle_track_action("next");
            break;
        case MUSIC_STREAM_PLAYER_EVENT_STOPPED:
            taskENTER_CRITICAL(&s_snapshot_lock);
            s_snapshot.state = MUSIC_SERVICE_STATE_PAUSED;
            s_snapshot.music_active = false;
            taskEXIT_CRITICAL(&s_snapshot_lock);
            break;
        case MUSIC_STREAM_PLAYER_EVENT_ERROR:
            music_service_set_error(esp_err_to_name(event.error));
            if (s_player != NULL && music_stream_player_is_stopped(s_player))
            {
                music_stream_player_release(s_player);
                s_player = NULL;
            }
            break;
        default:
            break;
        }
    }
}

static esp_err_t music_service_server_command(
    const char *action, music_service_mode_t mode,
    music_http_session_result_t *out_result)
{
    music_http_client_config_t config;
    esp_err_t ret = music_service_copy_http_config(&config);
    if (ret != ESP_OK)
    {
        return ret;
    }
    char session_id[MUSIC_SERVICE_SESSION_ID_MAX_BYTES];
    taskENTER_CRITICAL(&s_snapshot_lock);
    snprintf(session_id, sizeof(session_id), "%s",
             s_snapshot.music_session_id);
    taskEXIT_CRITICAL(&s_snapshot_lock);
    if (session_id[0] == '\0')
    {
        return ESP_ERR_INVALID_STATE;
    }
    char command_id[64];
    music_service_make_command_id(command_id, sizeof(command_id));
    return music_http_client_session_command(
        &s_control_client, &config, session_id, action,
        strcmp(action, "mode") == 0 ? music_service_mode_text(mode) : NULL,
        command_id, out_result);
}

static void music_service_handle_start(const music_service_command_t *command)
{
    esp_err_t ret = music_service_stop_player();
    if (ret != ESP_OK)
    {
        music_service_set_error("music_stop_timeout");
        return;
    }
    music_http_client_config_t config;
    ret = music_service_copy_http_config(&config);
    if (ret != ESP_OK)
    {
        music_service_set_error("music_endpoint_unconfigured");
        return;
    }
    char command_id[64];
    music_service_make_command_id(command_id, sizeof(command_id));
    music_http_session_result_t result = {0};
    ret = music_http_client_create_session(
        &s_control_client, &config, command->source_id, command->track_id,
        command_id, &result);
    if (ret != ESP_OK)
    {
        music_service_set_error(result.error_code[0] != '\0'
                                    ? result.error_code
                                    : "music_session_create_failed");
        return;
    }
    music_service_copy_result(&result);
    taskENTER_CRITICAL(&s_snapshot_lock);
    snprintf(s_snapshot.source_id, sizeof(s_snapshot.source_id), "%s",
             command->source_id);
    snprintf(s_snapshot.track_id, sizeof(s_snapshot.track_id), "%s",
             command->track_id);
    taskEXIT_CRITICAL(&s_snapshot_lock);
    ret = music_service_start_player(&config, &result);
    if (ret != ESP_OK)
    {
        music_service_set_error("music_player_start_failed");
    }
}

static void music_service_handle_pause(void)
{
    music_service_snapshot_t snapshot = {0};
    (void)music_service_get_snapshot(&snapshot);
    if (snapshot.state != MUSIC_SERVICE_STATE_PLAYING &&
        snapshot.state != MUSIC_SERVICE_STATE_BUFFERING)
    {
        return;
    }
    esp_err_t ret = music_service_stop_player();
    if (ret != ESP_OK)
    {
        music_service_set_error("music_stop_timeout");
        return;
    }
    music_http_session_result_t result = {0};
    ret = music_service_server_command("pause", MUSIC_SERVICE_MODE_REPEAT_ALL,
                                       &result);
    if (ret != ESP_OK)
    {
        music_service_set_error("music_pause_failed");
        return;
    }
    music_service_copy_result(&result);
}

static void music_service_handle_resume(void)
{
    music_http_client_config_t config;
    esp_err_t ret = music_service_copy_http_config(&config);
    if (ret != ESP_OK)
    {
        music_service_set_error("music_endpoint_unconfigured");
        return;
    }
    music_http_session_result_t result = {0};
    ret = music_service_server_command("resume", MUSIC_SERVICE_MODE_REPEAT_ALL,
                                       &result);
    if (ret != ESP_OK)
    {
        music_service_set_error("music_resume_failed");
        return;
    }
    music_service_copy_result(&result);
    ret = music_service_start_player(&config, &result);
    if (ret != ESP_OK)
    {
        music_service_set_error("music_player_start_failed");
    }
}

static void music_service_handle_track_action(const char *action)
{
    esp_err_t ret = music_service_stop_player();
    if (ret != ESP_OK)
    {
        music_service_set_error("music_stop_timeout");
        return;
    }
    music_http_client_config_t config;
    ret = music_service_copy_http_config(&config);
    if (ret != ESP_OK)
    {
        music_service_set_error("music_endpoint_unconfigured");
        return;
    }
    music_http_session_result_t result = {0};
    ret = music_service_server_command(action, MUSIC_SERVICE_MODE_REPEAT_ALL,
                                       &result);
    if (ret != ESP_OK)
    {
        music_service_set_error("music_track_action_failed");
        return;
    }
    music_service_copy_result(&result);
    ret = music_service_start_player(&config, &result);
    if (ret != ESP_OK)
    {
        music_service_set_error("music_player_start_failed");
    }
}

static void music_service_handle_mode(music_service_mode_t mode)
{
    music_http_session_result_t result = {0};
    const esp_err_t ret = music_service_server_command("mode", mode, &result);
    if (ret != ESP_OK)
    {
        music_service_set_error("music_mode_failed");
        return;
    }
    music_service_copy_result(&result);
}

static void music_service_handle_load_source(
    const music_service_command_t *command)
{
    music_http_client_config_t config;
    music_service_catalog_snapshot_t result = {0};
    esp_err_t ret = music_service_copy_http_config(&config);
    if (ret == ESP_OK)
    {
        ret = music_http_client_fetch_tracks(
            &s_control_client, &config, command->source_id, command->offset,
            &result);
    }
    taskENTER_CRITICAL(&s_snapshot_lock);
    if (s_catalog != NULL)
    {
        if (ret == ESP_OK)
        {
            result.loading = false;
            result.generation = s_catalog->generation + 1U;
            *s_catalog = result;
        }
        else
        {
            s_catalog->loading = false;
        }
    }
    taskEXIT_CRITICAL(&s_snapshot_lock);
}

static void music_service_handle_qr_start(void)
{
    music_http_client_config_t config;
    if (music_service_copy_http_config(&config) != ESP_OK)
    {
        music_service_set_account_error("music_endpoint_unconfigured");
        return;
    }
    music_http_account_result_t result = {
        .qr_data = s_qr_data,
        .qr_capacity = MUSIC_SERVICE_QR_MAX_BYTES,
    };
    const esp_err_t ret = music_http_client_create_qr(&config, &result);
    if (ret != ESP_OK)
    {
        music_service_set_account_error(result.error_code[0] != '\0'
                                            ? result.error_code
                                            : "music_qr_create_failed");
        return;
    }
    music_service_apply_account_result(&result);
    s_qr_polling = result.state == MUSIC_SERVICE_ACCOUNT_QR_PENDING ||
                   result.state == MUSIC_SERVICE_ACCOUNT_QR_CONFIRMING;
    s_qr_next_poll = xTaskGetTickCount() + pdMS_TO_TICKS(kQrPollIntervalMs);
}

static void music_service_handle_qr_cancel(void)
{
    s_qr_polling = false;
}

static void music_service_poll_qr_if_due(void)
{
    if (!s_qr_polling || xTaskGetTickCount() < s_qr_next_poll)
    {
        return;
    }
    music_http_client_config_t config;
    if (music_service_copy_http_config(&config) != ESP_OK)
    {
        music_service_set_account_error("music_endpoint_unconfigured");
        s_qr_polling = false;
        return;
    }
    char login_id[MUSIC_SERVICE_QR_LOGIN_ID_MAX_BYTES] = {0};
    taskENTER_CRITICAL(&s_snapshot_lock);
    if (s_account != NULL)
    {
        snprintf(login_id, sizeof(login_id), "%s", s_account->login_id);
    }
    taskEXIT_CRITICAL(&s_snapshot_lock);
    music_http_account_result_t result = {
        .qr_data = s_qr_data,
        .qr_capacity = MUSIC_SERVICE_QR_MAX_BYTES,
    };
    const esp_err_t ret = music_http_client_poll_qr(&config, login_id, &result);
    if (ret != ESP_OK && result.state == MUSIC_SERVICE_ACCOUNT_UNKNOWN)
    {
        music_service_set_account_error("music_qr_poll_failed");
        s_qr_polling = false;
        return;
    }
    music_service_apply_account_result(&result);
    s_qr_polling = result.state == MUSIC_SERVICE_ACCOUNT_QR_PENDING ||
                   result.state == MUSIC_SERVICE_ACCOUNT_QR_CONFIRMING;
    s_qr_next_poll = xTaskGetTickCount() + pdMS_TO_TICKS(kQrPollIntervalMs);
}

static void music_service_handle_destroy(void)
{
    (void)music_service_stop_player();
    music_http_session_result_t result = {0};
    if (music_service_server_command("destroy", MUSIC_SERVICE_MODE_REPEAT_ALL,
                                    &result) != ESP_OK)
    {
        ESP_LOGW(TAG, "destroy request failed; local playback was stopped");
    }
    taskENTER_CRITICAL(&s_snapshot_lock);
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.state = MUSIC_SERVICE_STATE_STOPPED;
    s_snapshot.mode = MUSIC_SERVICE_MODE_REPEAT_ALL;
    taskEXIT_CRITICAL(&s_snapshot_lock);
    music_http_client_control_reset(&s_control_client);
}

static void music_service_task(void *arg)
{
    (void)arg;
    while (true)
    {
        music_service_process_player_events();
        music_service_poll_qr_if_due();
        music_service_command_t command;
        if (xQueueReceive(s_command_queue, &command, pdMS_TO_TICKS(100)) ==
            pdTRUE)
        {
            switch (command.type)
            {
            case MUSIC_SERVICE_CMD_START:
                music_service_handle_start(&command);
                break;
            case MUSIC_SERVICE_CMD_TOGGLE:
            {
                music_service_snapshot_t snapshot;
                (void)music_service_get_snapshot(&snapshot);
                if (snapshot.state == MUSIC_SERVICE_STATE_PLAYING ||
                    snapshot.state == MUSIC_SERVICE_STATE_BUFFERING)
                    music_service_handle_pause();
                else if (snapshot.state == MUSIC_SERVICE_STATE_PAUSED)
                    music_service_handle_resume();
                break;
            }
            case MUSIC_SERVICE_CMD_PAUSE:
                music_service_handle_pause();
                break;
            case MUSIC_SERVICE_CMD_RESUME:
                music_service_handle_resume();
                break;
            case MUSIC_SERVICE_CMD_PREVIOUS:
                music_service_handle_track_action("previous");
                break;
            case MUSIC_SERVICE_CMD_NEXT:
                music_service_handle_track_action("next");
                break;
            case MUSIC_SERVICE_CMD_MODE:
                music_service_handle_mode(command.mode);
                break;
            case MUSIC_SERVICE_CMD_DESTROY:
                music_service_handle_destroy();
                break;
            case MUSIC_SERVICE_CMD_LOAD_SOURCE:
                music_service_handle_load_source(&command);
                break;
            case MUSIC_SERVICE_CMD_QR_START:
                music_service_handle_qr_start();
                break;
            case MUSIC_SERVICE_CMD_QR_CANCEL:
                music_service_handle_qr_cancel();
                break;
            default:
                break;
            }
        }
    }
}

static esp_err_t music_service_enqueue(music_service_command_type_t type,
                                       const char *source_id,
                                       const char *track_id,
                                       music_service_mode_t mode,
                                       uint32_t offset)
{
    if (s_command_queue == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    music_service_command_t command = {
        .type = type,
        .mode = mode,
        .offset = offset,
    };
    if (source_id != NULL)
        snprintf(command.source_id, sizeof(command.source_id), "%s", source_id);
    if (track_id != NULL)
        snprintf(command.track_id, sizeof(command.track_id), "%s", track_id);
    return xQueueSend(s_command_queue, &command, 0U) == pdTRUE ? ESP_OK
                                                                : ESP_ERR_TIMEOUT;
}

esp_err_t music_service_init(void)
{
    if (s_task_handle != NULL)
    {
        return ESP_OK;
    }
    s_command_queue_storage = heap_caps_calloc(
        kCommandQueueLength, sizeof(music_service_command_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_player_event_queue_storage = heap_caps_calloc(
        kPlayerEventQueueLength, sizeof(music_stream_player_event_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_catalog = heap_caps_calloc(1U, sizeof(*s_catalog),
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_account = heap_caps_calloc(1U, sizeof(*s_account),
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_qr_data = heap_caps_calloc(1U, MUSIC_SERVICE_QR_MAX_BYTES,
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_command_queue_storage == NULL || s_player_event_queue_storage == NULL ||
        s_catalog == NULL || s_account == NULL || s_qr_data == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    s_command_queue = xQueueCreateStatic(
        kCommandQueueLength, sizeof(music_service_command_t),
        s_command_queue_storage, &s_command_queue_buffer);
    s_player_event_queue = xQueueCreateStatic(
        kPlayerEventQueueLength, sizeof(music_stream_player_event_t),
        s_player_event_queue_storage, &s_player_event_queue_buffer);
    if (s_command_queue == NULL || s_player_event_queue == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    const BaseType_t created = xTaskCreateWithCaps(
        music_service_task, "music_service", kTaskStackBytes, NULL, 4,
        &s_task_handle, MALLOC_CAP_SPIRAM);
    if (created != pdPASS)
    {
        s_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "music service ready");
    return ESP_OK;
}

esp_err_t music_service_start(const char *source_id, const char *track_id)
{
    if (source_id == NULL || source_id[0] == '\0')
    {
        return ESP_ERR_INVALID_ARG;
    }
    return music_service_enqueue(MUSIC_SERVICE_CMD_START, source_id, track_id,
                                 MUSIC_SERVICE_MODE_REPEAT_ALL, 0U);
}

esp_err_t music_service_load_source(const char *source_id)
{
    if (source_id == NULL || source_id[0] == '\0')
    {
        return ESP_ERR_INVALID_ARG;
    }
    music_service_catalog_set_loading(source_id, 0U);
    return music_service_enqueue(MUSIC_SERVICE_CMD_LOAD_SOURCE, source_id,
                                 NULL, MUSIC_SERVICE_MODE_REPEAT_ALL, 0U);
}

esp_err_t music_service_load_source_page(const char *source_id,
                                          uint32_t offset)
{
    if (source_id == NULL || source_id[0] == '\0' ||
        offset % MUSIC_SERVICE_CATALOG_PAGE_SIZE != 0U)
    {
        return ESP_ERR_INVALID_ARG;
    }
    music_service_catalog_set_loading(source_id, offset);
    return music_service_enqueue(MUSIC_SERVICE_CMD_LOAD_SOURCE, source_id,
                                 NULL, MUSIC_SERVICE_MODE_REPEAT_ALL, offset);
}

esp_err_t music_service_start_source(const char *source_id)
{
    return music_service_start(source_id, NULL);
}

esp_err_t music_service_toggle_playback(void)
{
    return music_service_enqueue(MUSIC_SERVICE_CMD_TOGGLE, NULL, NULL,
                                 MUSIC_SERVICE_MODE_REPEAT_ALL, 0U);
}

esp_err_t music_service_pause(void)
{
    return music_service_enqueue(MUSIC_SERVICE_CMD_PAUSE, NULL, NULL,
                                 MUSIC_SERVICE_MODE_REPEAT_ALL, 0U);
}

esp_err_t music_service_resume(void)
{
    return music_service_enqueue(MUSIC_SERVICE_CMD_RESUME, NULL, NULL,
                                 MUSIC_SERVICE_MODE_REPEAT_ALL, 0U);
}

esp_err_t music_service_previous(void)
{
    return music_service_enqueue(MUSIC_SERVICE_CMD_PREVIOUS, NULL, NULL,
                                 MUSIC_SERVICE_MODE_REPEAT_ALL, 0U);
}

esp_err_t music_service_next(void)
{
    return music_service_enqueue(MUSIC_SERVICE_CMD_NEXT, NULL, NULL,
                                 MUSIC_SERVICE_MODE_REPEAT_ALL, 0U);
}

esp_err_t music_service_set_mode(music_service_mode_t mode)
{
    if (mode < MUSIC_SERVICE_MODE_REPEAT_ONE || mode > MUSIC_SERVICE_MODE_SHUFFLE)
    {
        return ESP_ERR_INVALID_ARG;
    }
    return music_service_enqueue(MUSIC_SERVICE_CMD_MODE, NULL, NULL, mode, 0U);
}

esp_err_t music_service_pause_for_hermes_page(void)
{
    music_service_snapshot_t snapshot = {0};
    if (music_service_get_snapshot(&snapshot) != ESP_OK)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (snapshot.state != MUSIC_SERVICE_STATE_PLAYING &&
        snapshot.state != MUSIC_SERVICE_STATE_BUFFERING)
    {
        return ESP_OK;
    }
    return music_service_pause();
}

esp_err_t music_service_destroy(void)
{
    return music_service_enqueue(MUSIC_SERVICE_CMD_DESTROY, NULL, NULL,
                                 MUSIC_SERVICE_MODE_REPEAT_ALL, 0U);
}

esp_err_t music_service_get_snapshot(music_service_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    taskENTER_CRITICAL(&s_snapshot_lock);
    memcpy(out_snapshot, &s_snapshot, sizeof(*out_snapshot));
    taskEXIT_CRITICAL(&s_snapshot_lock);
    return ESP_OK;
}

esp_err_t music_service_get_catalog(
    music_service_catalog_snapshot_t *out_catalog)
{
    if (out_catalog == NULL || s_catalog == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    taskENTER_CRITICAL(&s_snapshot_lock);
    memcpy(out_catalog, s_catalog, sizeof(*out_catalog));
    taskEXIT_CRITICAL(&s_snapshot_lock);
    return ESP_OK;
}

esp_err_t music_service_start_qr_login(void)
{
    return music_service_enqueue(MUSIC_SERVICE_CMD_QR_START, NULL, NULL,
                                 MUSIC_SERVICE_MODE_REPEAT_ALL, 0U);
}

esp_err_t music_service_cancel_qr_login(void)
{
    return music_service_enqueue(MUSIC_SERVICE_CMD_QR_CANCEL, NULL, NULL,
                                 MUSIC_SERVICE_MODE_REPEAT_ALL, 0U);
}

esp_err_t music_service_get_account(
    music_service_account_snapshot_t *out_account)
{
    if (out_account == NULL || s_account == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    taskENTER_CRITICAL(&s_snapshot_lock);
    memcpy(out_account, s_account, sizeof(*out_account));
    taskEXIT_CRITICAL(&s_snapshot_lock);
    return ESP_OK;
}

esp_err_t music_service_copy_qr(uint8_t *out_data, size_t capacity,
                                uint16_t *out_size, size_t *out_bytes)
{
    if (out_data == NULL || out_size == NULL || out_bytes == NULL ||
        s_account == NULL || s_qr_data == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    taskENTER_CRITICAL(&s_snapshot_lock);
    if (s_account->qr_bytes == 0U || s_account->qr_bytes > capacity)
    {
        taskEXIT_CRITICAL(&s_snapshot_lock);
        return ESP_ERR_NOT_FOUND;
    }
    memcpy(out_data, s_qr_data, s_account->qr_bytes);
    *out_size = s_account->qr_size;
    *out_bytes = s_account->qr_bytes;
    taskEXIT_CRITICAL(&s_snapshot_lock);
    return ESP_OK;
}
