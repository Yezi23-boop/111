#include "danger_detection_service.h"

#include <stdbool.h>
#include <string.h>

#include "features/alerts/app_alert_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "traffic_audio_runtime.h"
#include "traffic_inference_postprocess.h"

#define TAG "danger_detection"
#define DANGER_DETECTION_STOP_TIMEOUT_MS 2000U /* 默认停止等待时间，单位为毫秒。 */

/*
 * 危险检测服务实现说明：
 * - 统一协调交通声音运行时、后处理告警回调和应用级告警管理器；
 * - 对外发布快照时，会把运行时状态与后处理分数整合到同一结构；
 * - 快照通过临界区保护，避免 UI 在读取时看到半更新状态。
 */

typedef struct
{
    bool initialized;                     /**< 服务是否完成初始化。 */
    bool callback_registered;             /**< 后处理告警回调是否已注册。 */
    bool runtime_started;                 /**< 音频运行时是否已启动。 */
    danger_detection_snapshot_t snapshot; /**< 对外发布的快照。 */
    portMUX_TYPE lock;                    /**< 快照临界区锁，保护共享状态一致性。 */
} danger_detection_service_state_t;

static danger_detection_service_state_t s_service_state = {
    .initialized = false,
    .callback_registered = false,
    .runtime_started = false,
    .snapshot = {
        .state = DANGER_DETECTION_STATE_IDLE,
        .stable_label = DANGER_DETECTION_LABEL_NONE,
        .last_detected_label = DANGER_DETECTION_LABEL_NONE,
        .last_detected_confidence = 0.0f,
        .horn_confidence = 0.0f,
        .siren_confidence = 0.0f,
        .alert_sequence = 0U,
        .last_error = ESP_OK,
        .danger_overlay_active = false,
    },
    .lock = portMUX_INITIALIZER_UNLOCKED,
};

/**
 * @brief 将后处理稳定标签映射成服务层标签。
 * @param[in] label 后处理稳定标签。
 * @return 服务层对外标签。
 */
static danger_detection_label_t danger_detection_map_label(
    traffic_inference_postprocess_stable_label_t label)
{
    switch (label)
    {
    case TRAFFIC_INFERENCE_POSTPROCESS_STABLE_LABEL_HORN:
        return DANGER_DETECTION_LABEL_HORN;
    case TRAFFIC_INFERENCE_POSTPROCESS_STABLE_LABEL_SIREN:
        return DANGER_DETECTION_LABEL_SIREN;
    case TRAFFIC_INFERENCE_POSTPROCESS_STABLE_LABEL_NONE:
    default:
        return DANGER_DETECTION_LABEL_NONE;
    }
}

/**
 * @brief 将运行时状态映射成服务层状态。
 *
 * 对于底层未提供明确语义的状态，保留调用方传入的回退状态，
 * 以避免采样链路短暂波动时把上层状态意外重置。
 *
 * @param[in] runtime_state 底层运行时状态。
 * @param[in] fallback 无明确映射时保留的服务层状态。
 * @return 服务层状态枚举。
 */
static danger_detection_state_t danger_detection_map_runtime_state(
    traffic_audio_runtime_state_t runtime_state,
    danger_detection_state_t fallback)
{
    switch (runtime_state)
    {
    case TRAFFIC_AUDIO_RUNTIME_STATE_STARTING:
        return DANGER_DETECTION_STATE_STARTING;
    case TRAFFIC_AUDIO_RUNTIME_STATE_RUNNING:
        return DANGER_DETECTION_STATE_RUNNING;
    case TRAFFIC_AUDIO_RUNTIME_STATE_STOPPING:
        return DANGER_DETECTION_STATE_STOPPING;
    case TRAFFIC_AUDIO_RUNTIME_STATE_FAILED:
        return DANGER_DETECTION_STATE_ERROR;
    case TRAFFIC_AUDIO_RUNTIME_STATE_IDLE:
    default:
        return fallback;
    }
}

/**
 * @brief 统一更新服务状态和最近错误码。
 * @param[in] state 新的服务状态。
 * @param[in] last_error 最近错误码。
 * @return 无返回值。
 */
static void danger_detection_set_state(danger_detection_state_t state,
                                       esp_err_t last_error)
{
    taskENTER_CRITICAL(&s_service_state.lock);
    s_service_state.snapshot.state = state;
    s_service_state.snapshot.last_error = last_error;
    taskEXIT_CRITICAL(&s_service_state.lock);
}

/**
 * @brief 后处理告警回调。
 * @param[in] alert 后处理上报的告警动作。
 * @param[in] user_data 未使用。
 * @return 无返回值。
 *
 * RAISE 会触发应用级告警并更新快照；
 * CLEAR 会撤销应用级告警并把稳定标签恢复为 NONE。
 *
 * @note 回调可能由底层运行时线程触发，因此共享快照更新必须走临界区。
 */
