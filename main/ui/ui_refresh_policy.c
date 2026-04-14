#include "ui_refresh_policy.h"

#include "co5300_panel.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <limits.h>

typedef enum {
    UI_REFRESH_POLICY_STATE_ACTIVE = 0,
    UI_REFRESH_POLICY_STATE_IDLE_DIM,
    UI_REFRESH_POLICY_STATE_FORCE_ACTIVE,
} ui_refresh_policy_state_t;

static const char *TAG = "ui_refresh_policy";
static const uint32_t k_active_delay_ms = 16U;
static const uint32_t k_idle_delay_ms = 100U;
static const int64_t k_active_timeout_us = 5000LL * 1000LL;
static const uint8_t k_idle_brightness_percent = 40U;
static const uint8_t k_default_user_brightness_percent = 100U;

static bool s_initialized = false;
static bool s_force_active = false;
static uint8_t s_user_brightness_percent = 100U;
static uint8_t s_applied_brightness_percent = UCHAR_MAX;
static int64_t s_last_touch_time_us = 0;
static ui_refresh_policy_state_t s_state = UI_REFRESH_POLICY_STATE_ACTIVE;

static uint8_t ui_refresh_policy_clamp_percent(uint8_t percent)
{
    if (percent > 100U)
    {
        return 100U;
    }
    return percent;
}

static uint8_t ui_refresh_policy_compute_dim_brightness(uint8_t percent)
{
    uint32_t scaled = ((uint32_t)percent * k_idle_brightness_percent) / 100U;
    if (percent > 0U && scaled == 0U)
    {
        return 1U;
    }
    return (uint8_t)scaled;
}

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

bool ui_refresh_policy_is_force_active(void)
{
    return s_force_active;
}

void ui_refresh_policy_set_user_brightness_percent(uint8_t percent)
{
    if (!s_initialized)
    {
        ui_refresh_policy_init();
    }

    s_user_brightness_percent = ui_refresh_policy_clamp_percent(percent);
    ui_refresh_policy_apply_brightness_if_needed();
}

uint8_t ui_refresh_policy_get_user_brightness_percent(void)
{
    return s_user_brightness_percent;
}

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
