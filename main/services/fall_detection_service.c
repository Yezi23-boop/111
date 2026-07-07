#include "services/fall_detection_service.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "fall_model_runner.h"
#include "features/alerts/app_alert_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "services/imu_service.h"
#include "services/watch_endpoint_service.h"

static const char *k_fall_app_danger_type = "fall";
static const char *k_fall_app_message = "检测到跌倒";

extern const uint8_t cnn_c24_pool225_do015_e80_with_test_espdl[]
    asm("_binary_cnn_c24_pool225_do015_e80_with_test_espdl_start");

static const char *TAG = "fall_detection";

static const UBaseType_t k_window_queue_length = 1U;
static const uint32_t k_task_stack_bytes = 6144U;
static const UBaseType_t k_task_priority = 2U;
static const char *k_model_name = "cnn_c24_pool225_do015_e80";
static const uint16_t k_legacy_model_frame_count =
    FALL_MODEL_INPUT_ELEMENTS / 3U;
static const float k_fall_clear_threshold = 0.50f; // 连续低于该 FALL 概率才允许解除已确认跌倒。
static const uint32_t k_fall_clear_window_count = 2U; // 4s 滑窗每 2s 发布一次，2 个低风险窗口约等于 4s 恢复证据。
typedef enum
{
    FALL_DETECTION_ALERT_EVENT_NONE = 0,
    FALL_DETECTION_ALERT_EVENT_CONFIRMED,
    FALL_DETECTION_ALERT_EVENT_CLEARED,
} fall_detection_alert_event_type_t;

typedef struct
{
    fall_detection_alert_event_type_t type;
    uint32_t alert_sequence;
    uint32_t window_sequence;
    uint32_t clear_window_count;
    float fall_prob;
    int64_t window_end_time_us;
} fall_detection_alert_event_t;

typedef struct
{
    bool started;
    TaskHandle_t task_handle;
    portMUX_TYPE lock;
    fall_detection_service_snapshot_t snapshot;
    QueueHandle_t window_queue;
    StaticQueue_t window_queue_control;
    uint8_t *window_queue_storage;
    fall_model_runner_t *runner;
    imu_service_accel_window_t *current_window;
    float *model_input;
} fall_detection_context_t;

static fall_detection_context_t s_fall_detection = {
    .started = false,
    .task_handle = NULL,
    .lock = portMUX_INITIALIZER_UNLOCKED,
    .snapshot = {
        .state = FALL_DETECTION_SERVICE_STATE_STOPPED,
        .model_ready = false,
        .self_test_passed = false,
        .threshold = FALL_MODEL_THRESHOLD_DEFAULT,
        .alert_state = FALL_DETECTION_ALERT_STATE_IDLE,
        .last_error = ESP_OK,
        .last_alert_error = ESP_OK,
        .label_index = -1,
    },
    .window_queue = NULL,
    .window_queue_storage = NULL,
    .runner = NULL,
    .current_window = NULL,
    .model_input = NULL,
};

static void fall_detection_store_state(fall_detection_service_state_t state,
                                       esp_err_t error)
{
    taskENTER_CRITICAL(&s_fall_detection.lock);
    s_fall_detection.snapshot.state = state;
    s_fall_detection.snapshot.last_error = error;
    taskEXIT_CRITICAL(&s_fall_detection.lock);
}

static void fall_detection_store_model_ready(bool self_test_passed)
{
    taskENTER_CRITICAL(&s_fall_detection.lock);
    s_fall_detection.snapshot.model_ready = true;
    s_fall_detection.snapshot.self_test_passed = self_test_passed;
    s_fall_detection.snapshot.threshold = FALL_MODEL_THRESHOLD_DEFAULT;
    s_fall_detection.snapshot.last_error = ESP_OK;
    taskEXIT_CRITICAL(&s_fall_detection.lock);
}

static void fall_detection_store_window_received(
    const imu_service_accel_window_t *window)
{
    taskENTER_CRITICAL(&s_fall_detection.lock);
    s_fall_detection.snapshot.window_count++;
    s_fall_detection.snapshot.last_window_sequence = window->sequence;
    s_fall_detection.snapshot.last_window_end_time_us = window->end_time_us;
    taskEXIT_CRITICAL(&s_fall_detection.lock);
}

