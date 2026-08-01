#ifndef SAFETY_MONITOR_POLICY_H
#define SAFETY_MONITOR_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "services/power/power_policy.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        SAFETY_MONITOR_POLICY_BLOCK_NONE = 0,
        SAFETY_MONITOR_POLICY_BLOCK_NOT_READY,
        SAFETY_MONITOR_POLICY_BLOCK_USER_DISABLED,
        SAFETY_MONITOR_POLICY_BLOCK_POWER,
        SAFETY_MONITOR_POLICY_BLOCK_FOREGROUND_AUDIO,
        SAFETY_MONITOR_POLICY_BLOCK_RUNTIME_COORDINATOR,
    } safety_monitor_policy_block_reason_t;

    /** Safety Monitor 用户意图、策略许可和真实运行态快照。 */
    typedef struct
    {
        bool started;
        bool enabled_by_user;
        bool allowed_by_power_policy;
        bool should_run;
        bool runtime_running;
        safety_monitor_policy_block_reason_t block_reason;
        bool blocked_by_runtime_coordinator;
        power_policy_state_t policy_state;
        uint32_t policy_flags;
    } safety_monitor_policy_snapshot_t;

    esp_err_t safety_monitor_policy_init(void);
    esp_err_t safety_monitor_policy_start(void);
    esp_err_t safety_monitor_policy_set_enabled(bool enabled);
    esp_err_t safety_monitor_policy_set_foreground_audio_active(
        bool active, const char *reason);
    esp_err_t safety_monitor_policy_notify_power_changed(void);
    safety_monitor_policy_snapshot_t safety_monitor_policy_get_snapshot(void);

#ifdef __cplusplus
}
#endif

#endif /* SAFETY_MONITOR_POLICY_H */
