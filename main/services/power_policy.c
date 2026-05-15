#include "services/power_policy.h"

#include "app/board_power.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "services/power_service.h"
#include "ui_refresh_policy.h"

static const char *TAG = "power_policy";
static const uint8_t k_low_battery_warn_percent = 20U; /* 低电量预警起点，单位为百分比。 */

static bool s_initialized = false;
static bool s_started = false;
static bool s_maintenance_window_active = false;
static power_policy_budget_t s_last_budget = {
    .state = POWER_POLICY_STATE_ACTIVE,
    .danger_detection_allowed = true,
    .network_sync_allowed = true,
    .maintenance_allowed = false,
    .ui_high_refresh_allowed = true,
    .haptic_alert_allowed = true,
    .low_battery_warn = false,
    .external_power_present = false,
};
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

const char *power_policy_state_text(power_policy_state_t state)
{
    switch (state)
    {
    case POWER_POLICY_STATE_IDLE_DIM:
        return "IDLE_DIM";
    case POWER_POLICY_STATE_STANDBY:
        return "STANDBY";
    case POWER_POLICY_STATE_LOW_BATTERY_WARN:
        return "LOW_BATTERY_WARN";
    case POWER_POLICY_STATE_CHARGING:
        return "CHARGING";
    case POWER_POLICY_STATE_MAINTENANCE:
        return "MAINTENANCE";
    case POWER_POLICY_STATE_ACTIVE:
    default:
        return "ACTIVE";
    }
}

/**
 * @brief 按 UI 活跃度快照收窄普通运行态预算。
 *
 * `ui_refresh_policy` 仍然是亮度和 LVGL 延时 owner；这里仅消费它已经
 * 缓存的只读事实，用于把普通 `ACTIVE` 预算细分为 `IDLE_DIM`。
 *
 * @param[in,out] budget 当前待发布的资源预算。
 */
static void power_policy_apply_ui_activity_budget(power_policy_budget_t *budget)
{
    if (budget == NULL)
    {
        return;
    }

    ui_refresh_policy_activity_snapshot_t activity_snapshot;
    if (!ui_refresh_policy_get_activity_snapshot(&activity_snapshot) ||
        !activity_snapshot.initialized)
    {
        return;
    }

    if (activity_snapshot.activity_state ==
        UI_REFRESH_POLICY_ACTIVITY_IDLE_DIM)
    {
        if (budget->state == POWER_POLICY_STATE_ACTIVE)
        {
            budget->state = POWER_POLICY_STATE_IDLE_DIM;
        }
        budget->ui_high_refresh_allowed = false;
    }
}

/**
 * @brief 根据电源快照和只读 UI 活跃度事实计算第一阶段资源预算。
 *
 * 第一阶段只把已可观测的充电、低电量、维护窗口和 UI idle-dim 信号纳入预算；
 * standby 还没有统一触发源，因此保留枚举和接口，不在这里猜测状态。
 */
static power_policy_budget_t power_policy_build_budget(
    const board_power_state_t *power_state)
{
    power_policy_budget_t budget = {
        .state = POWER_POLICY_STATE_ACTIVE,
        .danger_detection_allowed = true,
        .network_sync_allowed = true,
        .maintenance_allowed = false,
        .ui_high_refresh_allowed = true,
        .haptic_alert_allowed = true,
        .low_battery_warn = false,
        .external_power_present = false,
    };

    taskENTER_CRITICAL(&s_lock);
    const bool maintenance_window_active = s_maintenance_window_active;
    taskEXIT_CRITICAL(&s_lock);

    if (power_state == NULL || !power_state->available)
    {
        if (maintenance_window_active)
        {
            budget.state = POWER_POLICY_STATE_MAINTENANCE;
            budget.danger_detection_allowed = false;
            budget.network_sync_allowed = false;
            budget.maintenance_allowed = true;
            budget.ui_high_refresh_allowed = false;
        }
        return budget;
    }

    power_policy_apply_ui_activity_budget(&budget);

    budget.external_power_present =
        power_state->external_power_present || power_state->charging;
    budget.low_battery_warn =
        power_state->battery_data_valid &&
        !budget.external_power_present &&
        power_state->battery_percent <= k_low_battery_warn_percent;

    if (budget.external_power_present)
    {
        budget.state = POWER_POLICY_STATE_CHARGING;
        budget.maintenance_allowed = true;
        if (maintenance_window_active)
        {
            budget.state = POWER_POLICY_STATE_MAINTENANCE;
            budget.danger_detection_allowed = false;
            budget.network_sync_allowed = false;
            budget.ui_high_refresh_allowed = false;
        }
        return budget;
    }

    if (budget.low_battery_warn)
    {
        budget.state = POWER_POLICY_STATE_LOW_BATTERY_WARN;
        budget.network_sync_allowed = false;
        budget.maintenance_allowed = false;
        budget.ui_high_refresh_allowed = false;
        /*
         * 低电量预警下仍保留危险识别，但后续可以在同一预算字段上
         * 继续细化为保守降频，而不是让各模块自行解释电量策略。
         */
        budget.danger_detection_allowed = true;
    }

    if (maintenance_window_active && !budget.low_battery_warn)
    {
        budget.state = POWER_POLICY_STATE_MAINTENANCE;
        budget.danger_detection_allowed = false;
        budget.network_sync_allowed = false;
        budget.maintenance_allowed = true;
        budget.ui_high_refresh_allowed = false;
        return budget;
    }

    return budget;
}

