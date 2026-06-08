#include "services/memory_watch_service.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "network_service.h"

static const char *TAG = "memory_watch";
static const UBaseType_t kCommandQueueLength = 8;
static const uint32_t kTaskStackWords = 2048;

typedef enum
{
    MEMORY_WATCH_SERVICE_CMD_BEGIN_RECORDING = 0,
    MEMORY_WATCH_SERVICE_CMD_SEND_RECORDING,
    MEMORY_WATCH_SERVICE_CMD_CANCEL_RECORDING,
    MEMORY_WATCH_SERVICE_CMD_CANCEL_WAITING,
    MEMORY_WATCH_SERVICE_CMD_CANCEL_CLARIFICATION,
} memory_watch_service_cmd_type_t;

typedef struct
{
    memory_watch_service_cmd_type_t type;
} memory_watch_service_cmd_t;

static TaskHandle_t s_service_task_handle = NULL;
static QueueHandle_t s_command_queue = NULL;
static StaticQueue_t s_command_queue_buffer;
static uint8_t s_command_queue_storage[8 * sizeof(memory_watch_service_cmd_t)];
static StaticTask_t s_service_task_buffer;
static StackType_t s_service_task_stack[2048];
static portMUX_TYPE s_snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
static memory_watch_service_snapshot_t s_snapshot = {
    .state = MEMORY_WATCH_SERVICE_STATE_READY,
    .network_ready = false,
    .request_active = false,
    .clarification_active = false,
    .last_error = ESP_OK,
};

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

    memory_watch_service_set_request_active(true);
    memory_watch_service_set_state(MEMORY_WATCH_SERVICE_STATE_RECORDING,
                                   ESP_OK);
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