static void danger_detection_on_alert(
    const traffic_inference_postprocess_alert_t *alert,
    void *user_data)
{
    (void)user_data;

    if (alert == NULL)
    {
        return;
    }

    if (alert->action == TRAFFIC_INFERENCE_POSTPROCESS_ALERT_ACTION_RAISE)
    {
        /* RAISE 要同步推进应用级告警和本地快照，避免 UI 与提示音状态脱节。 */
        app_alert_request_t request = {
            .source = APP_ALERT_SOURCE_TRAFFIC_AUDIO,
            .severity = APP_ALERT_SEVERITY_DANGER,
            .label = alert->label ==
                             TRAFFIC_INFERENCE_POSTPROCESS_STABLE_LABEL_HORN
                         ? APP_ALERT_LABEL_HORN
                         : APP_ALERT_LABEL_SIREN,
        };
        (void)app_alert_manager_raise(&request);

        taskENTER_CRITICAL(&s_service_state.lock);
        s_service_state.snapshot.state = DANGER_DETECTION_STATE_RUNNING;
        s_service_state.snapshot.stable_label =
            danger_detection_map_label(alert->label);
        s_service_state.snapshot.last_detected_label =
            danger_detection_map_label(alert->label);
        s_service_state.snapshot.last_detected_confidence =
            alert->confidence_score;
        s_service_state.snapshot.alert_sequence += 1U;
        s_service_state.snapshot.danger_overlay_active = true;
        taskEXIT_CRITICAL(&s_service_state.lock);
    }
    else if (alert->action ==
             TRAFFIC_INFERENCE_POSTPROCESS_ALERT_ACTION_CLEAR)
    {
        /* CLEAR 只撤销当前告警表现，不重置整个服务生命周期状态。 */
        (void)app_alert_manager_clear(APP_ALERT_SOURCE_TRAFFIC_AUDIO);

        taskENTER_CRITICAL(&s_service_state.lock);
        s_service_state.snapshot.state = DANGER_DETECTION_STATE_RUNNING;
        s_service_state.snapshot.stable_label = DANGER_DETECTION_LABEL_NONE;
        s_service_state.snapshot.danger_overlay_active = false;
        taskEXIT_CRITICAL(&s_service_state.lock);
    }
}

/**
 * @brief 初始化危险检测服务。
 * @return `ESP_OK` 表示初始化成功或已初始化；其他错误表示依赖模块初始化失败。
 */
esp_err_t danger_detection_service_init(void)
{
    if (s_service_state.initialized)
    {
        return ESP_OK;
    }

    esp_err_t ret = app_alert_manager_init();
    if (ret != ESP_OK)
    {
        danger_detection_set_state(DANGER_DETECTION_STATE_ERROR, ret);
        return ret;
    }

    taskENTER_CRITICAL(&s_service_state.lock);
    s_service_state.initialized = true;
    s_service_state.snapshot.state = DANGER_DETECTION_STATE_IDLE;
    s_service_state.snapshot.stable_label = DANGER_DETECTION_LABEL_NONE;
    s_service_state.snapshot.last_detected_label = DANGER_DETECTION_LABEL_NONE;
    s_service_state.snapshot.last_detected_confidence = 0.0f;
    s_service_state.snapshot.horn_confidence = 0.0f;
    s_service_state.snapshot.siren_confidence = 0.0f;
    s_service_state.snapshot.alert_sequence = 0U;
    s_service_state.snapshot.last_error = ESP_OK;
    s_service_state.snapshot.danger_overlay_active = false;
    taskEXIT_CRITICAL(&s_service_state.lock);
    return ESP_OK;
}

/**
 * @brief 启动危险检测运行时。
 * @return 若运行时已在运行，则直接返回 `ESP_OK`；其他错误表示回调注册或运行时启动失败。
 */
esp_err_t danger_detection_service_start(void)
{
    esp_err_t ret = danger_detection_service_init();
    if (ret != ESP_OK)
    {
        return ret;
    }

    taskENTER_CRITICAL(&s_service_state.lock);
    const bool already_running = s_service_state.runtime_started;
    taskEXIT_CRITICAL(&s_service_state.lock);
    if (already_running)
    {
        return ESP_OK;
    }

    traffic_audio_runtime_config_t config = {
        .input_chunk_frames = 0U,
        .read_timeout_ms = 250U,  /* 音频读取超时阈值，单位为毫秒。 */
        .task_stack_size = 8192U, /* 运行时任务栈大小，单位为字节。 */
        .task_priority = 5U,      /* 运行时任务优先级。 */
    };

    danger_detection_set_state(DANGER_DETECTION_STATE_STARTING, ESP_OK);
    taskENTER_CRITICAL(&s_service_state.lock);
    s_service_state.snapshot.stable_label = DANGER_DETECTION_LABEL_NONE;
    s_service_state.snapshot.last_detected_label = DANGER_DETECTION_LABEL_NONE;
    s_service_state.snapshot.last_detected_confidence = 0.0f;
    s_service_state.snapshot.horn_confidence = 0.0f;
    s_service_state.snapshot.siren_confidence = 0.0f;
    s_service_state.snapshot.alert_sequence = 0U;
    s_service_state.snapshot.danger_overlay_active = false;
    taskEXIT_CRITICAL(&s_service_state.lock);

    ret = traffic_inference_postprocess_set_alert_callback(
        danger_detection_on_alert, NULL);
    if (ret != ESP_OK)
    {
        danger_detection_set_state(DANGER_DETECTION_STATE_ERROR, ret);
        return ret;
    }

    taskENTER_CRITICAL(&s_service_state.lock);
    s_service_state.callback_registered = true;
    taskEXIT_CRITICAL(&s_service_state.lock);

    ret = traffic_audio_runtime_start(&config);
    if (ret != ESP_OK)
    {
        (void)traffic_inference_postprocess_set_alert_callback(NULL, NULL);
        taskENTER_CRITICAL(&s_service_state.lock);
        s_service_state.callback_registered = false;
        taskEXIT_CRITICAL(&s_service_state.lock);
        danger_detection_set_state(DANGER_DETECTION_STATE_ERROR, ret);
        return ret;
    }

    taskENTER_CRITICAL(&s_service_state.lock);
    s_service_state.runtime_started = true;
    taskEXIT_CRITICAL(&s_service_state.lock);
    ESP_LOGI(TAG, "danger detection runtime started");
    return ESP_OK;
}

