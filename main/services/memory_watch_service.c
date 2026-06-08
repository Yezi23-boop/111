#include "services/memory_watch_service.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_random.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "network_service.h"
#include "services/memory_watch_voice_client.h"

static const char *TAG = "memory_watch";
static const UBaseType_t kCommandQueueLength = 8;
static const uint32_t kTaskStackWords = 2048;

typedef enum
{
    MEMORY_WATCH_SERVICE_CMD_BEGIN_RECORDING = 0,
    MEMORY_WATCH_SERVICE_CMD_CHECK_HEALTH,
    MEMORY_WATCH_SERVICE_CMD_SEND_RECORDING,
    MEMORY_WATCH_SERVICE_CMD_CANCEL_RECORDING,
    MEMORY_WATCH_SERVICE_CMD_CANCEL_WAITING,
    MEMORY_WATCH_SERVICE_CMD_CANCEL_CLARIFICATION,
} memory_watch_service_cmd_type_t;

typedef struct
{
    memory_watch_service_cmd_type_t type;
} memory_watch_service_cmd_t;

typedef struct
{
    bool configured;
    char base_url[MEMORY_WATCH_SERVICE_URL_MAX_BYTES];
    char device_id[MEMORY_WATCH_SERVICE_DEVICE_ID_MAX_BYTES];
    char device_token[MEMORY_WATCH_SERVICE_DEVICE_TOKEN_MAX_BYTES];
    uint32_t timeout_ms;
    bool allow_insecure_http;
} memory_watch_service_endpoint_state_t;

typedef struct
{
    memory_watch_voice_client_config_t client_config;
    char base_url[MEMORY_WATCH_SERVICE_URL_MAX_BYTES];
    char device_id[MEMORY_WATCH_SERVICE_DEVICE_ID_MAX_BYTES];
    char device_token[MEMORY_WATCH_SERVICE_DEVICE_TOKEN_MAX_BYTES];
} memory_watch_service_client_config_snapshot_t;

static TaskHandle_t s_service_task_handle = NULL;
static QueueHandle_t s_command_queue = NULL;
static StaticQueue_t s_command_queue_buffer;
static uint8_t s_command_queue_storage[8 * sizeof(memory_watch_service_cmd_t)];
static StaticTask_t s_service_task_buffer;
static StackType_t s_service_task_stack[2048];
static portMUX_TYPE s_snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_endpoint_lock = portMUX_INITIALIZER_UNLOCKED;
static memory_watch_service_snapshot_t s_snapshot = {
    .state = MEMORY_WATCH_SERVICE_STATE_READY,
    .network_ready = false,
    .endpoint_configured = false,
    .hermes_online = false,
    .request_active = false,
    .clarification_active = false,
    .last_error = ESP_OK,
};
static memory_watch_service_endpoint_state_t s_endpoint_config = {0};
static uint32_t s_boot_id = 0;
static uint32_t s_request_seq = 0;

/**
 * @brief 复制当前快照。
 *
 * snapshot 是 UI 与 owner task 共享的低频状态，因此用极短 critical
 * section 复制；getter 不做 I/O，也不推进录音或上传状态。
 */
static memory_watch_service_snapshot_t memory_watch_service_copy_snapshot(void)
{
    memory_watch_service_snapshot_t snapshot;

    portENTER_CRITICAL(&s_snapshot_lock);
    snapshot = s_snapshot;
    portEXIT_CRITICAL(&s_snapshot_lock);

    return snapshot;
}

static void memory_watch_service_set_state(
    memory_watch_service_state_t state, esp_err_t last_error)
{
    portENTER_CRITICAL(&s_snapshot_lock);
    s_snapshot.state = state;
    s_snapshot.last_error = last_error;
    portEXIT_CRITICAL(&s_snapshot_lock);
}

static void memory_watch_service_set_request_active(bool active)
{
    portENTER_CRITICAL(&s_snapshot_lock);
    s_snapshot.request_active = active;
    portEXIT_CRITICAL(&s_snapshot_lock);
}