static void fall_detection_store_result(const imu_service_accel_window_t *window,
                                        const fall_model_result_t *result)
{
    taskENTER_CRITICAL(&s_fall_detection.lock);
    s_fall_detection.snapshot.state = FALL_DETECTION_SERVICE_STATE_RUNNING;
    s_fall_detection.snapshot.inference_count++;
    s_fall_detection.snapshot.last_window_sequence = window->sequence;
    s_fall_detection.snapshot.last_window_end_time_us = window->end_time_us;
    s_fall_detection.snapshot.label_index = result->label_index;
    s_fall_detection.snapshot.confidence = result->confidence;
    s_fall_detection.snapshot.adl_prob = result->adl_prob;
    s_fall_detection.snapshot.fall_prob = result->fall_prob;
    s_fall_detection.snapshot.infer_us = result->infer_us;
    s_fall_detection.snapshot.last_error = ESP_OK;
    taskEXIT_CRITICAL(&s_fall_detection.lock);
}

static void fall_detection_store_alert_error(esp_err_t error)
{
    taskENTER_CRITICAL(&s_fall_detection.lock);
    s_fall_detection.snapshot.last_alert_error = error;
    taskEXIT_CRITICAL(&s_fall_detection.lock);
}

static void fall_detection_store_inference_error(esp_err_t error)
{
    taskENTER_CRITICAL(&s_fall_detection.lock);
    s_fall_detection.snapshot.state = FALL_DETECTION_SERVICE_STATE_ERROR;
    s_fall_detection.snapshot.inference_error_count++;
    s_fall_detection.snapshot.last_error = error;
    taskEXIT_CRITICAL(&s_fall_detection.lock);
}

