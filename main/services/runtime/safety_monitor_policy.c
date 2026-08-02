#include "services/runtime/safety_monitor_policy.h"

#include "audio_codec.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "services/runtime/runtime_coordinator.h"
#include "services/runtime/startup_readiness.h"
#include "services/safety/safety_monitor_session.h"

static const char *TAG = "safety_policy";
static const TickType_t kPolicyPollTicks = pdMS_TO_TICKS(1000);

typedef enum
{
    SAFETY_MONITOR_POLICY_NOTIFY_NONE = 0,
    SAFETY_MONITOR_POLICY_NOTIFY_USER = 1U << 0,
    SAFETY_MONITOR_POLICY_NOTIFY_AUDIO = 1U << 1,
    SAFETY_MONITOR_POLICY_NOTIFY_POWER = 1U << 2,
    SAFETY_MONITOR_POLICY_NOTIFY_COORDINATOR = 1U << 3,
    SAFETY_MONITOR_POLICY_NOTIFY_STARTUP = 1U << 4,
    SAFETY_MONITOR_POLICY_NOTIFY_PERIODIC = 1U << 5,
    SAFETY_MONITOR_POLICY_NOTIFY_MUSIC = 1U << 6,
} safety_monitor_policy_notify_t;

typedef struct
{
    bool initialized;
    bool started;
    bool enabled_by_user;
    bool allowed_by_power_policy;
    bool should_run;
    bool runtime_running;
    safety_monitor_policy_block_reason_t block_reason;
    bool foreground_audio_active;
    bool music_playback_active;
    bool blocked_by_runtime_coordinator;
    power_policy_state_t policy_state;
    uint32_t policy_flags;
    uint32_t pending_quiesce_generation;
    TaskHandle_t task_handle;
    portMUX_TYPE lock;
} safety_monitor_policy_context_t;

static safety_monitor_policy_context_t s_policy = {
    .allowed_by_power_policy = true,
    .block_reason = SAFETY_MONITOR_POLICY_BLOCK_NOT_READY,
    .policy_state = POWER_POLICY_STATE_ACTIVE,
    .lock = portMUX_INITIALIZER_UNLOCKED,
};

static const char *safety_monitor_policy_notify_text(uint32_t reasons)
{
    if ((reasons & SAFETY_MONITOR_POLICY_NOTIFY_USER) != 0U)
    {
        return "user";
    }
    if ((reasons & SAFETY_MONITOR_POLICY_NOTIFY_AUDIO) != 0U)
    {
        return "foreground_audio";
    }
    if ((reasons & SAFETY_MONITOR_POLICY_NOTIFY_MUSIC) != 0U)
    {
        return "music_playback";
    }
    if ((reasons & SAFETY_MONITOR_POLICY_NOTIFY_COORDINATOR) != 0U)
    {
        return "runtime_coordinator";
    }
    if ((reasons & SAFETY_MONITOR_POLICY_NOTIFY_POWER) != 0U)
    {
        return "power";
    }
    if ((reasons & SAFETY_MONITOR_POLICY_NOTIFY_STARTUP) != 0U)
    {
        return "startup";
    }
    return "periodic";
}

static const char *safety_monitor_policy_block_text(
    safety_monitor_policy_block_reason_t reason)
{
    switch (reason)
    {
    case SAFETY_MONITOR_POLICY_BLOCK_NONE:
        return "none";
    case SAFETY_MONITOR_POLICY_BLOCK_NOT_READY:
        return "not_ready";
    case SAFETY_MONITOR_POLICY_BLOCK_USER_DISABLED:
        return "user_disabled";
    case SAFETY_MONITOR_POLICY_BLOCK_POWER:
        return "power";
    case SAFETY_MONITOR_POLICY_BLOCK_FOREGROUND_AUDIO:
        return "foreground_audio";
    case SAFETY_MONITOR_POLICY_BLOCK_RUNTIME_COORDINATOR:
        return "runtime_coordinator";
    case SAFETY_MONITOR_POLICY_BLOCK_MUSIC_PLAYBACK:
        return "music_playback";
    default:
        return "unknown";
    }
}

static bool safety_monitor_policy_input_owner_blocks(audio_codec_owner_t owner)
{
    switch (owner)
    {
    case AUDIO_CODEC_OWNER_OFFICIAL_CHAT:
    case AUDIO_CODEC_OWNER_HERMES:
    case AUDIO_CODEC_OWNER_AUDIO_RECORDER:
        return true;
    default:
        return false;
    }
}

