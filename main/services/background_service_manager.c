#include "services/background_service_manager.h"

#include "audio_codec.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "services/power_policy.h"
#include "services/safety_monitor_session.h"
#include "services/startup_readiness.h"

static const char *TAG = "background_mgr";
static const TickType_t k_policy_poll_ticks = pdMS_TO_TICKS(1000);

typedef struct
{
    bool initialized;                  /**< 是否已初始化依赖模块。 */
    bool started;                      /**< 后台任务是否已启动。 */
    bool danger_enabled_by_user;       /**< 用户是否允许危险识别后台运行。 */
    bool danger_allowed_by_policy;     /**< 最近一次策略是否允许危险识别。 */
    bool danger_runtime_running;       /**< 最近一次命令确认的运行状态。 */
    bool foreground_audio_active;      /**< 前台录音/语音是否正在占用麦克风。 */
    bool danger_blocked_by_foreground_audio; /**< 当前麦克风 owner 是否阻塞危险识别。 */
    power_policy_state_t policy_state; /**< 最近一次策略状态。 */
    esp_err_t last_error;              /**< 最近一次 session 启动、停止或恢复错误码。 */
    TaskHandle_t task_handle;          /**< 后台策略轮询任务。 */
    portMUX_TYPE lock;                 /**< 保护管理器快照。 */
} background_service_manager_state_t;

static background_service_manager_state_t s_manager = {
    .initialized = false,
    .started = false,
    .danger_enabled_by_user = false,
    .danger_allowed_by_policy = true,
    .danger_runtime_running = false,
    .foreground_audio_active = false,
    .danger_blocked_by_foreground_audio = false,
    .policy_state = POWER_POLICY_STATE_ACTIVE,
    .last_error = ESP_OK,
    .task_handle = NULL,
    .lock = portMUX_INITIALIZER_UNLOCKED,
};

static bool background_service_manager_input_owner_blocks_danger(
    audio_codec_owner_t owner)
{
    switch (owner)
    {
    case AUDIO_CODEC_OWNER_SYSTEM:
    case AUDIO_CODEC_OWNER_ESPDL_INFERENCE:
        return false;
    case AUDIO_CODEC_OWNER_TRAFFIC_INFERENCE:
    case AUDIO_CODEC_OWNER_AUDIO_PLAYER:
    case AUDIO_CODEC_OWNER_AUDIO_RECORDER:
    case AUDIO_CODEC_OWNER_OFFICIAL_CHAT:
    default:
        return true;
    }
}

static bool background_service_manager_audio_blocks_danger(
    bool explicit_foreground_audio, const char **owner_text)
{
    if (owner_text != NULL)
    {
        *owner_text = NULL;
    }

    if (explicit_foreground_audio)
    {
        if (owner_text != NULL)
        {
            *owner_text = "foreground_audio";
        }
        return true;
    }

    audio_codec_session_snapshot_t audio_snapshot = {0};
    esp_err_t ret = audio_codec_get_session_snapshot(&audio_snapshot);
    if (ret != ESP_OK || !audio_snapshot.input_active)
    {
        return false;
    }

    if (!background_service_manager_input_owner_blocks_danger(
            audio_snapshot.input_owner))
    {
        return false;
    }

    if (owner_text != NULL)
    {
        *owner_text = audio_codec_owner_to_text(audio_snapshot.input_owner);
    }
    return true;
}

/**
 * @brief 根据最新策略和用户开关同步危险识别后台运行状态。
 *
 * 该函数是第一阶段的核心闭环：页面不再拥有 start/stop 生命周期，
 * 后台管理器根据用户开关和资源预算决定是否运行危险识别。
 */
