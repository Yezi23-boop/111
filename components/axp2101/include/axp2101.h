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
        bool vbus_good;         // 外部供电是否有效
        bool battery_present;   // 电池是否在位
        bool battfet_on;        // BATFET 导通状态
        bool charging;          // 充电方向判定
        bool discharging;       // 放电方向判定
        uint16_t battery_mv;    // 电池电压（mV）
        uint16_t vbus_mv;       // VBUS 电压（mV）
        uint16_t vsys_mv;       // 系统电压（mV）
        int8_t battery_percent; // 电量百分比，-1 表示未知
    } axp2101_snapshot_t;

    typedef struct
    {
        uint8_t irq0; // IRQ bank0 原始位图
        uint8_t irq1; // IRQ bank1 原始位图
        uint8_t irq2; // IRQ bank2 原始位图
    } axp2101_irq_status_t;

    // 初始化 AXP2101 驱动并绑定共享 I2C 设备句柄。
    esp_err_t axp2101_init(void);
    // 探测 AXP2101 是否应答。
    esp_err_t axp2101_probe(bool *present);
    // 读取一次电源快照。
    esp_err_t axp2101_read_snapshot(axp2101_snapshot_t *snapshot);
    // 读取 IRQ 状态寄存器。
    esp_err_t axp2101_read_irq_status(axp2101_irq_status_t *status);
    /* Not atomic: each IRQ bank is cleared with a separate RW1C write, so a later
     * bank failure may leave earlier banks already cleared. */
    esp_err_t axp2101_clear_irq_status(const axp2101_irq_status_t *status);

#ifdef __cplusplus
}
#endif