static void memory_watch_service_set_network_ready(bool ready)
{
    portENTER_CRITICAL(&s_snapshot_lock);
    s_snapshot.network_ready = ready;
    portEXIT_CRITICAL(&s_snapshot_lock);
}

static void memory_watch_service_set_endpoint_snapshot(bool configured,
                                                       bool hermes_online)
{
    portENTER_CRITICAL(&s_snapshot_lock);
    s_snapshot.endpoint_configured = configured;
    s_snapshot.hermes_online = hermes_online;
    portEXIT_CRITICAL(&s_snapshot_lock);
}

static void memory_watch_service_copy_text(char *dst, size_t dst_len,
                                           const char *src)
{
    if (dst == NULL || dst_len == 0U)
    {
        return;
    }

    if (src == NULL)
    {
        dst[0] = '\0';
        return;
    }

    size_t index = 0;
    while (index + 1U < dst_len && src[index] != '\0')
    {
        dst[index] = src[index];
        ++index;
    }
    dst[index] = '\0';
}

static esp_err_t memory_watch_service_copy_required_text(char *dst,
                                                         size_t dst_len,
                                                         const char *src)
{
    if (dst == NULL || dst_len == 0U || src == NULL || src[0] == '\0')
    {
        return ESP_ERR_INVALID_ARG;
    }

    size_t len = 0;
    while (src[len] != '\0' && len < dst_len)
    {
        ++len;
    }
    if (len == 0U)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (len >= dst_len)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(dst, src, len + 1U);
    return ESP_OK;
}

static void memory_watch_service_init_boot_id(void)
{
    if (s_boot_id != 0U)
    {
        return;
    }

    s_boot_id = esp_random();
    if (s_boot_id == 0U)
    {
        s_boot_id = 1U;
    }
}

