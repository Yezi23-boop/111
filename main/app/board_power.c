#include "board_power.h"

#include "axp2101.h"

/* 最近一次成功采样后的缓存状态，供 power_service 与 UI 查询。 */
static board_power_state_t s_cached_state = {0};
static const board_power_state_t s_unsampled_state = {
    .available = false,
    .battery_data_valid = false,
    .snapshot_stale = false,
    .battery_percent = UINT8_MAX,
};
static bool s_initialized = false;      // 组件是否已完成 probe/init
static bool s_has_cached_state = false; // 是否已有至少一次成功快照

static const board_power_state_t *board_power_get_unsampled_state(void)
{
    return &s_unsampled_state;
}

static board_power_state_t board_power_from_snapshot(
    const axp2101_snapshot_t *snapshot)
{
    board_power_state_t state = {0};
    // AXP 上报的电量百分比必须在 [0,100] 才作为有效数据发布。
    bool battery_percent_valid = snapshot->battery_percent >= 0 &&
                                 snapshot->battery_percent <= 100;

    state.available = true;
    state.snapshot_stale = false;
    state.charging = snapshot->charging;
    state.discharging = snapshot->discharging;
    state.external_power_present = snapshot->vbus_good;
    state.battery_present = snapshot->battery_present;
    state.battery_mv = snapshot->battery_mv;
    state.system_mv = snapshot->vsys_mv;
    state.battery_data_valid = state.battery_present &&
                               snapshot->battery_mv > 0 &&
                               battery_percent_valid;
    state.battery_percent = state.battery_data_valid
                                ? (uint8_t)snapshot->battery_percent
                                : UINT8_MAX;
    return state;
}

esp_err_t board_power_init(void)
{
    if (s_initialized)
    {
        return ESP_OK;
    }

    bool present = false;
    esp_err_t ret = axp2101_probe(&present);
    if (ret != ESP_OK)
    {
        return ret;
    }
    if (!present)
    {
        return ESP_ERR_NOT_FOUND;
    }

    s_initialized = true;
    return ESP_OK;
}

esp_err_t board_power_refresh(board_power_state_t *state)
{
    if (state == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized)
    {
        *state = *board_power_get_unsampled_state();
        return ESP_ERR_INVALID_STATE;
    }

    axp2101_snapshot_t snapshot = {0}; // 原始 PMIC 采样
    esp_err_t ret = axp2101_read_snapshot(&snapshot);
    if (ret == ESP_OK)
    {
        s_cached_state = board_power_from_snapshot(&snapshot);
        s_has_cached_state = true;
        *state = s_cached_state;
        return ESP_OK;
    }

    if (s_has_cached_state)
    {
        board_power_state_t stale_state = s_cached_state;
        stale_state.snapshot_stale = true;
        stale_state.available = false;
        *state = stale_state;
        return ret;
    }

    *state = *board_power_get_unsampled_state();
    return ret;
}

const board_power_state_t *board_power_get_cached_state(void)
{
    if (!s_has_cached_state)
    {
        return board_power_get_unsampled_state();
    }
    return &s_cached_state;
}
