#include "services/memory_watch_service.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "network_service.h"
#include "nvs.h"
#include "power_policy.h"
#include "services/memory_watch_recorder.h"
#include "services/memory_watch_voice_client.h"

static const char *TAG = "memory_watch";
static const UBaseType_t kCommandQueueLength = 8;
static const UBaseType_t kWorkerQueueLength = 1;
static const UBaseType_t kCancelQueueLength = 1;
static const UBaseType_t kHealthQueueLength = 1;
static const uint32_t kTaskStackWords = 2048;
static const uint32_t kUploadWorkerStackWords = 6144;
static const uint32_t kCancelWorkerStackWords = 3072;
static const uint32_t kHealthWorkerStackWords = 3072;
static const size_t kAudioBufferInitialBytes = 8192U;
static const char *kEndpointNvsNamespace = "memory_watch";
static const char *kEndpointNvsBaseUrlKey = "base_url";
static const char *kEndpointNvsDeviceIdKey = "device_id";
static const char *kEndpointNvsDeviceTokenKey = "device_token";
static const char *kEndpointNvsTimeoutMsKey = "timeout_ms";
static const char *kEndpointNvsAllowHttpKey = "allow_http";

typedef enum
{
    MEMORY_WATCH_SERVICE_CMD_BEGIN_RECORDING = 0,
    MEMORY_WATCH_SERVICE_CMD_CHECK_HEALTH,
    MEMORY_WATCH_SERVICE_CMD_SEND_RECORDING,
    MEMORY_WATCH_SERVICE_CMD_CANCEL_RECORDING,
    MEMORY_WATCH_SERVICE_CMD_CANCEL_WAITING,
    MEMORY_WATCH_SERVICE_CMD_CANCEL_CLARIFICATION,
    MEMORY_WATCH_SERVICE_CMD_HEALTH_DONE,
    MEMORY_WATCH_SERVICE_CMD_WORKER_UPLOAD_STARTED,
    MEMORY_WATCH_SERVICE_CMD_WORKER_DONE,
} memory_watch_service_cmd_type_t;

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

typedef struct
{
    memory_watch_service_client_config_snapshot_t client_config;
    char request_id[MEMORY_WATCH_SERVICE_ID_MAX_BYTES];
    char clarification_id[MEMORY_WATCH_SERVICE_ID_MAX_BYTES];
} memory_watch_service_upload_job_t;

typedef struct
{
    memory_watch_service_client_config_snapshot_t client_config;
    char request_id[MEMORY_WATCH_SERVICE_ID_MAX_BYTES];
} memory_watch_service_cancel_job_t;

typedef struct
{
    memory_watch_service_client_config_snapshot_t client_config;
} memory_watch_service_health_job_t;

typedef struct
{
    esp_err_t error;
    bool hermes_online;
} memory_watch_service_health_result_t;

typedef struct
{
    char request_id[MEMORY_WATCH_SERVICE_ID_MAX_BYTES];
    esp_err_t error;
    bool has_response;
    bool cancel_requested;
    memory_watch_voice_client_response_t response;
} memory_watch_service_worker_result_t;

typedef struct
{
    uint8_t *data;
    size_t len;
    size_t capacity;
} memory_watch_service_audio_buffer_t;

typedef struct
{
    memory_watch_service_cmd_type_t type;
    memory_watch_service_health_result_t health_result;
    memory_watch_service_worker_result_t worker_result;
    char request_id[MEMORY_WATCH_SERVICE_ID_MAX_BYTES];
} memory_watch_service_cmd_t;

static TaskHandle_t s_service_task_handle = NULL;
static TaskHandle_t s_upload_worker_task_handle = NULL;
static TaskHandle_t s_cancel_worker_task_handle = NULL;
static TaskHandle_t s_health_worker_task_handle = NULL;
static QueueHandle_t s_command_queue = NULL;
static QueueHandle_t s_upload_worker_queue = NULL;
static QueueHandle_t s_cancel_worker_queue = NULL;
static QueueHandle_t s_health_worker_queue = NULL;
static StaticQueue_t s_command_queue_buffer;
static StaticQueue_t s_upload_worker_queue_buffer;
static StaticQueue_t s_cancel_worker_queue_buffer;
static StaticQueue_t s_health_worker_queue_buffer;
static uint8_t s_command_queue_storage[
    8 * sizeof(memory_watch_service_cmd_t)];
static uint8_t s_upload_worker_queue_storage[
    1 * sizeof(memory_watch_service_upload_job_t)];
static uint8_t s_cancel_worker_queue_storage[
    1 * sizeof(memory_watch_service_cancel_job_t)];
static uint8_t s_health_worker_queue_storage[
    1 * sizeof(memory_watch_service_health_job_t)];