static bool safety_monitor_policy_audio_blocks(
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

    audio_codec_session_snapshot_t snapshot = {0};
    if (audio_codec_get_session_snapshot(&snapshot) != ESP_OK ||
        !snapshot.input_active ||
        !safety_monitor_policy_input_owner_blocks(snapshot.input_owner))
    {
        return false;
    }
    if (owner_text != NULL)
    {
        *owner_text = audio_codec_owner_to_text(snapshot.input_owner);
    }
    return true;
}

static safety_monitor_policy_block_reason_t
safety_monitor_policy_resolve_block(bool enabled, bool power_allowed,
                                    bool audio_blocked,
                                    bool coordinator_blocked,
                                    bool music_playback_active)
{
    if (!enabled)
    {
        return SAFETY_MONITOR_POLICY_BLOCK_USER_DISABLED;
    }
    if (audio_blocked)
    {
        return SAFETY_MONITOR_POLICY_BLOCK_FOREGROUND_AUDIO;
    }
    if (music_playback_active)
    {
        return SAFETY_MONITOR_POLICY_BLOCK_MUSIC_PLAYBACK;
    }
    if (coordinator_blocked)
    {
        return SAFETY_MONITOR_POLICY_BLOCK_RUNTIME_COORDINATOR;
    }
    if (!power_allowed)
    {
        return SAFETY_MONITOR_POLICY_BLOCK_POWER;
    }
    return SAFETY_MONITOR_POLICY_BLOCK_NONE;
}