static bool power_policy_budget_equal(const power_policy_budget_t *lhs,
                                      const power_policy_budget_t *rhs)
{
    return lhs->state == rhs->state &&
           lhs->danger_detection_allowed == rhs->danger_detection_allowed &&
           lhs->network_sync_allowed == rhs->network_sync_allowed &&
           lhs->maintenance_allowed == rhs->maintenance_allowed &&
           lhs->ui_high_refresh_allowed == rhs->ui_high_refresh_allowed &&
           lhs->haptic_alert_allowed == rhs->haptic_alert_allowed &&
           lhs->low_battery_warn == rhs->low_battery_warn &&
           lhs->external_power_present == rhs->external_power_present;
}

/**
 * @brief 发布预算变化日志。
 *
 * 只在预算发生有效变化时打印，避免后台服务周期读取造成日志噪声。
 */
static void power_policy_store_budget(const power_policy_budget_t *budget)
{
    bool changed = false;

    taskENTER_CRITICAL(&s_lock);
    changed = !power_policy_budget_equal(&s_last_budget, budget);
    if (changed)
    {
        s_last_budget = *budget;
    }
    taskEXIT_CRITICAL(&s_lock);

    if (changed)
    {
        ESP_LOGI(TAG,
                 "policy_state_change: state=%s danger=%d net_sync=%d maintenance=%d ui_high_refresh=%d low_battery=%d external_power=%d",
                 power_policy_state_text(budget->state),
                 budget->danger_detection_allowed,
                 budget->network_sync_allowed,
                 budget->maintenance_allowed,
                 budget->ui_high_refresh_allowed,
                 budget->low_battery_warn,
                 budget->external_power_present);
    }
}

esp_err_t power_policy_init(void)
{
    if (s_initialized)
    {
        return ESP_OK;
    }

    s_initialized = true;
    return ESP_OK;
}

esp_err_t power_policy_start(void)
{
    esp_err_t ret = power_policy_init();
    if (ret != ESP_OK)
    {
        return ret;
    }

    if (s_started)
    {
        return ESP_OK;
    }

    s_started = true;
    power_policy_budget_t budget = power_policy_get_budget();
    ESP_LOGI(TAG, "policy started: state=%s", power_policy_state_text(budget.state));
    return ESP_OK;
}

esp_err_t power_policy_set_maintenance_window(bool active, const char *reason)
{
    esp_err_t ret = power_policy_init();
    if (ret != ESP_OK)
    {
        return ret;
    }

    bool changed = false;
    taskENTER_CRITICAL(&s_lock);
    changed = s_maintenance_window_active != active;
    s_maintenance_window_active = active;
    taskEXIT_CRITICAL(&s_lock);

    if (changed)
    {
        ESP_LOGI(TAG, "maintenance_window_%s: reason=%s",
                 active ? "enter" : "exit",
                 reason != NULL ? reason : "unknown");
    }

    (void)power_policy_get_budget();
    return ESP_OK;
}

power_policy_budget_t power_policy_get_budget(void)
{
    power_policy_budget_t budget =
        power_policy_build_budget(power_service_get_state());
    power_policy_store_budget(&budget);
    return budget;
}
