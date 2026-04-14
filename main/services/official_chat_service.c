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

/*
 * 官方聊天服务实现说明：
 * - 底层 `official_chat` 更像一次会话实例，本模块把它包装成“可长期驻留的服务”；
 * - UI 只需要声明前台/后台意图，本模块负责等网络、拉起实例、接收事件、缓存文本和安全销毁；
 * - 关闭流程额外引入 quiet period，是为了给传输层和音频链路一个收敛窗口，降低异常销毁概率。
 */

static const char *TAG = "official_chat_srv";
static const size_t kLastUserTextMaxBytes = 192;
static const size_t kLastAssistantTextMaxBytes = 256;
static const size_t kMessageHistoryCapacity = 8;
static const uint32_t kShutdownTransportQuietPeriodMs = 1500;
static const uint32_t kShutdownWaitTimeoutMs = 4000;

static TaskHandle_t s_service_task_handle = NULL;                 // 服务任务句柄
static official_chat_handle_t s_chat_handle = NULL;               // 底层 official_chat 会话句柄
static volatile bool s_foreground_requested = false;              // UI 是否请求前台会话
static volatile bool s_shutdown_requested = false;                // 是否已请求进入关闭流程
static volatile bool s_shutdown_stop_requested = false;           // 关闭流程中是否已触发 stop_listening
static volatile TickType_t s_shutdown_destroy_deadline_ticks = 0; // quiet period 截止 tick
static volatile official_chat_service_state_t s_service_state =
    OFFICIAL_CHAT_SERVICE_STATE_STOPPED;
static volatile esp_err_t s_last_error = ESP_OK;
static StaticSemaphore_t s_text_mutex_buffer;
static SemaphoreHandle_t s_text_mutex = NULL;
static char s_last_user_text[192] = {0};                           // 最近用户文本缓存
static char s_last_assistant_text[256] = {0};                      // 最近助手文本缓存
static official_chat_service_message_t s_message_history[8] = {0}; // 固定容量消息历史
static size_t s_message_count = 0;                                 // 当前历史条目数

static void official_chat_service_lock(void)
{
    /* 文本缓存会被事件回调与 UI 查询同时访问，因此统一用互斥锁保护。 */
    if (s_text_mutex != NULL)
    {
        xSemaphoreTake(s_text_mutex, portMAX_DELAY);
    }
}

static void official_chat_service_unlock(void)
{
    if (s_text_mutex != NULL)
    {
        xSemaphoreGive(s_text_mutex);
    }
}

static void official_chat_service_clear_cached_text_locked(void)
{
    memset(s_last_user_text, 0, sizeof(s_last_user_text));
    memset(s_last_assistant_text, 0, sizeof(s_last_assistant_text));
    memset(s_message_history, 0, sizeof(s_message_history));
    s_message_count = 0;
}

static void official_chat_service_store_text_locked(char *target,
                                                    size_t target_size,
                                                    const char *text)
{
    if (target == NULL || target_size == 0 || text == NULL)
    {
        return;
    }

    snprintf(target, target_size, "%s", text);
}