static esp_err_t memory_watch_service_make_request_id(const char *device_id,
                                                      char *out_request_id,
                                                      size_t out_len)
{
    if (device_id == NULL || device_id[0] == '\0' ||
        out_request_id == NULL || out_len == 0U)
    {
        return ESP_ERR_INVALID_ARG;
    }

    const uint32_t seq = ++s_request_seq;
    const int written = snprintf(out_request_id, out_len,
                                 "%s-%08" PRIx32 "-%04" PRIu32,
                                 device_id, s_boot_id, seq);
    if (written < 0 || (size_t)written >= out_len)
    {
        out_request_id[0] = '\0';
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static esp_err_t memory_watch_service_copy_client_config(
    memory_watch_service_client_config_snapshot_t *out_config)
{
    if (out_config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    memory_watch_service_endpoint_state_t endpoint_config;
    portENTER_CRITICAL(&s_endpoint_lock);
    endpoint_config = s_endpoint_config;
    portEXIT_CRITICAL(&s_endpoint_lock);

    if (!endpoint_config.configured)
    {
        return ESP_ERR_INVALID_STATE;
    }

    memset(out_config, 0, sizeof(*out_config));
    memory_watch_service_copy_text(out_config->base_url,
                                   sizeof(out_config->base_url),
                                   endpoint_config.base_url);
    memory_watch_service_copy_text(out_config->device_id,
                                   sizeof(out_config->device_id),
                                   endpoint_config.device_id);
    memory_watch_service_copy_text(out_config->device_token,
                                   sizeof(out_config->device_token),
                                   endpoint_config.device_token);

    out_config->client_config.base_url = out_config->base_url;
    out_config->client_config.device_id = out_config->device_id;
    out_config->client_config.device_token = out_config->device_token;
    out_config->client_config.timeout_ms = endpoint_config.timeout_ms;
    out_config->client_config.allow_insecure_http =
        endpoint_config.allow_insecure_http;
    return ESP_OK;
}

static void memory_watch_service_set_request_id(const char *request_id)
{
    portENTER_CRITICAL(&s_snapshot_lock);
    memory_watch_service_copy_text(s_snapshot.request_id,
                                   sizeof(s_snapshot.request_id),
                                   request_id);
    s_snapshot.asr_text[0] = '\0';
    s_snapshot.reply_text[0] = '\0';
    portEXIT_CRITICAL(&s_snapshot_lock);
}

static void memory_watch_service_clear_clarification(void)
{
    portENTER_CRITICAL(&s_snapshot_lock);
    s_snapshot.clarification_active = false;
    s_snapshot.clarification_id[0] = '\0';
    portEXIT_CRITICAL(&s_snapshot_lock);
}

static bool memory_watch_service_can_begin_from_state(
    memory_watch_service_state_t state)
{
    switch (state)
    {
    case MEMORY_WATCH_SERVICE_STATE_READY:
    case MEMORY_WATCH_SERVICE_STATE_DONE:
    case MEMORY_WATCH_SERVICE_STATE_TIMEOUT:
    case MEMORY_WATCH_SERVICE_STATE_ERROR:
    case MEMORY_WATCH_SERVICE_STATE_CANCELED:
        return true;
    case MEMORY_WATCH_SERVICE_STATE_NEEDS_CLARIFICATION:
    case MEMORY_WATCH_SERVICE_STATE_WAITING_NETWORK:
    case MEMORY_WATCH_SERVICE_STATE_RECORDING:
    case MEMORY_WATCH_SERVICE_STATE_ENCODING:
    case MEMORY_WATCH_SERVICE_STATE_UPLOADING:
    case MEMORY_WATCH_SERVICE_STATE_THINKING:
    default:
        return false;
    }
}

static void memory_watch_service_handle_begin_recording(void)
{
    const memory_watch_service_snapshot_t before =
        memory_watch_service_copy_snapshot();
    if (before.request_active ||
        !memory_watch_service_can_begin_from_state(before.state))
    {
        memory_watch_service_set_state(before.state, ESP_ERR_INVALID_STATE);
        ESP_LOGW(TAG, "recording blocked: request already active state=%s",
                 memory_watch_service_state_to_string(before.state));
        return;
    }

    memory_watch_service_client_config_snapshot_t client_config;
    esp_err_t err = memory_watch_service_copy_client_config(&client_config);
    if (err != ESP_OK)
    {
        memory_watch_service_set_endpoint_snapshot(false, false);
        memory_watch_service_set_request_active(false);
        memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_ERROR, err);
        ESP_LOGW(TAG, "recording blocked: watch endpoint is not configured");
        return;
    }

    const bool network_ready = network_service_is_service_ready();
    memory_watch_service_set_network_ready(network_ready);
    if (!network_ready)
    {
        memory_watch_service_set_state(
            MEMORY_WATCH_SERVICE_STATE_WAITING_NETWORK,
            ESP_ERR_INVALID_STATE);
        memory_watch_service_set_request_active(false);
        ESP_LOGW(TAG, "recording blocked: network service is not ready");
        return;
    }

    char request_id[MEMORY_WATCH_SERVICE_ID_MAX_BYTES];
    err = memory_watch_service_make_request_id(
        client_config.client_config.device_id, request_id,
        sizeof(request_id));
    if (err != ESP_OK)
    {
        memory_watch_service_set_request_active(false);
        memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_ERROR, err);
        return;
    }

    memory_watch_service_set_request_id(request_id);
    memory_watch_service_set_request_active(true);
    memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_RECORDING,
                                   ESP_OK);
}

static void memory_watch_service_handle_check_health(void)
{
    const memory_watch_service_snapshot_t before =
        memory_watch_service_copy_snapshot();
    if (before.request_active)
    {
        memory_watch_service_set_state(before.state, ESP_ERR_INVALID_STATE);
        return;
    }

    const bool network_ready = network_service_is_service_ready();
    memory_watch_service_set_network_ready(network_ready);
    if (!network_ready)
    {
        memory_watch_service_set_endpoint_snapshot(before.endpoint_configured,
                                                   false);
        memory_watch_service_set_state(
            MEMORY_WATCH_SERVICE_STATE_WAITING_NETWORK,
            ESP_ERR_INVALID_STATE);
        return;
    }

    memory_watch_service_client_config_snapshot_t client_config;
    esp_err_t err = memory_watch_service_copy_client_config(&client_config);
    if (err != ESP_OK)
    {
        memory_watch_service_set_endpoint_snapshot(false, false);
        memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_ERROR, err);
        return;
    }

    /*
     * 页面进入时的 health 只判断 watch endpoint / Hermes 是否在线，不使用
     * 语音请求 120 秒预算，避免 owner task 因短健康检查长时间不可响应。
     */
    client_config.client_config.timeout_ms =
        MEMORY_WATCH_SERVICE_HEALTH_TIMEOUT_MS;
    memory_watch_voice_client_health_t health = {0};
    err = memory_watch_voice_client_get_health(
        &client_config.client_config, &health);
    if (err == ESP_OK)
    {
        memory_watch_service_set_endpoint_snapshot(true, true);
        memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_READY,
                                       ESP_OK);
        return;
    }

    memory_watch_service_set_endpoint_snapshot(true, false);
    memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_ERROR, err);
}

