#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        bool available;              // 当前快照是否来自一次成功采样
        bool battery_data_valid;     // 电池电量/电压字段是否可信
        bool snapshot_stale;         // true 表示使用的是历史快照（最新采样失败）
        bool charging;               // PMIC 判定为充电中
        bool discharging;            // PMIC 判定为放电中
        bool external_power_present; // 是否检测到外部供电（USB/VBUS）
        bool battery_present;        // 是否检测到电池在位
        uint16_t battery_mv;         // 电池电压（mV）
        uint16_t system_mv;          // 系统母线电压（mV）
        /* Valid only when battery_data_valid is true; otherwise UINT8_MAX. */
        uint8_t battery_percent;
    } board_power_state_t;

    // 初始化板级电源观测模块，内部会探测 AXP2101。
    esp_err_t board_power_init(void);
    // 刷新一次快照，失败时可能回落到历史状态并标记 stale。
    esp_err_t board_power_refresh(board_power_state_t *state);
    // 获取最近一次缓存快照（可能是 unsampled 占位状态）。
    const board_power_state_t *board_power_get_cached_state(void);

#ifdef __cplusplus
}
#endif