static void official_chat_service_enqueue_message_locked(
    official_chat_service_message_role_t role, const char *text)
{
    official_chat_service_message_t message = {
        .role = role,
    };

    snprintf(message.text, sizeof(message.text), "%s",
             text != NULL ? text : "");

    if (s_message_count == kMessageHistoryCapacity)
    {
        /* 固定容量环形历史的简化实现：满了就丢最旧的一条。 */
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
                                                        size_t size)
{
    if (buffer == NULL || size == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    const bool has_text = source != NULL && source[0] != '\0';
    if (has_text)
    {
        snprintf(buffer, size, "%s", source);
    }
    else
    {
        buffer[0] = '\0';
    }

    return has_text ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static official_chat_service_state_t map_official_chat_state(
    official_chat_state_t state)
{
    switch (state)
    {
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

static bool official_chat_service_requires_shutdown_quiet_period(
    official_chat_state_t state)
{
    /* 这些状态下传输或音频链路仍可能活跃，销毁前需要额外等待。 */
    return state == OFFICIAL_CHAT_STATE_CONNECTING ||
           state == OFFICIAL_CHAT_STATE_IDLE ||
           state == OFFICIAL_CHAT_STATE_LISTENING ||
           state == OFFICIAL_CHAT_STATE_SPEAKING;
}

static void official_chat_service_request_shutdown_internal(void)
{
    /* 通过 abort delay 唤醒服务任务，让关闭请求尽快生效。 */
    s_foreground_requested = false;
    s_shutdown_requested = true;
    s_shutdown_stop_requested = false;
    s_shutdown_destroy_deadline_ticks = 0;

    if (s_service_task_handle != NULL)
    {
        xTaskAbortDelay(s_service_task_handle);
    }
}

const char *official_chat_service_state_to_string(
    official_chat_service_state_t state)
{
    switch (state)
    {
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
                                           void *user_data)
{
    (void)user_data;
    if (event == NULL)
    {
        return;
    }

    if (s_shutdown_requested)
    {
        /* 关闭流程开始后丢弃后续事件，避免缓存重新被写入。 */
        return;
    }

    switch (event->type)
    {
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

static esp_err_t official_chat_service_start_internal(void)
{
    /* 启动参数由服务层集中指定，避免页面或控制器分散管理底层配置。 */
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
    if (s_chat_handle == NULL)
    {
        s_last_error = ESP_FAIL;
        s_service_state = OFFICIAL_CHAT_SERVICE_STATE_ERROR;
        ESP_LOGE(TAG, "official_chat_create failed");
        return ESP_FAIL;
    }

    esp_err_t ret = official_chat_set_event_callback(
        s_chat_handle, official_chat_service_event_cb, NULL);
    if (ret != ESP_OK)
    {
        s_last_error = ret;
        s_service_state = OFFICIAL_CHAT_SERVICE_STATE_ERROR;
        ESP_LOGE(TAG, "official_chat_set_event_callback failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    ret = official_chat_start(s_chat_handle);
    if (ret != ESP_OK)
    {
        s_last_error = ret;
        s_service_state = OFFICIAL_CHAT_SERVICE_STATE_ERROR;
        ESP_LOGE(TAG, "official_chat_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

static void official_chat_service_task(void *arg)
{
    (void)arg;

    while (1)
    {
        if (s_shutdown_requested)
        {
            /*
             * 关闭流程分三步：
             * 1. 请求底层进入可关闭状态；
             * 2. 等待 idle，并额外留出 quiet period；
             * 3. 解绑回调、销毁实例、清空缓存。
             */
            official_chat_handle_t chat_handle = s_chat_handle;

            if (chat_handle != NULL)
            {
                const esp_err_t prepare_ret =
                    official_chat_prepare_shutdown(chat_handle);
                if (prepare_ret != ESP_OK)
                {
                    ESP_LOGW(TAG, "official_chat_prepare_shutdown failed: %s",
                             esp_err_to_name(prepare_ret));
                }
                official_chat_state_t chat_state =
                    official_chat_get_state(chat_handle);
                s_service_state = map_official_chat_state(chat_state);
                if (official_chat_service_requires_shutdown_quiet_period(
                        chat_state))
                {
                    if (chat_state != OFFICIAL_CHAT_STATE_IDLE &&
                        !s_shutdown_stop_requested)
                    {
                        const esp_err_t stop_ret =
                            official_chat_stop_listening(chat_handle);
                        if (stop_ret != ESP_OK)
                        {
                            ESP_LOGW(
                                TAG,
                                "official_chat_stop_listening during shutdown failed: %s",
                                esp_err_to_name(stop_ret));
                        }
                        s_shutdown_stop_requested = true;
                        ESP_LOGI(
                            TAG,
                            "shutdown waiting for idle before destroy state=%s",
                            official_chat_service_state_to_string(
                                map_official_chat_state(chat_state)));
                    }

                    if (chat_state != OFFICIAL_CHAT_STATE_IDLE)
                    {
                        if (s_shutdown_destroy_deadline_ticks != 0)
                        {
                            s_shutdown_destroy_deadline_ticks = 0;
                            ESP_LOGI(
                                TAG,
                                "shutdown quiet period canceled until idle state=%s",
                                official_chat_service_state_to_string(
                                    map_official_chat_state(chat_state)));
                        }
                        vTaskDelay(pdMS_TO_TICKS(50));
                        continue;
                    }

                    s_service_state = OFFICIAL_CHAT_SERVICE_STATE_IDLE;
                    if (s_shutdown_destroy_deadline_ticks == 0)
                    {
                        s_shutdown_destroy_deadline_ticks =
                            xTaskGetTickCount() +
                            pdMS_TO_TICKS(kShutdownTransportQuietPeriodMs);
                        ESP_LOGI(
                            TAG,
                            "shutdown reached idle, arming destroy quiet period wait_ms=%lu",
                            (unsigned long)kShutdownTransportQuietPeriodMs);
                        ESP_LOGI(TAG,
                                 "shutdown transport quiet period armed state=idle wait_ms=%lu",
                                 (unsigned long)kShutdownTransportQuietPeriodMs);
                    }

                    if (xTaskGetTickCount() <
                        s_shutdown_destroy_deadline_ticks)
                    {
                        vTaskDelay(pdMS_TO_TICKS(50));
                        continue;
                    }
                }
            }

            if (s_shutdown_stop_requested &&
                xTaskGetTickCount() < s_shutdown_destroy_deadline_ticks)
            {
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }

            if (chat_handle != NULL)
            {
                official_chat_set_event_callback(chat_handle, NULL, NULL);
                official_chat_destroy(chat_handle);
            }

            official_chat_service_lock();
            official_chat_service_clear_cached_text_locked();
            official_chat_service_unlock();

            s_chat_handle = NULL;
            s_last_error = ESP_OK;
            s_service_state = OFFICIAL_CHAT_SERVICE_STATE_STOPPED;
            s_shutdown_requested = false;
            s_shutdown_stop_requested = false;
            s_shutdown_destroy_deadline_ticks = 0;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (!s_foreground_requested)
        {
            /* 未进入前台时保留任务本身，但不主动创建会话实例。 */
            if (s_chat_handle == NULL)
            {
                s_service_state = OFFICIAL_CHAT_SERVICE_STATE_STOPPED;
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        if (s_chat_handle != NULL)
        {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        if (!network_service_is_service_ready())
        {
            /* 会话严格依赖网络服务层，而不是自己重复做联网判断。 */
            s_service_state = OFFICIAL_CHAT_SERVICE_STATE_WAITING_NETWORK;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (official_chat_service_start_internal() != ESP_OK)
        {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t official_chat_service_init(void)
{
    if (s_service_task_handle != NULL)
    {
        return ESP_OK;
    }

    /* 使用静态互斥锁，避免服务初始化时再引入额外堆分配不确定性。 */
    if (s_text_mutex == NULL)
    {
        s_text_mutex = xSemaphoreCreateMutexStatic(&s_text_mutex_buffer);
    }

    BaseType_t result = xTaskCreatePinnedToCore(
        official_chat_service_task, "official_chat_service", 1024 * 8, NULL, 5,
        &s_service_task_handle, 0);
    if (result != pdPASS)
    {
        s_service_task_handle = NULL;
        s_service_state = OFFICIAL_CHAT_SERVICE_STATE_ERROR;
        s_last_error = ESP_FAIL;
        return ESP_FAIL;
    }

    return ESP_OK;
}

void official_chat_service_enter_foreground(void)
{
    s_shutdown_requested = false;
    s_shutdown_stop_requested = false;
    s_shutdown_destroy_deadline_ticks = 0;
    s_foreground_requested = true;
}

void official_chat_service_leave_foreground(void)
{
    s_foreground_requested = false;
}

void official_chat_service_request_shutdown(void)
{
    official_chat_service_request_shutdown_internal();
}

bool official_chat_service_is_shutdown_pending(void)
{
    return s_shutdown_requested;
}

esp_err_t official_chat_service_shutdown(void)
{
    official_chat_service_request_shutdown_internal();

    const TickType_t start_ticks = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(kShutdownWaitTimeoutMs);

    while (s_shutdown_requested || s_chat_handle != NULL ||
           s_service_state != OFFICIAL_CHAT_SERVICE_STATE_STOPPED)
    {
        if ((xTaskGetTickCount() - start_ticks) >= timeout_ticks)
        {
            return ESP_ERR_TIMEOUT;
        }

        if (s_service_task_handle != NULL)
        {
            xTaskAbortDelay(s_service_task_handle);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return ESP_OK;
}

official_chat_service_state_t official_chat_service_get_state(void)
{
    return s_service_state;
}

esp_err_t official_chat_service_get_last_error(void)
{
    return s_last_error;
}

size_t official_chat_service_get_message_count(void)
{
    size_t message_count = 0;

    official_chat_service_lock();
    message_count = s_message_count;
    official_chat_service_unlock();

    return message_count;
}

esp_err_t official_chat_service_get_message(
    size_t index, official_chat_service_message_t *out_message)
{
    if (out_message == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_ERR_NOT_FOUND;
    official_chat_service_lock();
    if (index < s_message_count)
    {
        *out_message = s_message_history[index];
        ret = ESP_OK;
    }
    official_chat_service_unlock();

    return ret;
}

esp_err_t official_chat_service_get_last_user_text(char *buffer, size_t size)
{
    official_chat_service_lock();
    esp_err_t ret = official_chat_service_copy_text_locked(s_last_user_text,
                                                           buffer, size);
    official_chat_service_unlock();
    return ret;
}

esp_err_t official_chat_service_get_last_assistant_text(char *buffer,
                                                        size_t size)
{
    official_chat_service_lock();
    esp_err_t ret = official_chat_service_copy_text_locked(
        s_last_assistant_text, buffer, size);
    official_chat_service_unlock();
    return ret;
}