static esp_err_t fall_detection_prepare_buffers(void)
{
    if (s_fall_detection.window_queue_storage == NULL)
    {
        s_fall_detection.window_queue_storage =
            (uint8_t *)heap_caps_malloc(
                k_window_queue_length * sizeof(imu_service_accel_window_t),
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_fall_detection.window_queue_storage == NULL)
        {
            ESP_LOGE(TAG, "window queue psram allocation failed");
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_fall_detection.current_window == NULL)
    {
        s_fall_detection.current_window =
            (imu_service_accel_window_t *)heap_caps_calloc(
                1U, sizeof(imu_service_accel_window_t),
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_fall_detection.current_window == NULL)
        {
            ESP_LOGE(TAG, "current window psram allocation failed");
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_fall_detection.model_input == NULL)
    {
        s_fall_detection.model_input =
            (float *)heap_caps_calloc(FALL_MODEL_INPUT_ELEMENTS,
                                      sizeof(float),
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_fall_detection.model_input == NULL)
        {
            ESP_LOGE(TAG, "model input psram allocation failed");
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

static QueueHandle_t fall_detection_prepare_window_queue(void)
{
    if (s_fall_detection.window_queue != NULL)
    {
        return s_fall_detection.window_queue;
    }

    s_fall_detection.window_queue = xQueueCreateStatic(
        k_window_queue_length,
        sizeof(imu_service_accel_window_t),
        s_fall_detection.window_queue_storage,
        &s_fall_detection.window_queue_control);
    return s_fall_detection.window_queue;
}

static void fall_detection_fill_model_input(
    const imu_service_accel_window_t *window,
    float input[FALL_MODEL_INPUT_ELEMENTS])
{
    /*
     * Board orientation from six-face test:
     * model +X = watch right / USB side = -chip X,
     * model +Y = watch top = +chip Y,
     * model +Z = watch front/dial = -chip Z.
     */
    /*
     * 本阶段只接入 Event Trigger + 5s 事件窗口，不替换模型资产。
     * 旧 3ch 模型仍固定读取 200 帧加速度；后续 6ch / [1,1500]
     * 模型接入时再把 gyro 转为 rad/s 并扩展输入构造。
     */
    for (uint16_t frame = 0; frame < k_legacy_model_frame_count; ++frame)
    {
        input[(frame * 3U) + 0U] = -window->accel[frame].x;
        input[(frame * 3U) + 1U] = window->accel[frame].y;
        input[(frame * 3U) + 2U] = -window->accel[frame].z;
    }
}

static fall_detection_alert_event_t fall_detection_update_alert_state(
    const imu_service_accel_window_t *window,
    const fall_model_result_t *result)
{
    fall_detection_alert_event_t event = {
        .type = FALL_DETECTION_ALERT_EVENT_NONE,
        .window_sequence = window->sequence,
        .fall_prob = result->fall_prob,
        .window_end_time_us = window->end_time_us,
    };

    taskENTER_CRITICAL(&s_fall_detection.lock);
    if (s_fall_detection.snapshot.alert_state ==
        FALL_DETECTION_ALERT_STATE_IDLE)
    {
        s_fall_detection.snapshot.clear_window_count = 0;
        if (result->fall_prob >= FALL_MODEL_THRESHOLD_DEFAULT)
        {
            s_fall_detection.snapshot.alert_state =
                FALL_DETECTION_ALERT_STATE_CONFIRMED;
            s_fall_detection.snapshot.alert_sequence++;
            s_fall_detection.snapshot.last_alert_window_sequence =
                window->sequence;
            s_fall_detection.snapshot.last_alert_fall_prob =
                result->fall_prob;
            s_fall_detection.snapshot.last_alert_error = ESP_OK;

            event.type = FALL_DETECTION_ALERT_EVENT_CONFIRMED;
            event.alert_sequence = s_fall_detection.snapshot.alert_sequence;
        }
    }
    else if (result->fall_prob < k_fall_clear_threshold)
    {
        s_fall_detection.snapshot.clear_window_count++;
        event.clear_window_count =
            s_fall_detection.snapshot.clear_window_count;
        if (s_fall_detection.snapshot.clear_window_count >=
            k_fall_clear_window_count)
        {
            s_fall_detection.snapshot.alert_state =
                FALL_DETECTION_ALERT_STATE_IDLE;
            s_fall_detection.snapshot.clear_window_count = 0;
            event.type = FALL_DETECTION_ALERT_EVENT_CLEARED;
            event.alert_sequence = s_fall_detection.snapshot.alert_sequence;
        }
    }
    else
    {
        s_fall_detection.snapshot.clear_window_count = 0;
    }
    taskEXIT_CRITICAL(&s_fall_detection.lock);

    return event;
}

static esp_err_t fall_detection_raise_local_alert(void)
{
    const app_alert_request_t request = {
        .source = APP_ALERT_SOURCE_FALL_DETECTION,
        .severity = APP_ALERT_SEVERITY_DANGER,
        .label = APP_ALERT_LABEL_FALL,
    };
    return app_alert_manager_raise(&request);
}

static esp_err_t fall_detection_post_app_alert(
    const fall_detection_alert_event_t *event)
{
    const watch_endpoint_danger_alert_t alert = {
        .danger_type = k_fall_app_danger_type,
        .danger_prob = event->fall_prob,
        .alert_sequence = event->alert_sequence,
        .message = k_fall_app_message,
    };
    return watch_endpoint_service_post_danger_alert(&alert);
}

static void fall_detection_handle_alert_event(
    const fall_detection_alert_event_t *event)
{
    if (event->type == FALL_DETECTION_ALERT_EVENT_CONFIRMED)
    {
        ESP_LOGW(TAG,
                 "跌倒告警已确认: 告警序号=%u 窗口序号=%u 跌倒概率=%.4f 阈值=%.2f 窗口结束_us=%lld",
                 (unsigned)event->alert_sequence,
                 (unsigned)event->window_sequence,
                 (double)event->fall_prob,
                 (double)FALL_MODEL_THRESHOLD_DEFAULT,
                 (long long)event->window_end_time_us);

        esp_err_t local_ret = fall_detection_raise_local_alert();
        if (local_ret != ESP_OK)
        {
            ESP_LOGW(TAG, "本地跌倒告警失败: 窗口序号=%u 错误=%s",
                     (unsigned)event->window_sequence,
                     esp_err_to_name(local_ret));
        }

        esp_err_t app_ret = fall_detection_post_app_alert(event);
        if (app_ret != ESP_OK)
        {
            ESP_LOGW(TAG, "跌倒App上传失败: 窗口序号=%u 错误=%s",
                     (unsigned)event->window_sequence,
                     esp_err_to_name(app_ret));
        }
        else
        {
            ESP_LOGI(TAG,
                     "跌倒App上传已入队: 告警序号=%u 窗口序号=%u 重试=0",
                     (unsigned)event->alert_sequence,
                     (unsigned)event->window_sequence);
        }

        ESP_LOGI(TAG,
                 "跌倒危险语言播发已跳过: 告警序号=%u 窗口序号=%u 默认保留App上传",
                 (unsigned)event->alert_sequence,
                 (unsigned)event->window_sequence);
        fall_detection_store_alert_error(local_ret != ESP_OK ? local_ret
                                                             : app_ret);
    }
    else if (event->type == FALL_DETECTION_ALERT_EVENT_CLEARED)
    {
        esp_err_t clear_ret =
            app_alert_manager_clear(APP_ALERT_SOURCE_FALL_DETECTION);
        if (clear_ret != ESP_OK)
        {
            ESP_LOGW(TAG, "跌倒告警清除失败: 窗口序号=%u 错误=%s",
                     (unsigned)event->window_sequence,
                     esp_err_to_name(clear_ret));
        }
        fall_detection_store_alert_error(clear_ret);
        ESP_LOGI(TAG,
                 "跌倒告警已清除: 告警序号=%u 窗口序号=%u 低风险窗口数=%u 跌倒概率=%.4f 清除阈值=%.2f",
                 (unsigned)event->alert_sequence,
                 (unsigned)event->window_sequence,
                 (unsigned)event->clear_window_count,
                 (double)event->fall_prob,
                 (double)k_fall_clear_threshold);
    }
}

static void fall_detection_task(void *arg)
{
    (void)arg;

    while (1)
    {
        if (xQueueReceive(s_fall_detection.window_queue,
                          s_fall_detection.current_window,
                          portMAX_DELAY) != pdTRUE)
        {
            continue;
        }
        const imu_service_accel_window_t *window =
            s_fall_detection.current_window;
        fall_detection_store_window_received(window);

        if (window->frame_count !=
                IMU_SERVICE_WINDOW_FRAME_COUNT ||
            window->sample_rate_hz !=
                IMU_SERVICE_SAMPLE_RATE_HZ ||
            window->trigger_frame_index !=
                IMU_SERVICE_EVENT_PRE_FRAMES)
        {
            ESP_LOGW(TAG,
                     "事件窗口契约不匹配: 序号=%u 帧数=%u 采样率=%u 触发帧=%u",
                     (unsigned)window->sequence,
                     (unsigned)window->frame_count,
                     (unsigned)window->sample_rate_hz,
                     (unsigned)window->trigger_frame_index);
            fall_detection_store_inference_error(ESP_ERR_INVALID_SIZE);
            continue;
        }

        fall_detection_fill_model_input(window,
                                        s_fall_detection.model_input);

        fall_model_result_t result = {0};
        const esp_err_t ret = fall_model_runner_run(
            s_fall_detection.runner, s_fall_detection.model_input, &result);
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "推理失败: 窗口序号=%u 错误=%s",
                     (unsigned)window->sequence,
                     esp_err_to_name(ret));
            fall_detection_store_inference_error(ret);
            continue;
        }

        fall_detection_store_result(window, &result);
        ESP_LOGI(TAG,
                 "跌倒表 | 窗口=%-4u 来源采样=%-6u 触发采样=%-6u flags=0x%02x | 判定=%s(%d) | 置信度=%.4f 日常=%.4f 跌倒=%.4f 阈值=%.2f | 推理_ms=%6.2f 触发_us=%lld 结束_us=%lld",
                 (unsigned)window->sequence,
                 (unsigned)window->source_sample_count,
                 (unsigned)window->trigger_sample_count,
                 (unsigned)window->trigger_flags,
                 fall_model_label_name(result.label_index),
                 result.label_index,
                 (double)result.confidence,
                 (double)result.adl_prob,
                 (double)result.fall_prob,
                 (double)FALL_MODEL_THRESHOLD_DEFAULT,
                 (double)result.infer_us / 1000.0,
                 (long long)window->trigger_time_us,
                 (long long)window->end_time_us);

        const fall_detection_alert_event_t alert_event =
            fall_detection_update_alert_state(window, &result);
        fall_detection_handle_alert_event(&alert_event);
    }
}

esp_err_t fall_detection_service_start(void)
{
    taskENTER_CRITICAL(&s_fall_detection.lock);
    const bool already_started = s_fall_detection.started;
    taskEXIT_CRITICAL(&s_fall_detection.lock);
    if (already_started)
    {
        return ESP_OK;
    }

    fall_detection_store_state(FALL_DETECTION_SERVICE_STATE_STARTING, ESP_OK);

    esp_err_t ret = fall_detection_prepare_buffers();
    if (ret != ESP_OK)
    {
        fall_detection_store_state(FALL_DETECTION_SERVICE_STATE_ERROR, ret);
        return ret;
    }

    QueueHandle_t queue = fall_detection_prepare_window_queue();
    if (queue == NULL)
    {
        fall_detection_store_state(FALL_DETECTION_SERVICE_STATE_ERROR,
                                   ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }

    ret = fall_model_runner_create(
        &s_fall_detection.runner,
        cnn_c24_pool225_do015_e80_with_test_espdl,
        k_model_name);
    if (ret != ESP_OK)
    {
        fall_detection_store_state(FALL_DETECTION_SERVICE_STATE_ERROR, ret);
        return ret;
    }

    ret = fall_model_runner_self_test(s_fall_detection.runner);
    if (ret != ESP_OK)
    {
        fall_model_runner_destroy(s_fall_detection.runner);
        s_fall_detection.runner = NULL;
        fall_detection_store_state(FALL_DETECTION_SERVICE_STATE_ERROR, ret);
        return ret;
    }
    fall_detection_store_model_ready(true);

    ret = imu_service_set_window_queue(queue);
    if (ret != ESP_OK)
    {
        fall_model_runner_destroy(s_fall_detection.runner);
        s_fall_detection.runner = NULL;
        fall_detection_store_state(FALL_DETECTION_SERVICE_STATE_ERROR, ret);
        return ret;
    }

    const BaseType_t created = xTaskCreateWithCaps(
        fall_detection_task,
        "fall_detect",
        k_task_stack_bytes,
        NULL,
        k_task_priority,
        &s_fall_detection.task_handle,
        MALLOC_CAP_SPIRAM);
    if (created != pdPASS)
    {
        (void)imu_service_set_window_queue(NULL);
        fall_model_runner_destroy(s_fall_detection.runner);
        s_fall_detection.runner = NULL;
        s_fall_detection.task_handle = NULL;
        fall_detection_store_state(FALL_DETECTION_SERVICE_STATE_ERROR,
                                   ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }

    taskENTER_CRITICAL(&s_fall_detection.lock);
    s_fall_detection.started = true;
    s_fall_detection.snapshot.state = FALL_DETECTION_SERVICE_STATE_RUNNING;
    s_fall_detection.snapshot.last_error = ESP_OK;
    taskEXIT_CRITICAL(&s_fall_detection.lock);

    ESP_LOGI(TAG, "已启动: 模型=%s 阈值=%.2f 队列长度=%u",
             k_model_name,
             (double)FALL_MODEL_THRESHOLD_DEFAULT,
             (unsigned)k_window_queue_length);
    return ESP_OK;
}

esp_err_t fall_detection_service_get_snapshot(fall_detection_service_snapshot_t *out)
{
    if (out == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    taskENTER_CRITICAL(&s_fall_detection.lock);
    *out = s_fall_detection.snapshot;
    taskEXIT_CRITICAL(&s_fall_detection.lock);
    return ESP_OK;
}

const char *fall_detection_service_state_text(fall_detection_service_state_t state)
{
    switch (state)
    {
    case FALL_DETECTION_SERVICE_STATE_STOPPED:
        return "stopped";
    case FALL_DETECTION_SERVICE_STATE_STARTING:
        return "starting";
    case FALL_DETECTION_SERVICE_STATE_RUNNING:
        return "running";
    case FALL_DETECTION_SERVICE_STATE_ERROR:
        return "error";
    default:
        return "unknown";
    }
}