static StaticTask_t s_service_task_buffer;
static StaticTask_t s_upload_worker_task_buffer;
static StaticTask_t s_cancel_worker_task_buffer;
static StaticTask_t s_health_worker_task_buffer;
static StackType_t s_service_task_stack[2048];
static StackType_t s_upload_worker_task_stack[6144];
static StackType_t s_cancel_worker_task_stack[3072];
static StackType_t s_health_worker_task_stack[3072];
static portMUX_TYPE s_snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_endpoint_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_worker_lock = portMUX_INITIALIZER_UNLOCKED;
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
static bool s_record_stop_requested = false;
static bool s_record_discard_requested = false;
static bool s_wait_cancel_requested = false;
static bool s_upload_worker_busy = false;
static char s_wait_canceled_request_id[MEMORY_WATCH_SERVICE_ID_MAX_BYTES];

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

static bool memory_watch_service_is_safe_endpoint_text(const char *text)
{
    if (text == NULL || text[0] == '\0')
    {
        return false;
    }

    for (const char *p = text; *p != '\0'; ++p)
    {
        if (*p == '\r' || *p == '\n')
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief 校验所有 endpoint 配置入口共同遵守的 URL 与文本边界。
 *
 * 配置可能来自 SoftAP、NVS 或后续调试入口，因此校验放在 service owner 层，避免
 * 某个入口漏检后把非法地址标记为已配置。
 */
static esp_err_t memory_watch_service_validate_endpoint_state(
    const memory_watch_service_endpoint_state_t *state)
{
    if (state == NULL ||
        !memory_watch_service_is_safe_endpoint_text(state->base_url) ||
        !memory_watch_service_is_safe_endpoint_text(state->device_id) ||
        !memory_watch_service_is_safe_endpoint_text(state->device_token))
    {
        return ESP_ERR_INVALID_ARG;
    }

    const bool uses_http = strncmp(state->base_url, "http://", 7) == 0;
    const bool uses_https = strncmp(state->base_url, "https://", 8) == 0;
    if (!uses_http && !uses_https)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (uses_http && !state->allow_insecure_http)
    {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t memory_watch_service_build_endpoint_state(
    const memory_watch_service_endpoint_config_t *config,
    memory_watch_service_endpoint_state_t *out_state)
{
    if (config == NULL || out_state == NULL)
    {
        return ESP_ERR_INVALID_ARG;
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

    err = memory_watch_service_validate_endpoint_state(&next_config);
    if (err != ESP_OK)
    {
        return err;
    }

    *out_state = next_config;
    return ESP_OK;
}

static esp_err_t memory_watch_service_read_nvs_text(nvs_handle_t handle,
                                                    const char *key,
                                                    char *dst,
                                                    size_t dst_len)
{
    if (key == NULL || dst == NULL || dst_len == 0U)
    {
        return ESP_ERR_INVALID_ARG;
    }

    size_t len = dst_len;
    const esp_err_t err = nvs_get_str(handle, key, dst, &len);
    if (err != ESP_OK)
    {
        dst[0] = '\0';
        return err;
    }
    if (len == 0U || dst[0] == '\0')
    {
        dst[0] = '\0';
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

/**
 * @brief 从 NVS 读取 watch endpoint 运行期配置。
 *
 * 这里仅读取 ESP32-S3 到 voice endpoint 的 device token，不读取也不保存
 * Hermes / MiMo / API Server key。缺失配置只让页面保持“未配置”，不阻塞启动。
 */
static esp_err_t memory_watch_service_load_endpoint_from_nvs(void)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kEndpointNvsNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        memory_watch_service_set_endpoint_snapshot(false, false);
        ESP_LOGI(TAG, "watch endpoint NVS config not found");
        return ESP_OK;
    }
    if (err != ESP_OK)
    {
        memory_watch_service_set_endpoint_snapshot(false, false);
        return err;
    }

    char base_url[MEMORY_WATCH_SERVICE_URL_MAX_BYTES] = {0};
    char device_id[MEMORY_WATCH_SERVICE_DEVICE_ID_MAX_BYTES] = {0};
    char device_token[MEMORY_WATCH_SERVICE_DEVICE_TOKEN_MAX_BYTES] = {0};

    err = memory_watch_service_read_nvs_text(
        handle, kEndpointNvsBaseUrlKey, base_url, sizeof(base_url));
    if (err == ESP_OK)
    {
        err = memory_watch_service_read_nvs_text(
            handle, kEndpointNvsDeviceIdKey, device_id, sizeof(device_id));
    }
    if (err == ESP_OK)
    {
        err = memory_watch_service_read_nvs_text(
            handle, kEndpointNvsDeviceTokenKey, device_token,
            sizeof(device_token));
    }
    if (err != ESP_OK)
    {
        nvs_close(handle);
        memory_watch_service_set_endpoint_snapshot(false, false);
        ESP_LOGW(TAG, "watch endpoint NVS config incomplete: %s",
                 esp_err_to_name(err));
        return ESP_OK;
    }

    uint32_t timeout_ms = 0;
    err = nvs_get_u32(handle, kEndpointNvsTimeoutMsKey, &timeout_ms);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        nvs_close(handle);
        memory_watch_service_set_endpoint_snapshot(false, false);
        return err;
    }

    uint8_t allow_http = 0;
    err = nvs_get_u8(handle, kEndpointNvsAllowHttpKey, &allow_http);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        nvs_close(handle);
        memory_watch_service_set_endpoint_snapshot(false, false);
        return err;
    }
    nvs_close(handle);

    const memory_watch_service_endpoint_config_t config = {
        .base_url = base_url,
        .device_id = device_id,
        .device_token = device_token,
        .timeout_ms = timeout_ms,
        .allow_insecure_http = allow_http != 0U,
    };
    err = memory_watch_service_configure_endpoint(&config);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG,
                 "watch endpoint configured from NVS: device_id=%s allow_http=%u timeout_ms=%lu",
                 device_id, (unsigned int)(allow_http != 0U),
                 (unsigned long)timeout_ms);
    }
    return err;
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

static void memory_watch_service_copy_current_clarification(
    char *out_clarification_id, size_t out_len)
{
    if (out_clarification_id == NULL || out_len == 0U)
    {
        return;
    }

    portENTER_CRITICAL(&s_snapshot_lock);
    memory_watch_service_copy_text(out_clarification_id, out_len,
                                   s_snapshot.clarification_active
                                       ? s_snapshot.clarification_id
                                       : "");
    portEXIT_CRITICAL(&s_snapshot_lock);
}

static bool memory_watch_service_request_id_matches_current(
    const char *request_id)
{
    if (request_id == NULL)
    {
        return false;
    }

    bool matches = false;
    portENTER_CRITICAL(&s_snapshot_lock);
    matches = strcmp(s_snapshot.request_id, request_id) == 0;
    portEXIT_CRITICAL(&s_snapshot_lock);
    return matches;
}

static void memory_watch_service_set_response_texts(
    const memory_watch_voice_client_response_t *response)
{
    if (response == NULL)
    {
        return;
    }

    portENTER_CRITICAL(&s_snapshot_lock);
    memory_watch_service_copy_text(s_snapshot.asr_text,
                                   sizeof(s_snapshot.asr_text),
                                   response->asr_text);
    memory_watch_service_copy_text(s_snapshot.reply_text,
                                   sizeof(s_snapshot.reply_text),
                                   response->reply_text);
    memory_watch_service_copy_text(s_snapshot.clarification_id,
                                   sizeof(s_snapshot.clarification_id),
                                   response->clarification_id);
    s_snapshot.clarification_active = response->clarification_id[0] != '\0';
    portEXIT_CRITICAL(&s_snapshot_lock);
}

static void memory_watch_service_reset_worker_flags(void)
{
    portENTER_CRITICAL(&s_worker_lock);
    s_record_stop_requested = false;
    s_record_discard_requested = false;
    s_wait_cancel_requested = false;
    s_wait_canceled_request_id[0] = '\0';
    portEXIT_CRITICAL(&s_worker_lock);
}

static void memory_watch_service_request_record_stop(bool discard)
{
    portENTER_CRITICAL(&s_worker_lock);
    s_record_stop_requested = true;
    if (discard)
    {
        s_record_discard_requested = true;
    }
    portEXIT_CRITICAL(&s_worker_lock);
}

static void memory_watch_service_request_wait_cancel(const char *request_id)
{
    portENTER_CRITICAL(&s_worker_lock);
    s_wait_cancel_requested = true;
    memory_watch_service_copy_text(s_wait_canceled_request_id,
                                   sizeof(s_wait_canceled_request_id),
                                   request_id);
    portEXIT_CRITICAL(&s_worker_lock);
}

static bool memory_watch_service_should_stop_recording(void *user_ctx)
{
    (void)user_ctx;

    bool should_stop = false;
    portENTER_CRITICAL(&s_worker_lock);
    should_stop = s_record_stop_requested || s_record_discard_requested;
    portEXIT_CRITICAL(&s_worker_lock);
    return should_stop;
}

static bool memory_watch_service_is_record_discard_requested(void)
{
    bool discard = false;
    portENTER_CRITICAL(&s_worker_lock);
    discard = s_record_discard_requested;
    portEXIT_CRITICAL(&s_worker_lock);
    return discard;
}

static bool memory_watch_service_should_abort_recording(void *user_ctx)
{
    (void)user_ctx;
    return memory_watch_service_is_record_discard_requested();
}

static bool memory_watch_service_is_wait_cancel_requested(void)
{
    bool cancel_requested = false;
    portENTER_CRITICAL(&s_worker_lock);
    cancel_requested = s_wait_cancel_requested;
    portEXIT_CRITICAL(&s_worker_lock);
    return cancel_requested;
}

static bool memory_watch_service_is_wait_canceled_request(
    const char *request_id)
{
    if (request_id == NULL)
    {
        return false;
    }

    bool canceled = false;
    portENTER_CRITICAL(&s_worker_lock);
    canceled = s_wait_canceled_request_id[0] != '\0' &&
               strcmp(s_wait_canceled_request_id, request_id) == 0;
    portEXIT_CRITICAL(&s_worker_lock);
    return canceled;
}

static void memory_watch_service_set_upload_worker_busy(bool busy)
{
    portENTER_CRITICAL(&s_worker_lock);
    s_upload_worker_busy = busy;
    portEXIT_CRITICAL(&s_worker_lock);
}

static bool memory_watch_service_is_upload_worker_busy(void)
{
    bool busy = false;
    portENTER_CRITICAL(&s_worker_lock);
    busy = s_upload_worker_busy;
    portEXIT_CRITICAL(&s_worker_lock);
    return busy;
}

static void *memory_watch_service_alloc(size_t len)
{
    void *ptr = heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == NULL)
    {
        ptr = heap_caps_malloc(len, MALLOC_CAP_8BIT);
    }
    return ptr;
}

static void memory_watch_service_free(void *ptr)
{
    if (ptr != NULL)
    {
        heap_caps_free(ptr);
    }
}

static void memory_watch_service_audio_buffer_free(
    memory_watch_service_audio_buffer_t *buffer)
{
    if (buffer == NULL)
    {
        return;
    }

    memory_watch_service_free(buffer->data);
    buffer->data = NULL;
    buffer->len = 0U;
    buffer->capacity = 0U;
}

static esp_err_t memory_watch_service_audio_buffer_reserve(
    memory_watch_service_audio_buffer_t *buffer, size_t required)
{
    if (buffer == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (required <= buffer->capacity)
    {
        return ESP_OK;
    }
    if (required > MEMORY_WATCH_VOICE_CLIENT_MAX_AUDIO_BYTES)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t next_capacity = buffer->capacity > 0U
                               ? buffer->capacity
                               : kAudioBufferInitialBytes;
    while (next_capacity < required)
    {
        if (next_capacity > (MEMORY_WATCH_VOICE_CLIENT_MAX_AUDIO_BYTES / 2U))
        {
            next_capacity = MEMORY_WATCH_VOICE_CLIENT_MAX_AUDIO_BYTES;
        }
        else
        {
            next_capacity *= 2U;
        }
    }

    uint8_t *next_data = (uint8_t *)memory_watch_service_alloc(next_capacity);
    if (next_data == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    if (buffer->data != NULL && buffer->len > 0U)
    {
        memcpy(next_data, buffer->data, buffer->len);
    }
    memory_watch_service_free(buffer->data);
    buffer->data = next_data;
    buffer->capacity = next_capacity;
    return ESP_OK;
}

static esp_err_t memory_watch_service_audio_write_cb(const uint8_t *data,
                                                     size_t len,
                                                     void *user_ctx)
{
    memory_watch_service_audio_buffer_t *buffer =
        (memory_watch_service_audio_buffer_t *)user_ctx;
    if (buffer == NULL || (data == NULL && len > 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (len == 0U)
    {
        return ESP_OK;
    }
    if (len > MEMORY_WATCH_VOICE_CLIENT_MAX_AUDIO_BYTES ||
        buffer->len > MEMORY_WATCH_VOICE_CLIENT_MAX_AUDIO_BYTES - len)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    const size_t required = buffer->len + len;
    esp_err_t err =
        memory_watch_service_audio_buffer_reserve(buffer, required);
    if (err != ESP_OK)
    {
        return err;
    }

    memcpy(buffer->data + buffer->len, data, len);
    buffer->len = required;
    return ESP_OK;
}

static esp_err_t memory_watch_service_post_worker_result(
    const memory_watch_service_worker_result_t *result)
{
    if (s_command_queue == NULL || result == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    memory_watch_service_cmd_t command = {
        .type = MEMORY_WATCH_SERVICE_CMD_WORKER_DONE,
        .worker_result = *result,
    };
    if (xQueueSend(s_command_queue, &command, portMAX_DELAY) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static esp_err_t memory_watch_service_post_health_result(
    const memory_watch_service_health_result_t *result)
{
    if (s_command_queue == NULL || result == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    memory_watch_service_cmd_t command = {
        .type = MEMORY_WATCH_SERVICE_CMD_HEALTH_DONE,
        .health_result = *result,
    };
    if (xQueueSend(s_command_queue, &command, portMAX_DELAY) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static esp_err_t memory_watch_service_post_upload_started(
    const char *request_id)
{
    if (s_command_queue == NULL || request_id == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    memory_watch_service_cmd_t command = {
        .type = MEMORY_WATCH_SERVICE_CMD_WORKER_UPLOAD_STARTED,
    };
    memory_watch_service_copy_text(command.request_id,
                                   sizeof(command.request_id),
                                   request_id);
    if (xQueueSend(s_command_queue, &command, portMAX_DELAY) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static const char *memory_watch_service_firmware_version(void)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (app_desc == NULL || app_desc->version[0] == '\0')
    {
        return NULL;
    }
    return app_desc->version;
}

static void memory_watch_service_fill_power_fields(
    memory_watch_voice_client_request_t *request)
{
    if (request == NULL)
    {
        return;
    }

    const power_policy_budget_t budget = power_policy_get_budget();
    if (budget.battery_data_valid && budget.battery_percent <= 100U)
    {
        request->has_battery_percent = true;
        request->battery_percent = (int)budget.battery_percent;
    }
    request->has_charging = true;
    request->charging = (budget.flags & POWER_POLICY_FLAG_CHARGING) != 0U;
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
    case MEMORY_WATCH_SERVICE_STATE_NEEDS_CLARIFICATION:
        return true;
    case MEMORY_WATCH_SERVICE_STATE_WAITING_NETWORK:
    case MEMORY_WATCH_SERVICE_STATE_RECORDING:
    case MEMORY_WATCH_SERVICE_STATE_ENCODING:
    case MEMORY_WATCH_SERVICE_STATE_UPLOADING:
    case MEMORY_WATCH_SERVICE_STATE_THINKING:
    default:
        return false;
    }
}

static esp_err_t memory_watch_service_start_upload_job(
    const memory_watch_service_client_config_snapshot_t *client_config,
    const char *request_id)
{
    if (s_upload_worker_queue == NULL || client_config == NULL ||
        request_id == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    memory_watch_service_upload_job_t job = {
        .client_config = *client_config,
    };
    memory_watch_service_copy_text(job.request_id, sizeof(job.request_id),
                                   request_id);
    memory_watch_service_copy_current_clarification(
        job.clarification_id, sizeof(job.clarification_id));

    memory_watch_service_set_upload_worker_busy(true);
    if (xQueueSend(s_upload_worker_queue, &job, 0) != pdTRUE)
    {
        memory_watch_service_set_upload_worker_busy(false);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static esp_err_t memory_watch_service_start_cancel_job(
    const memory_watch_service_client_config_snapshot_t *client_config,
    const char *request_id)
{
    if (s_cancel_worker_queue == NULL || client_config == NULL ||
        request_id == NULL || request_id[0] == '\0')
    {
        return ESP_ERR_INVALID_STATE;
    }

    memory_watch_service_cancel_job_t job = {
        .client_config = *client_config,
    };
    memory_watch_service_copy_text(job.request_id, sizeof(job.request_id),
                                   request_id);
    if (xQueueSend(s_cancel_worker_queue, &job, 0) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static esp_err_t memory_watch_service_start_health_job(
    const memory_watch_service_client_config_snapshot_t *client_config)
{
    if (s_health_worker_queue == NULL || client_config == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    memory_watch_service_health_job_t job = {
        .client_config = *client_config,
    };
    job.client_config.client_config.timeout_ms =
        MEMORY_WATCH_SERVICE_HEALTH_TIMEOUT_MS;
    if (xQueueSend(s_health_worker_queue, &job, 0) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
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
    if (memory_watch_service_is_upload_worker_busy())
    {
        memory_watch_service_set_state(before.state, ESP_ERR_INVALID_STATE);
        ESP_LOGW(TAG, "recording blocked: upload worker is still busy");
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
    memory_watch_service_reset_worker_flags();
    memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_RECORDING,
                                   ESP_OK);
    err = memory_watch_service_start_upload_job(&client_config, request_id);
    if (err != ESP_OK)
    {
        memory_watch_service_set_request_active(false);
        memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_ERROR, err);
    }
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

    err = memory_watch_service_start_health_job(&client_config);
    if (err != ESP_OK)
    {
        memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_ERROR, err);
        return;
    }

    memory_watch_service_set_endpoint_snapshot(true, false);
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

    memory_watch_service_request_record_stop(false);
    memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_ENCODING,
                                   ESP_OK);
}

static void memory_watch_service_handle_cancel_recording(void)
{
    const memory_watch_service_snapshot_t before =
        memory_watch_service_copy_snapshot();
    if (before.state == MEMORY_WATCH_SERVICE_STATE_RECORDING ||
        before.state == MEMORY_WATCH_SERVICE_STATE_ENCODING)
    {
        memory_watch_service_request_record_stop(true);
    }
    memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_CANCELED,
                                   ESP_OK);
}

static void memory_watch_service_handle_cancel_waiting(void)
{
    const memory_watch_service_snapshot_t before =
        memory_watch_service_copy_snapshot();
    if (before.state == MEMORY_WATCH_SERVICE_STATE_RECORDING ||
        before.state == MEMORY_WATCH_SERVICE_STATE_ENCODING)
    {
        memory_watch_service_request_record_stop(true);
        memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_CANCELED,
                                       ESP_OK);
        return;
    }
    if (!before.request_active || before.request_id[0] == '\0')
    {
        memory_watch_service_set_request_active(false);
        memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_CANCELED,
                                       ESP_OK);
        return;
    }
    if (before.state != MEMORY_WATCH_SERVICE_STATE_UPLOADING &&
        before.state != MEMORY_WATCH_SERVICE_STATE_THINKING)
    {
        memory_watch_service_set_state(before.state, ESP_ERR_INVALID_STATE);
        return;
    }

    memory_watch_service_request_wait_cancel(before.request_id);
    memory_watch_service_set_request_active(false);
    memory_watch_service_client_config_snapshot_t client_config;
    esp_err_t err = memory_watch_service_copy_client_config(&client_config);
    if (err == ESP_OK)
    {
        err = memory_watch_service_start_cancel_job(&client_config,
                                                    before.request_id);
    }
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "server cancel job failed: %s", esp_err_to_name(err));
    }
    memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_CANCELED,
                                   ESP_OK);
}

static void memory_watch_service_handle_upload_started(
    const char *request_id)
{
    if (!memory_watch_service_request_id_matches_current(request_id) ||
        memory_watch_service_is_wait_cancel_requested() ||
        memory_watch_service_is_wait_canceled_request(request_id))
    {
        return;
    }

    memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_UPLOADING,
                                   ESP_OK);
    memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_THINKING,
                                   ESP_OK);
}

static void memory_watch_service_handle_health_done(
    const memory_watch_service_health_result_t *result)
{
    if (result == NULL)
    {
        return;
    }

    memory_watch_service_set_endpoint_snapshot(true, result->hermes_online);
    const memory_watch_service_snapshot_t before =
        memory_watch_service_copy_snapshot();
    if (before.request_active)
    {
        return;
    }

    memory_watch_service_set_state(
        result->hermes_online ? MEMORY_WATCH_SERVICE_STATE_READY
                              : MEMORY_WATCH_SERVICE_STATE_ERROR,
        result->error);
}

static memory_watch_service_state_t
memory_watch_service_state_from_response(
    const memory_watch_voice_client_response_t *response,
    esp_err_t error)
{
    if (response != NULL)
    {
        if (strcmp(response->status, "canceled") == 0)
        {
            return MEMORY_WATCH_SERVICE_STATE_CANCELED;
        }
        if (strcmp(response->status, "timeout") == 0)
        {
            return MEMORY_WATCH_SERVICE_STATE_TIMEOUT;
        }
        if (strcmp(response->status, "error") == 0)
        {
            return MEMORY_WATCH_SERVICE_STATE_ERROR;
        }
        if (strcmp(response->action, "clarification_needed") == 0 ||
            response->clarification_id[0] != '\0')
        {
            return MEMORY_WATCH_SERVICE_STATE_NEEDS_CLARIFICATION;
        }
        return MEMORY_WATCH_SERVICE_STATE_DONE;
    }

    if (error == ESP_ERR_TIMEOUT)
    {
        return MEMORY_WATCH_SERVICE_STATE_TIMEOUT;
    }
    return MEMORY_WATCH_SERVICE_STATE_ERROR;
}

static void memory_watch_service_handle_worker_done(
    const memory_watch_service_worker_result_t *result)
{
    if (result == NULL)
    {
        return;
    }

    if (memory_watch_service_is_wait_canceled_request(result->request_id))
    {
        memory_watch_service_set_request_active(false);
        memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_CANCELED,
                                       ESP_OK);
        return;
    }

    if (!memory_watch_service_request_id_matches_current(result->request_id))
    {
        return;
    }

    if (result->cancel_requested)
    {
        memory_watch_service_set_request_active(false);
        memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_CANCELED,
                                       ESP_OK);
        return;
    }

    if (result->has_response)
    {
        memory_watch_service_set_response_texts(&result->response);
    }

    const memory_watch_service_state_t next_state =
        memory_watch_service_state_from_response(
            result->has_response ? &result->response : NULL,
            result->error);
    memory_watch_service_set_request_active(false);
    memory_watch_service_set_state(next_state, result->error);
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
        memory_watch_service_handle_cancel_recording();
        break;
    case MEMORY_WATCH_SERVICE_CMD_CANCEL_WAITING:
        memory_watch_service_handle_cancel_waiting();
        break;
    case MEMORY_WATCH_SERVICE_CMD_CANCEL_CLARIFICATION:
        memory_watch_service_clear_clarification();
        memory_watch_service_set_request_active(false);
        memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_READY,
                                       ESP_OK);
        break;
    case MEMORY_WATCH_SERVICE_CMD_HEALTH_DONE:
        memory_watch_service_handle_health_done(&command->health_result);
        break;
    case MEMORY_WATCH_SERVICE_CMD_WORKER_UPLOAD_STARTED:
        memory_watch_service_handle_upload_started(command->request_id);
        break;
    case MEMORY_WATCH_SERVICE_CMD_WORKER_DONE:
        memory_watch_service_handle_worker_done(&command->worker_result);
        break;
    default:
        break;
    }
}

static void memory_watch_service_upload_worker_task(void *arg)
{
    (void)arg;

    while (1)
    {
        memory_watch_service_upload_job_t job;
        if (xQueueReceive(s_upload_worker_queue, &job, portMAX_DELAY) !=
            pdTRUE)
        {
            continue;
        }

        memory_watch_service_worker_result_t result = {0};
        memory_watch_service_copy_text(result.request_id,
                                       sizeof(result.request_id),
                                       job.request_id);
        result.error = ESP_OK;

        memory_watch_service_audio_buffer_t audio_buffer = {0};
        memory_watch_recorder_result_t recorder_result = {0};
        memory_watch_recorder_config_t recorder_config = {
            .ogg_serial = esp_random(),
            .write_cb = memory_watch_service_audio_write_cb,
            .write_user_ctx = &audio_buffer,
            .should_stop_cb = memory_watch_service_should_stop_recording,
            .should_stop_user_ctx = NULL,
            .should_abort_cb = memory_watch_service_should_abort_recording,
            .should_abort_user_ctx = NULL,
        };
        result.error = memory_watch_recorder_capture_ogg_opus(
            &recorder_config, &recorder_result);

        const bool discard_requested =
            memory_watch_service_is_record_discard_requested();
        if (discard_requested)
        {
            result.cancel_requested = true;
            result.error = ESP_OK;
        }
        else if (result.error == ESP_OK)
        {
            (void)memory_watch_service_post_upload_started(job.request_id);

            memory_watch_voice_client_request_t request = {
                .request_id = job.request_id,
                .audio = audio_buffer.data,
                .audio_len = audio_buffer.len,
                .clarification_id = job.clarification_id,
                .firmware_version = memory_watch_service_firmware_version(),
                .ui_state = memory_watch_service_state_to_string(
                    MEMORY_WATCH_SERVICE_STATE_THINKING),
            };
            memory_watch_service_fill_power_fields(&request);
            result.error = memory_watch_voice_client_post_voice_command(
                &job.client_config.client_config, &request, &result.response);
            result.has_response = result.error == ESP_OK;
            result.cancel_requested =
                memory_watch_service_is_wait_cancel_requested() ||
                memory_watch_service_is_wait_canceled_request(job.request_id);
        }

        memory_watch_service_audio_buffer_free(&audio_buffer);
        memory_watch_service_set_upload_worker_busy(false);
        (void)memory_watch_service_post_worker_result(&result);
    }
}

static void memory_watch_service_health_worker_task(void *arg)
{
    (void)arg;

    while (1)
    {
        memory_watch_service_health_job_t job;
        if (xQueueReceive(s_health_worker_queue, &job, portMAX_DELAY) !=
            pdTRUE)
        {
            continue;
        }

        memory_watch_voice_client_health_t health = {0};
        const esp_err_t err = memory_watch_voice_client_get_health(
            &job.client_config.client_config, &health);
        const memory_watch_service_health_result_t result = {
            .error = err,
            .hermes_online = err == ESP_OK,
        };
        (void)memory_watch_service_post_health_result(&result);
    }
}

static void memory_watch_service_cancel_worker_task(void *arg)
{
    (void)arg;

    while (1)
    {
        memory_watch_service_cancel_job_t job;
        if (xQueueReceive(s_cancel_worker_queue, &job, portMAX_DELAY) !=
            pdTRUE)
        {
            continue;
        }

        memory_watch_voice_client_response_t response = {0};
        const esp_err_t err = memory_watch_voice_client_cancel_request(
            &job.client_config.client_config, job.request_id, &response);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "server cancel request failed: %s",
                     esp_err_to_name(err));
        }
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
    if (s_service_task_handle != NULL &&
        s_upload_worker_task_handle != NULL &&
        s_cancel_worker_task_handle != NULL &&
        s_health_worker_task_handle != NULL)
    {
        return ESP_OK;
    }
    if (s_service_task_handle != NULL ||
        s_upload_worker_task_handle != NULL ||
        s_cancel_worker_task_handle != NULL ||
        s_health_worker_task_handle != NULL)
    {
        return ESP_ERR_INVALID_STATE;
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
    if (s_upload_worker_queue == NULL)
    {
        s_upload_worker_queue = xQueueCreateStatic(
            kWorkerQueueLength,
            sizeof(memory_watch_service_upload_job_t),
            s_upload_worker_queue_storage,
            &s_upload_worker_queue_buffer);
    }
    if (s_upload_worker_queue == NULL)
    {
        memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_ERROR,
                                       ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }
    if (s_cancel_worker_queue == NULL)
    {
        s_cancel_worker_queue = xQueueCreateStatic(
            kCancelQueueLength,
            sizeof(memory_watch_service_cancel_job_t),
            s_cancel_worker_queue_storage,
            &s_cancel_worker_queue_buffer);
    }
    if (s_cancel_worker_queue == NULL)
    {
        memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_ERROR,
                                       ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }
    if (s_health_worker_queue == NULL)
    {
        s_health_worker_queue = xQueueCreateStatic(
            kHealthQueueLength,
            sizeof(memory_watch_service_health_job_t),
            s_health_worker_queue_storage,
            &s_health_worker_queue_buffer);
    }
    if (s_health_worker_queue == NULL)
    {
        memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_ERROR,
                                       ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t endpoint_err =
        memory_watch_service_load_endpoint_from_nvs();
    if (endpoint_err != ESP_OK)
    {
        ESP_LOGW(TAG, "watch endpoint NVS load failed: %s",
                 esp_err_to_name(endpoint_err));
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

    s_upload_worker_task_handle = xTaskCreateStatic(
        memory_watch_service_upload_worker_task,
        "mw_upload",
        kUploadWorkerStackWords,
        NULL,
        4,
        s_upload_worker_task_stack,
        &s_upload_worker_task_buffer);
    if (s_upload_worker_task_handle == NULL)
    {
        memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_ERROR,
                                       ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }

    s_health_worker_task_handle = xTaskCreateStatic(
        memory_watch_service_health_worker_task,
        "mw_health",
        kHealthWorkerStackWords,
        NULL,
        4,
        s_health_worker_task_stack,
        &s_health_worker_task_buffer);
    if (s_health_worker_task_handle == NULL)
    {
        memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_ERROR,
                                       ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }

    s_cancel_worker_task_handle = xTaskCreateStatic(
        memory_watch_service_cancel_worker_task,
        "mw_cancel",
        kCancelWorkerStackWords,
        NULL,
        4,
        s_cancel_worker_task_stack,
        &s_cancel_worker_task_buffer);
    if (s_cancel_worker_task_handle == NULL)
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
    const memory_watch_service_snapshot_t snapshot =
        memory_watch_service_copy_snapshot();
    if (snapshot.request_active)
    {
        return ESP_ERR_INVALID_STATE;
    }

    memory_watch_service_endpoint_state_t next_config = {0};
    esp_err_t err = memory_watch_service_build_endpoint_state(config,
                                                              &next_config);
    if (err != ESP_OK)
    {
        return err;
    }

    portENTER_CRITICAL(&s_endpoint_lock);
    s_endpoint_config = next_config;
    portEXIT_CRITICAL(&s_endpoint_lock);

    memory_watch_service_set_endpoint_snapshot(true, false);
    memory_watch_service_set_state(snapshot.state, ESP_OK);
    return ESP_OK;
}

esp_err_t memory_watch_service_save_endpoint_to_nvs(
    const memory_watch_service_endpoint_config_t *config)
{
    const memory_watch_service_snapshot_t snapshot =
        memory_watch_service_copy_snapshot();
    if (snapshot.request_active)
    {
        return ESP_ERR_INVALID_STATE;
    }

    memory_watch_service_endpoint_state_t next_config = {0};
    esp_err_t err = memory_watch_service_build_endpoint_state(config,
                                                              &next_config);
    if (err != ESP_OK)
    {
        return err;
    }

    nvs_handle_t handle = 0;
    err = nvs_open(kEndpointNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        return err;
    }

    err = nvs_set_str(handle, kEndpointNvsBaseUrlKey, next_config.base_url);
    if (err == ESP_OK)
    {
        err = nvs_set_str(handle, kEndpointNvsDeviceIdKey,
                          next_config.device_id);
    }
    if (err == ESP_OK)
    {
        err = nvs_set_str(handle, kEndpointNvsDeviceTokenKey,
                          next_config.device_token);
    }
    if (err == ESP_OK)
    {
        err = nvs_set_u32(handle, kEndpointNvsTimeoutMsKey,
                          next_config.timeout_ms);
    }
    if (err == ESP_OK)
    {
        err = nvs_set_u8(handle, kEndpointNvsAllowHttpKey,
                         next_config.allow_insecure_http ? 1U : 0U);
    }
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK)
    {
        return err;
    }

    portENTER_CRITICAL(&s_endpoint_lock);
    s_endpoint_config = next_config;
    portEXIT_CRITICAL(&s_endpoint_lock);

    memory_watch_service_set_endpoint_snapshot(true, false);
    memory_watch_service_set_state(snapshot.state, ESP_OK);
    ESP_LOGI(TAG,
             "watch endpoint config saved to NVS: device_id=%s allow_http=%u timeout_ms=%lu",
             next_config.device_id,
             (unsigned int)next_config.allow_insecure_http,
             (unsigned long)next_config.timeout_ms);
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
