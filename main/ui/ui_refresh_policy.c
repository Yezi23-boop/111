#include "ui_refresh_policy.h"

#include "co5300_panel.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <limits.h>

/*
 * 刷新策略实现说明：
 * 1. 该模块只维护“界面是否活跃”的事实，不直接管理触摸采样或 LVGL tick；
 * 2. 延时策略和亮度策略复用同一状态机，保证刷新降频和 dim 同步发生；
 * 3. 所有对外接口都允许被高层多次调用，初始化和状态切换需保持幂等。
 */

typedef enum
{
    UI_REFRESH_POLICY_STATE_ACTIVE = 0,
    UI_REFRESH_POLICY_STATE_IDLE_DIM,
    UI_REFRESH_POLICY_STATE_FORCE_ACTIVE,
} ui_refresh_policy_state_t;

static const char *TAG = "ui_refresh_policy";
static const uint32_t k_active_delay_ms = 16U;                 /* 活跃态最大循环延时，单位为毫秒。 */
static const uint32_t k_idle_delay_ms = 100U;                  /* 空闲态最小循环延时，单位为毫秒。 */
static const int64_t k_active_timeout_us = 5000LL * 1000LL;    /* 最近一次触摸后保持活跃的窗口，单位为微秒。 */
static const uint8_t k_idle_brightness_percent = 40U;          /* 空闲态目标亮度占用户亮度的百分比。 */
static const uint8_t k_default_user_brightness_percent = 100U; /* 默认用户亮度，单位为百分比。 */

static bool s_initialized = false;
static bool s_force_active = false;                                        /* 强制活跃标志，由上层场景控制。 */
static uint8_t s_user_brightness_percent = 100U;                           /* 用户配置的原始亮度百分比。 */
static uint8_t s_applied_brightness_percent = UCHAR_MAX;                   /* 最近一次成功下发给面板的亮度；`UCHAR_MAX` 表示尚未下发。 */
static int64_t s_last_touch_time_us = 0;                                   /* 最近一次用户活跃时间戳，单位为微秒。 */
static ui_refresh_policy_state_t s_state = UI_REFRESH_POLICY_STATE_ACTIVE; /* 当前刷新策略状态机状态。 */

/**
 * @brief 限制亮度百分比到合法范围。
 * @param[in] percent 外部传入亮度值。
 * @return 0 到 100 之间的亮度百分比。
 */
static uint8_t ui_refresh_policy_clamp_percent(uint8_t percent)
{
    if (percent > 100U)
    {
        return 100U;
    }
    return percent;
}

/**
 * @brief 根据 idle dim 策略计算空闲态目标亮度。
 *
 * 这里额外保证非零用户亮度在 dim 后不会直接变成 0，
 * 以避免用户仍期望“可见但更暗”时被错误解释成完全熄屏。
 *
 * @param[in] percent 用户配置的原始亮度百分比。
 * @return 空闲态下实际应使用的亮度百分比。
 */
static uint8_t ui_refresh_policy_compute_dim_brightness(uint8_t percent)
{
    uint32_t scaled = ((uint32_t)percent * k_idle_brightness_percent) / 100U;
    if (percent > 0U && scaled == 0U)
    {
        return 1U;
    }
    return (uint8_t)scaled;
}

/**
 * @brief 根据当前状态机推导真正要下发给面板的亮度。
 * @return 应写入面板的亮度百分比。
 */
static uint8_t ui_refresh_policy_get_effective_brightness_percent(void)
{
    if (s_force_active ||
        s_state == UI_REFRESH_POLICY_STATE_FORCE_ACTIVE ||
        s_state == UI_REFRESH_POLICY_STATE_ACTIVE)
    {
        return s_user_brightness_percent;
    }

    return ui_refresh_policy_compute_dim_brightness(s_user_brightness_percent);
}

/**
 * @brief 将内部状态枚举转换成日志可读字符串。
 * @param[in] state 内部状态机状态。
 * @return 状态对应的短字符串。
 */
static const char *ui_refresh_policy_state_name(ui_refresh_policy_state_t state)
{
    switch (state)
    {
    case UI_REFRESH_POLICY_STATE_ACTIVE:
        return "active";
    case UI_REFRESH_POLICY_STATE_IDLE_DIM:
        return "idle_dim";
    case UI_REFRESH_POLICY_STATE_FORCE_ACTIVE:
        return "force_active";
    default:
        return "unknown";
    }
}

/**
 * @brief 仅在亮度变化时向面板下发新的背光值。
 *
 * 空闲轮询会高频进入该路径；若每次都写面板寄存器，会增加无意义总线操作并干扰功耗观察。
 *
 * @return 无返回值。
 */
static void ui_refresh_policy_apply_brightness_if_needed(void)
{
    uint8_t target_percent = ui_refresh_policy_get_effective_brightness_percent();
    if (target_percent == s_applied_brightness_percent)
    {
        return;
    }

    esp_err_t ret = co5300_panel_set_brightness_percent(target_percent);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "设置亮度失败 target=%u%% err=%d", target_percent, ret);
        return;
    }

    ESP_LOGI(TAG, "apply brightness state=%s force=%d user=%u%% target=%u%%",
             ui_refresh_policy_state_name(s_state),
             s_force_active,
             s_user_brightness_percent,
             target_percent);
    s_applied_brightness_percent = target_percent;
}