static esp_err_t background_service_manager_apply_policy(const char *reason)
{
    const power_policy_budget_t budget = power_policy_get_budget();
    const safety_monitor_session_snapshot_t before_session =
        safety_monitor_session_get_snapshot();

    bool user_enabled = true;
    bool explicit_foreground_audio = false;
    bool previous_allowed = true;
    bool previous_running = false;
    bool previous_audio_blocked = false;

    taskENTER_CRITICAL(&s_manager.lock);
    user_enabled = s_manager.danger_enabled_by_user;
    explicit_foreground_audio = s_manager.foreground_audio_active;
    previous_allowed = s_manager.danger_allowed_by_policy;
    previous_running = s_manager.danger_runtime_running;
    previous_audio_blocked = s_manager.danger_blocked_by_foreground_audio;
    s_manager.policy_state = budget.state;
    s_manager.danger_allowed_by_policy = budget.danger_detection_allowed;
    s_manager.danger_runtime_running = before_session.runtime_running;
    s_manager.last_error = before_session.last_error;
    taskEXIT_CRITICAL(&s_manager.lock);

    const char *blocking_audio_owner = NULL;
    const bool foreground_audio_blocked =
        background_service_manager_audio_blocks_danger(
            explicit_foreground_audio, &blocking_audio_owner);

    taskENTER_CRITICAL(&s_manager.lock);
    s_manager.danger_blocked_by_foreground_audio = foreground_audio_blocked;
    taskEXIT_CRITICAL(&s_manager.lock);

    if (previous_allowed != budget.danger_detection_allowed)
    {
        ESP_LOGI(TAG,
                 "background_allowed_change: danger=%d policy=%s reason=%s",
                 budget.danger_detection_allowed,
                 power_policy_state_text(budget.state),
                 reason != NULL ? reason : "unknown");
    }

    if (previous_audio_blocked != foreground_audio_blocked)
    {
        ESP_LOGI(TAG,
                 "resource_blocked_change: resource=mic danger=%d owner=%s reason=%s",
                 foreground_audio_blocked ? 1 : 0,
                 blocking_audio_owner != NULL ? blocking_audio_owner : "none",
                 reason != NULL ? reason : "unknown");
    }

    const bool should_run = user_enabled && budget.danger_detection_allowed &&
                            !foreground_audio_blocked;
    esp_err_t ret = safety_monitor_session_apply(should_run, reason);
    const safety_monitor_session_snapshot_t after_session =
        safety_monitor_session_get_snapshot();

    taskENTER_CRITICAL(&s_manager.lock);
    s_manager.last_error = after_session.last_error;
    s_manager.danger_runtime_running = after_session.runtime_running;
    taskEXIT_CRITICAL(&s_manager.lock);

    if (previous_running != after_session.runtime_running)
    {
        ESP_LOGI(TAG, "background danger runtime observed: running=%d",
                 after_session.runtime_running);
    }

    return ret;
}

static void background_service_manager_task(void *arg)
{
    (void)arg;

    /*
     * 后台 Safety Monitor session 可能加载模型并占用麦克风、PSRAM 和
     * internal/DMA 相关资源；必须等 UI 首帧真实完成后再进入策略循环，
     * 避免用固定延时猜测 Display Foundation / UI First Frame 边界。
     */
    ESP_LOGI(TAG, "background_gate_wait: ui_first_frame_ready");
    (void)startup_readiness_wait_ui_first_frame(portMAX_DELAY);
    ESP_LOGI(TAG, "background_gate_ready: ui_first_frame_ready");

    while (1)
    {
        (void)background_service_manager_apply_policy("periodic");
        vTaskDelay(k_policy_poll_ticks);
    }
}

esp_err_t background_service_manager_init(void)
{
    if (s_manager.initialized)
    {
        return ESP_OK;
    }

    esp_err_t ret = power_policy_init();
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = startup_readiness_init();
    if (ret != ESP_OK)
    {
        taskENTER_CRITICAL(&s_manager.lock);
        s_manager.last_error = ret;
        taskEXIT_CRITICAL(&s_manager.lock);
        return ret;
    }

    ret = safety_monitor_session_init();
    if (ret != ESP_OK)
    {
        taskENTER_CRITICAL(&s_manager.lock);
        s_manager.last_error = ret;
        taskEXIT_CRITICAL(&s_manager.lock);
        return ret;
    }

    taskENTER_CRITICAL(&s_manager.lock);
    s_manager.initialized = true;
    s_manager.last_error = ESP_OK;
    taskEXIT_CRITICAL(&s_manager.lock);
    return ESP_OK;
}