/**
 * @brief 停止危险检测运行时。
 * @param[in] timeout_ms 停止运行时允许等待的超时，单位为毫秒；传 0 使用默认值。
 * @return 停止底层运行时或注销回调失败时返回错误。
 */
esp_err_t danger_detection_service_stop(uint32_t timeout_ms)
{
    if (!s_service_state.initialized)
    {
        return ESP_OK;
    }

    taskENTER_CRITICAL(&s_service_state.lock);
    const bool runtime_started = s_service_state.runtime_started;
    const bool callback_registered = s_service_state.callback_registered;
    taskEXIT_CRITICAL(&s_service_state.lock);

    if (!runtime_started && !callback_registered)
    {
        danger_detection_set_state(DANGER_DETECTION_STATE_IDLE, ESP_OK);
        return ESP_OK;
    }

    danger_detection_set_state(DANGER_DETECTION_STATE_STOPPING, ESP_OK);
    (void)app_alert_manager_clear(APP_ALERT_SOURCE_TRAFFIC_AUDIO);

    esp_err_t ret = ESP_OK;
    if (runtime_started)
    {
        ret = traffic_audio_runtime_stop(timeout_ms == 0U
                                             ? DANGER_DETECTION_STOP_TIMEOUT_MS
                                             : timeout_ms);
    }

    if (callback_registered)
    {
        esp_err_t clear_ret =
            traffic_inference_postprocess_set_alert_callback(NULL, NULL);
        if (ret == ESP_OK && clear_ret != ESP_OK)
        {
            ret = clear_ret;
        }
    }

    taskENTER_CRITICAL(&s_service_state.lock);
    s_service_state.callback_registered = false;
    s_service_state.runtime_started = false;
    s_service_state.snapshot.stable_label = DANGER_DETECTION_LABEL_NONE;
    s_service_state.snapshot.last_detected_label = DANGER_DETECTION_LABEL_NONE;
    s_service_state.snapshot.last_detected_confidence = 0.0f;
    s_service_state.snapshot.horn_confidence = 0.0f;
    s_service_state.snapshot.siren_confidence = 0.0f;
    s_service_state.snapshot.alert_sequence = 0U;
    s_service_state.snapshot.danger_overlay_active = false;
    taskEXIT_CRITICAL(&s_service_state.lock);

    if (ret != ESP_OK)
    {
        danger_detection_set_state(DANGER_DETECTION_STATE_ERROR, ret);
        return ret;
    }

    danger_detection_set_state(DANGER_DETECTION_STATE_IDLE, ESP_OK);
    ESP_LOGI(TAG, "danger detection runtime stopped");
    return ESP_OK;
}

/**
 * @brief 获取当前危险检测快照。
 * @return 线程安全复制出的快照值。
 *
 * 若运行时仍在运行，会额外拉取最新运行时状态和 horn/siren 分数补全快照。
 *
 * @note 函数先复制共享快照，再在锁外查询运行时细节，避免长时间占用临界区。
 */
danger_detection_snapshot_t danger_detection_service_get_snapshot(void)
{
    danger_detection_snapshot_t snapshot;
    bool runtime_started = false;

    taskENTER_CRITICAL(&s_service_state.lock);
    snapshot = s_service_state.snapshot;
    runtime_started = s_service_state.runtime_started;
    taskEXIT_CRITICAL(&s_service_state.lock);

    if (runtime_started)
    {
        traffic_audio_runtime_state_t runtime_state =
            traffic_audio_runtime_get_state();
        const traffic_inference_postprocess_snapshot_t postprocess_snapshot =
            traffic_inference_postprocess_get_latest_snapshot();
        snapshot.state = danger_detection_map_runtime_state(runtime_state,
                                                            snapshot.state);
        snapshot.horn_confidence = postprocess_snapshot.horn_score;
        snapshot.siren_confidence = postprocess_snapshot.siren_score;
        if (runtime_state == TRAFFIC_AUDIO_RUNTIME_STATE_FAILED &&
            snapshot.last_error == ESP_OK)
        {
            snapshot.last_error = ESP_FAIL;
        }
    }

    return snapshot;
}