static esp_err_t safety_monitor_policy_notify(uint32_t reasons)
{
    TaskHandle_t task_handle = NULL;
    taskENTER_CRITICAL(&s_policy.lock);
    task_handle = s_policy.task_handle;
    taskEXIT_CRITICAL(&s_policy.lock);
    if (task_handle == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    xTaskNotify(task_handle, reasons, eSetBits);
    return ESP_OK;
}

static esp_err_t safety_monitor_policy_apply(const char *reason)
{
    const power_policy_budget_t budget = power_policy_get_budget();
    bool enabled = false;
    bool explicit_audio = false;
    bool coordinator_blocked = false;
    bool music_playback_active = false;
    uint32_t quiesce_generation = 0U;

    taskENTER_CRITICAL(&s_policy.lock);
    enabled = s_policy.enabled_by_user;
    explicit_audio = s_policy.foreground_audio_active;
    coordinator_blocked = s_policy.blocked_by_runtime_coordinator;
    music_playback_active = s_policy.music_playback_active;
    quiesce_generation = s_policy.pending_quiesce_generation;
    taskEXIT_CRITICAL(&s_policy.lock);

    const char *audio_owner = NULL;
    const bool audio_blocked =
        safety_monitor_policy_audio_blocks(explicit_audio, &audio_owner);
    const safety_monitor_policy_block_reason_t block_reason =
        safety_monitor_policy_resolve_block(
            enabled, budget.danger_detection_allowed, audio_blocked,
            coordinator_blocked, music_playback_active);
    const bool should_run =
        block_reason == SAFETY_MONITOR_POLICY_BLOCK_NONE;

    esp_err_t ret = safety_monitor_session_apply(should_run, reason);
    const safety_monitor_session_snapshot_t session =
        safety_monitor_session_get_snapshot();

    taskENTER_CRITICAL(&s_policy.lock);
    s_policy.allowed_by_power_policy = budget.danger_detection_allowed;
    s_policy.should_run = should_run;
    s_policy.runtime_running = session.runtime_running;
    s_policy.block_reason = block_reason;
    s_policy.policy_state = budget.state;
    s_policy.policy_flags = budget.flags;
    if (quiesce_generation != 0U && !session.runtime_running)
    {
        s_policy.pending_quiesce_generation = 0U;
    }
    taskEXIT_CRITICAL(&s_policy.lock);

    ESP_LOGD(TAG,
             "apply: should_run=%d block=%s audio_owner=%s reason=%s",
             should_run ? 1 : 0,
             safety_monitor_policy_block_text(block_reason),
             audio_owner != NULL ? audio_owner : "none",
             reason != NULL ? reason : "unknown");

    if (quiesce_generation != 0U && !session.runtime_running)
    {
        const esp_err_t report_ret = runtime_coordinator_report_quiesce_result(
            RUNTIME_COORDINATOR_PARTICIPANT_SAFETY_MONITOR,
            quiesce_generation, ret);
        if (report_ret == ESP_OK)
        {
            taskENTER_CRITICAL(&s_policy.lock);
            if (s_policy.pending_quiesce_generation == quiesce_generation)
            {
                s_policy.pending_quiesce_generation = 0U;
            }
            taskEXIT_CRITICAL(&s_policy.lock);
        }
    }
    return ret;
}

static esp_err_t safety_monitor_policy_request_quiesce(
    uint32_t generation, void *user_ctx)
{
    (void)user_ctx;
    taskENTER_CRITICAL(&s_policy.lock);
    s_policy.blocked_by_runtime_coordinator = true;
    s_policy.pending_quiesce_generation = generation;
    const bool started = s_policy.started;
    const bool running = s_policy.runtime_running;
    taskEXIT_CRITICAL(&s_policy.lock);

    if (!started && !running)
    {
        const esp_err_t ret = runtime_coordinator_report_quiesce_result(
            RUNTIME_COORDINATOR_PARTICIPANT_SAFETY_MONITOR,
            generation, ESP_OK);
        if (ret == ESP_OK)
        {
            taskENTER_CRITICAL(&s_policy.lock);
            if (s_policy.pending_quiesce_generation == generation)
            {
                s_policy.pending_quiesce_generation = 0U;
            }
            taskEXIT_CRITICAL(&s_policy.lock);
        }
        return ret;
    }
    return safety_monitor_policy_notify(
        SAFETY_MONITOR_POLICY_NOTIFY_COORDINATOR);
}

static esp_err_t safety_monitor_policy_request_reevaluate(
    uint32_t generation, void *user_ctx)
{
    (void)generation;
    (void)user_ctx;
    taskENTER_CRITICAL(&s_policy.lock);
    s_policy.blocked_by_runtime_coordinator = false;
    s_policy.pending_quiesce_generation = 0U;
    taskEXIT_CRITICAL(&s_policy.lock);
    const esp_err_t ret = safety_monitor_policy_notify(
        SAFETY_MONITOR_POLICY_NOTIFY_COORDINATOR);
    return ret == ESP_ERR_INVALID_STATE ? ESP_OK : ret;
}

static void safety_monitor_policy_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "wait: ui_first_frame_ready");
    (void)startup_readiness_wait_ui_first_frame(portMAX_DELAY);
    ESP_LOGI(TAG, "ready: ui_first_frame_ready");
    (void)safety_monitor_policy_apply("startup");

    while (true)
    {
        uint32_t reasons = SAFETY_MONITOR_POLICY_NOTIFY_NONE;
        if (xTaskNotifyWait(0U, UINT32_MAX, &reasons,
                            kPolicyPollTicks) != pdTRUE ||
            reasons == SAFETY_MONITOR_POLICY_NOTIFY_NONE)
        {
            reasons = SAFETY_MONITOR_POLICY_NOTIFY_PERIODIC;
        }
        (void)safety_monitor_policy_apply(
            safety_monitor_policy_notify_text(reasons));
    }
}

esp_err_t safety_monitor_policy_init(void)
{
    if (s_policy.initialized)
    {
        return ESP_OK;
    }
    esp_err_t ret = power_policy_init();
    if (ret == ESP_OK)
    {
        ret = startup_readiness_init();
    }
    if (ret == ESP_OK)
    {
        ret = safety_monitor_session_init();
    }
    if (ret != ESP_OK)
    {
        return ret;
    }

    const runtime_coordinator_participant_config_t participant = {
        .id = RUNTIME_COORDINATOR_PARTICIPANT_SAFETY_MONITOR,
        .name = "safety_monitor",
        .capabilities =
            RUNTIME_COORDINATOR_CAPABILITY_BACKGROUND_PREEMPTIBLE,
        .request_quiesce = safety_monitor_policy_request_quiesce,
        .request_reevaluate = safety_monitor_policy_request_reevaluate,
    };
    ret = runtime_coordinator_register(&participant);
    if (ret != ESP_OK)
    {
        return ret;
    }

    taskENTER_CRITICAL(&s_policy.lock);
    s_policy.initialized = true;
    taskEXIT_CRITICAL(&s_policy.lock);
    return ESP_OK;
}