esp_err_t background_service_manager_start(void)
{
    esp_err_t ret = background_service_manager_init();
    if (ret != ESP_OK)
    {
        return ret;
    }

    taskENTER_CRITICAL(&s_manager.lock);
    const bool already_started = s_manager.started;
    taskEXIT_CRITICAL(&s_manager.lock);
    if (already_started)
    {
        return ESP_OK;
    }

    BaseType_t ok = xTaskCreate(background_service_manager_task,
                                "background_mgr",
                                4096,
                                NULL,
                                4,
                                &s_manager.task_handle);
    if (ok != pdPASS)
    {
        s_manager.task_handle = NULL;
        /*
         * 管理器任务没起来时不能留下无人托管的后台监听，
         * 否则后续用户开关和策略预算都无法继续生效。
         */
        (void)safety_monitor_session_apply(false, "manager_start_failed");
        taskENTER_CRITICAL(&s_manager.lock);
        s_manager.danger_runtime_running = false;
        taskEXIT_CRITICAL(&s_manager.lock);
        return ESP_FAIL;
    }

    taskENTER_CRITICAL(&s_manager.lock);
    s_manager.started = true;
    taskEXIT_CRITICAL(&s_manager.lock);

    ESP_LOGI(TAG, "background service manager started");
    return ESP_OK;
}

esp_err_t background_service_manager_set_danger_detection_enabled(bool enabled)
{
    esp_err_t ret = background_service_manager_init();
    if (ret != ESP_OK)
    {
        return ret;
    }

    taskENTER_CRITICAL(&s_manager.lock);
    const bool changed = s_manager.danger_enabled_by_user != enabled;
    s_manager.danger_enabled_by_user = enabled;
    taskEXIT_CRITICAL(&s_manager.lock);

    if (changed)
    {
        ESP_LOGI(TAG, "safety_monitor_session: enabled_by_user=%d", enabled);
    }

    taskENTER_CRITICAL(&s_manager.lock);
    const bool manager_started = s_manager.started;
    taskEXIT_CRITICAL(&s_manager.lock);
    if (!manager_started)
    {
        /*
         * 页面可以更新用户开关，但不能在管理器任务未启动时拉起无人托管的
         * 后台监听。app_main 会负责启动真正的周期 owner。
         */
        return ESP_ERR_INVALID_STATE;
    }

    return background_service_manager_apply_policy("user_switch");
}

esp_err_t background_service_manager_set_foreground_audio_active(
    bool active, const char *reason)
{
    esp_err_t ret = background_service_manager_init();
    if (ret != ESP_OK)
    {
        return ret;
    }

    taskENTER_CRITICAL(&s_manager.lock);
    const bool changed = s_manager.foreground_audio_active != active;
    s_manager.foreground_audio_active = active;
    const bool manager_started = s_manager.started;
    taskEXIT_CRITICAL(&s_manager.lock);

    if (changed)
    {
        ESP_LOGI(TAG, "foreground_audio_active: active=%d reason=%s",
                 active, reason != NULL ? reason : "unknown");
    }

    if (!manager_started)
    {
        return ESP_OK;
    }

    return background_service_manager_apply_policy(
        reason != NULL ? reason : "foreground_audio");
}

background_service_manager_snapshot_t
background_service_manager_get_snapshot(void)
{
    background_service_manager_snapshot_t snapshot;

    taskENTER_CRITICAL(&s_manager.lock);
    snapshot.started = s_manager.started;
    snapshot.danger_enabled_by_user = s_manager.danger_enabled_by_user;
    snapshot.danger_allowed_by_policy = s_manager.danger_allowed_by_policy;
    snapshot.danger_runtime_running = s_manager.danger_runtime_running;
    snapshot.danger_blocked_by_foreground_audio =
        s_manager.danger_blocked_by_foreground_audio;
    snapshot.policy_state = s_manager.policy_state;
    snapshot.last_error = s_manager.last_error;
    taskEXIT_CRITICAL(&s_manager.lock);

    return snapshot;
}