static void memory_watch_service_handle_send_recording(void)
{
    const memory_watch_service_snapshot_t before =
        memory_watch_service_copy_snapshot();
    if (before.state != MEMORY_WATCH_SERVICE_STATE_RECORDING)
    {
        memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_ERROR,
                                       ESP_ERR_INVALID_STATE);
        return;
    }

    /*
     * V1 skeleton 先固定 owner 状态推进位置。真实 Ogg Opus 编码、
     * audio_codec input session 和 HTTP multipart 上传会在后续窄实现中接入。
     */
    memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_ENCODING,
                                   ESP_OK);
    memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_UPLOADING,
                                   ESP_OK);
    memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_THINKING,
                                   ESP_OK);
}

static void memory_watch_service_handle_cancel(void)
{
    memory_watch_service_set_request_active(false);
    memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_CANCELED,
                                   ESP_OK);
}

static void memory_watch_service_handle_command(
    const memory_watch_service_cmd_t *command)
{
    if (command == NULL)
    {
        return;
    }

    switch (command->type)
    {
    case MEMORY_WATCH_SERVICE_CMD_BEGIN_RECORDING:
        memory_watch_service_handle_begin_recording();
        break;
    case MEMORY_WATCH_SERVICE_CMD_CHECK_HEALTH:
        memory_watch_service_handle_check_health();
        break;
    case MEMORY_WATCH_SERVICE_CMD_SEND_RECORDING:
        memory_watch_service_handle_send_recording();
        break;
    case MEMORY_WATCH_SERVICE_CMD_CANCEL_RECORDING:
    case MEMORY_WATCH_SERVICE_CMD_CANCEL_WAITING:
        memory_watch_service_handle_cancel();
        break;
    case MEMORY_WATCH_SERVICE_CMD_CANCEL_CLARIFICATION:
        memory_watch_service_clear_clarification();
        memory_watch_service_set_request_active(false);
        memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_READY,
                                       ESP_OK);
        break;
    default:
        break;
    }
}