esp_err_t safety_monitor_policy_start(void)
{
    esp_err_t ret = safety_monitor_policy_init();
    if (ret != ESP_OK)
    {
        return ret;
    }
    if (s_policy.started)
    {
        return ESP_OK;
    }
    const BaseType_t created = xTaskCreateWithCaps(
        safety_monitor_policy_task, "safety_policy", 4096, NULL, 4,
        &s_policy.task_handle, MALLOC_CAP_SPIRAM);
    if (created != pdPASS)
    {
        s_policy.task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    taskENTER_CRITICAL(&s_policy.lock);
    s_policy.started = true;
    taskEXIT_CRITICAL(&s_policy.lock);
    return ESP_OK;
}

esp_err_t safety_monitor_policy_set_enabled(bool enabled)
{
    esp_err_t ret = safety_monitor_policy_init();
    if (ret != ESP_OK)
    {
        return ret;
    }
    taskENTER_CRITICAL(&s_policy.lock);
    s_policy.enabled_by_user = enabled;
    const bool started = s_policy.started;
    taskEXIT_CRITICAL(&s_policy.lock);
    if (!started)
    {
        return ESP_ERR_INVALID_STATE;
    }
    return safety_monitor_policy_notify(SAFETY_MONITOR_POLICY_NOTIFY_USER);
}

esp_err_t safety_monitor_policy_set_foreground_audio_active(
    bool active, const char *reason)
{
    esp_err_t ret = safety_monitor_policy_init();
    if (ret != ESP_OK)
    {
        return ret;
    }
    // 前台音频占用/释放必须可诊断：日志标注是哪个调用方（如 mic_test、
    // memory_watch_recording）触发，便于排查谁长时间霸占输入链路。
    ESP_LOGD(TAG, "set_foreground_audio_active: active=%d reason=%s",
             active ? 1 : 0, reason != NULL ? reason : "unknown");
    taskENTER_CRITICAL(&s_policy.lock);
    s_policy.foreground_audio_active = active;
    const bool started = s_policy.started;
    taskEXIT_CRITICAL(&s_policy.lock);
    if (!started)
    {
        return ESP_OK;
    }
    return safety_monitor_policy_notify(SAFETY_MONITOR_POLICY_NOTIFY_AUDIO);
}

esp_err_t safety_monitor_policy_set_music_active(bool active,
                                                  const char *reason)
{
    esp_err_t ret = safety_monitor_policy_init();
    if (ret != ESP_OK)
    {
        return ret;
    }
    ESP_LOGD(TAG, "set_music_active: active=%d reason=%s", active ? 1 : 0,
             reason != NULL ? reason : "unknown");
    taskENTER_CRITICAL(&s_policy.lock);
    s_policy.music_playback_active = active;
    const bool started = s_policy.started;
    taskEXIT_CRITICAL(&s_policy.lock);
    if (!started)
    {
        return ESP_OK;
    }
    return safety_monitor_policy_notify(SAFETY_MONITOR_POLICY_NOTIFY_MUSIC);
}

esp_err_t safety_monitor_policy_notify_power_changed(void)
{
    const esp_err_t ret =
        safety_monitor_policy_notify(SAFETY_MONITOR_POLICY_NOTIFY_POWER);
    return ret == ESP_ERR_INVALID_STATE ? ESP_OK : ret;
}

safety_monitor_policy_snapshot_t safety_monitor_policy_get_snapshot(void)
{
    safety_monitor_policy_snapshot_t snapshot;
    taskENTER_CRITICAL(&s_policy.lock);
    snapshot.started = s_policy.started;
    snapshot.enabled_by_user = s_policy.enabled_by_user;
    snapshot.allowed_by_power_policy = s_policy.allowed_by_power_policy;
    snapshot.should_run = s_policy.should_run;
    snapshot.runtime_running = s_policy.runtime_running;
    snapshot.block_reason = s_policy.block_reason;
    snapshot.music_playback_active = s_policy.music_playback_active;
    snapshot.blocked_by_runtime_coordinator =
        s_policy.blocked_by_runtime_coordinator;
    snapshot.policy_state = s_policy.policy_state;
    snapshot.policy_flags = s_policy.policy_flags;
    taskEXIT_CRITICAL(&s_policy.lock);
    return snapshot;
}