/**
 * @brief 更新刷新策略状态并输出统一日志。
 * @param[in] next_state 目标状态。
 * @return 无返回值。
 */
static void ui_refresh_policy_set_state(ui_refresh_policy_state_t next_state)
{
    if (s_state == next_state)
    {
        return;
    }

    ESP_LOGI(TAG, "refresh state %s -> %s",
             ui_refresh_policy_state_name(s_state),
             ui_refresh_policy_state_name(next_state));
    s_state = next_state;
}

/**
 * @brief 初始化刷新策略状态机。
 *
 * 初始化后默认进入活跃态，并立即把亮度同步到默认用户亮度，
 * 保证开机后不会沿用上一次缓存的 dim 状态。
 */
void ui_refresh_policy_init(void)
{
    s_last_touch_time_us = esp_timer_get_time();
    s_state = UI_REFRESH_POLICY_STATE_ACTIVE;
    s_force_active = false;
    s_user_brightness_percent = k_default_user_brightness_percent;
    s_applied_brightness_percent = UCHAR_MAX;
    s_initialized = true;

    ui_refresh_policy_apply_brightness_if_needed();
}

/**
 * @brief 通知策略层刚刚发生过一次用户活跃事件。
 *
 * 典型调用点是触摸驱动、按键输入或编码器旋转。
 * 该接口只刷新时间戳；真正的空闲态判断仍由 `ui_refresh_policy_poll()` 统一执行。
 */
void ui_refresh_policy_notify_touch(void)
{
    if (!s_initialized)
    {
        return;
    }

    s_last_touch_time_us = esp_timer_get_time();
    if (!s_force_active)
    {
        ui_refresh_policy_set_state(UI_REFRESH_POLICY_STATE_ACTIVE);
        ui_refresh_policy_apply_brightness_if_needed();
    }
}

/**
 * @brief 打开或关闭强制活跃模式。
 * @param enabled true 表示禁止自动 dim 和自动降频；false 表示恢复普通策略。
 */
void ui_refresh_policy_set_force_active(bool enabled)
{
    if (!s_initialized)
    {
        ui_refresh_policy_init();
    }

    s_force_active = enabled;
    if (enabled)
    {
        ui_refresh_policy_set_state(UI_REFRESH_POLICY_STATE_FORCE_ACTIVE);
    }

    ui_refresh_policy_poll();
}

/**
 * @brief 查询强制活跃模式是否开启。
 * @return true 表示当前不允许自动进入空闲态。
 */
bool ui_refresh_policy_is_force_active(void)
{
    return s_force_active;
}

/**
 * @brief 设置用户期望亮度百分比。
 * @param percent 用户设置的原始亮度值，超过 100 会被钳制。
 *
 * 该值不是最终输出亮度。若策略当前处于空闲 dim 态，最终下发值会进一步按比例缩小。
 */
void ui_refresh_policy_set_user_brightness_percent(uint8_t percent)
{
    if (!s_initialized)
    {
        ui_refresh_policy_init();
    }

    s_user_brightness_percent = ui_refresh_policy_clamp_percent(percent);
    ui_refresh_policy_apply_brightness_if_needed();
}

/**
 * @brief 获取用户配置的原始亮度百分比。
 * @return 用户层设置值，而不是当前面板实际生效值。
 */
uint8_t ui_refresh_policy_get_user_brightness_percent(void)
{
    return s_user_brightness_percent;
}

/**
 * @brief 按当前活跃状态修正 LVGL 主循环延时。
 * @param next_call_ms `lv_timer_handler()` 建议的下次唤醒时间。
 * @return 应实际传给 `vTaskDelay()` 的毫秒值。
 *
 * 活跃态优先保证流畅度，因此把最大延时压到 16ms；
 * 空闲态优先省电，因此把最小延时抬高到 100ms。
 */
uint32_t ui_refresh_policy_adjust_delay(uint32_t next_call_ms)
{
    if (!s_initialized)
    {
        return next_call_ms;
    }

    if (s_force_active ||
        s_state == UI_REFRESH_POLICY_STATE_FORCE_ACTIVE ||
        s_state == UI_REFRESH_POLICY_STATE_ACTIVE)
    {
        return next_call_ms > k_active_delay_ms ? k_active_delay_ms : next_call_ms;
    }

    return next_call_ms < k_idle_delay_ms ? k_idle_delay_ms : next_call_ms;
}

/**
 * @brief 周期轮询刷新策略状态机。
 *
 * 该函数应在 UI 主循环中高频调用。它会结合最近触摸时间和强制活跃标志，
 * 统一决定当前状态，并在状态变化时同步亮度。
 */
void ui_refresh_policy_poll(void)
{
    if (!s_initialized)
    {
        return;
    }

    ui_refresh_policy_state_t next_state = UI_REFRESH_POLICY_STATE_IDLE_DIM;
    if (s_force_active)
    {
        next_state = UI_REFRESH_POLICY_STATE_FORCE_ACTIVE;
    }
    else
    {
        int64_t idle_time_us = esp_timer_get_time() - s_last_touch_time_us;
        if (idle_time_us < 0)
        {
            idle_time_us = 0;
        }

        if (idle_time_us <= k_active_timeout_us)
        {
            next_state = UI_REFRESH_POLICY_STATE_ACTIVE;
        }
    }

    ui_refresh_policy_set_state(next_state);
    ui_refresh_policy_apply_brightness_if_needed();
}