static void memory_watch_service_task(void *arg)
{
    (void)arg;

    while (1)
    {
        memory_watch_service_cmd_t command;
        if (xQueueReceive(s_command_queue, &command, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }
        memory_watch_service_handle_command(&command);
    }
}

static esp_err_t memory_watch_service_post_command(
    memory_watch_service_cmd_type_t type)
{
    if (s_command_queue == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    memory_watch_service_cmd_t command = {
        .type = type,
    };
    if (xQueueSend(s_command_queue, &command, 0) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t memory_watch_service_init(void)
{
    if (s_service_task_handle != NULL)
    {
        return ESP_OK;
    }

    memory_watch_service_init_boot_id();

    if (s_command_queue == NULL)
    {
        s_command_queue = xQueueCreateStatic(
            kCommandQueueLength,
            sizeof(memory_watch_service_cmd_t),
            s_command_queue_storage,
            &s_command_queue_buffer);
    }
    if (s_command_queue == NULL)
    {
        memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_ERROR,
                                       ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }

    s_service_task_handle = xTaskCreateStatic(
        memory_watch_service_task,
        "memory_watch",
        kTaskStackWords,
        NULL,
        4,
        s_service_task_stack,
        &s_service_task_buffer);
    if (s_service_task_handle == NULL)
    {
        memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_ERROR,
                                       ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t memory_watch_service_configure_endpoint(
    const memory_watch_service_endpoint_config_t *config)
{
    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    const memory_watch_service_snapshot_t snapshot =
        memory_watch_service_copy_snapshot();
    if (snapshot.request_active)
    {
        return ESP_ERR_INVALID_STATE;
    }

    memory_watch_service_endpoint_state_t next_config = {0};
    esp_err_t err = memory_watch_service_copy_required_text(
        next_config.base_url, sizeof(next_config.base_url), config->base_url);
    if (err != ESP_OK)
    {
        return err;
    }
    err = memory_watch_service_copy_required_text(
        next_config.device_id, sizeof(next_config.device_id),
        config->device_id);
    if (err != ESP_OK)
    {
        return err;
    }
    err = memory_watch_service_copy_required_text(
        next_config.device_token, sizeof(next_config.device_token),
        config->device_token);
    if (err != ESP_OK)
    {
        return err;
    }
    next_config.timeout_ms = config->timeout_ms;
    next_config.allow_insecure_http = config->allow_insecure_http;
    next_config.configured = true;

    portENTER_CRITICAL(&s_endpoint_lock);
    s_endpoint_config = next_config;
    portEXIT_CRITICAL(&s_endpoint_lock);

    memory_watch_service_set_endpoint_snapshot(true, false);
    memory_watch_service_set_state(snapshot.state, ESP_OK);
    return ESP_OK;
}

esp_err_t memory_watch_service_check_health(void)
{
    return memory_watch_service_post_command(
        MEMORY_WATCH_SERVICE_CMD_CHECK_HEALTH);
}

esp_err_t memory_watch_service_begin_recording(void)
{
    return memory_watch_service_post_command(
        MEMORY_WATCH_SERVICE_CMD_BEGIN_RECORDING);
}

esp_err_t memory_watch_service_send_recording(void)
{
    return memory_watch_service_post_command(
        MEMORY_WATCH_SERVICE_CMD_SEND_RECORDING);
}

esp_err_t memory_watch_service_cancel_recording(void)
{
    return memory_watch_service_post_command(
        MEMORY_WATCH_SERVICE_CMD_CANCEL_RECORDING);
}

esp_err_t memory_watch_service_cancel_waiting(void)
{
    return memory_watch_service_post_command(
        MEMORY_WATCH_SERVICE_CMD_CANCEL_WAITING);
}

esp_err_t memory_watch_service_cancel_clarification(void)
{
    return memory_watch_service_post_command(
        MEMORY_WATCH_SERVICE_CMD_CANCEL_CLARIFICATION);
}

esp_err_t memory_watch_service_get_snapshot(
    memory_watch_service_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    *out_snapshot = memory_watch_service_copy_snapshot();
    return ESP_OK;
}

const char *memory_watch_service_state_to_string(
    memory_watch_service_state_t state)
{
    switch (state)
    {
    case MEMORY_WATCH_SERVICE_STATE_READY:
        return "ready";
    case MEMORY_WATCH_SERVICE_STATE_WAITING_NETWORK:
        return "waiting_network";
    case MEMORY_WATCH_SERVICE_STATE_RECORDING:
        return "recording";
    case MEMORY_WATCH_SERVICE_STATE_ENCODING:
        return "encoding";
    case MEMORY_WATCH_SERVICE_STATE_UPLOADING:
        return "uploading";
    case MEMORY_WATCH_SERVICE_STATE_THINKING:
        return "thinking";
    case MEMORY_WATCH_SERVICE_STATE_NEEDS_CLARIFICATION:
        return "needs_clarification";
    case MEMORY_WATCH_SERVICE_STATE_DONE:
        return "done";
    case MEMORY_WATCH_SERVICE_STATE_TIMEOUT:
        return "timeout";
    case MEMORY_WATCH_SERVICE_STATE_ERROR:
        return "error";
    case MEMORY_WATCH_SERVICE_STATE_CANCELED:
        return "canceled";
    default:
        return "unknown";
    }
}
