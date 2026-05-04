---
id: ft3168-shared-i2c-baseline
tags: esp32-s3, ft3168, touch, i2c, shared-bus, lvgl
summary: FT3168 在当前项目共享 I2C 总线下的通信上限、休眠约束和上电时序基线。
last_reviewed: 2026-03-11
memory_type: semantic
scope: board
owners: components/touch_ft5x06, components/lvgl_port
triggers: ft3168, shared, i2c, baseline
evidence_level: observed
---

# FT3168 共享 I2C 基线

## 当前已知事实

- 当前代码将触摸控制器按 `FT5x06/FT3168` 兼容路径接入
- 当前板级触摸控制总线为 `GPIO14/15` 对应的共享 `I2C`
- `touch_ft5x06.c` 当前使用的 7 位地址是 `0x38`
- `FT3168` datasheet 确认其主机接口是 `I2C slave`，最高支持 `400 kbps`

## 对当前项目特别重要的约束

- `FT3168` datasheet 明确说明：
  - 它可以工作在单从机或多从机环境
  - 当芯片处于 `Monitor` 或 `Sleep` 模式时，如果主机先访问了同一总线上的其他从设备，主机可能暂时无法继续与触摸芯片通信
  - 触摸事件唤醒或固件定期清理 I2C 状态机后，通信才会恢复正常

## 为什么这点重要

- 当前项目的同一组 `I2C` 线上不只有触摸，还有：
  - `ES8311/ES7210` 控制面
  - 原理图中的 `QMI8658C`
  - 原理图中的 `PCF85063ATL`
  - 预期后续接入的 `AXP2101`
- 因此“偶发触摸失联”不一定是坐标解析错，也可能是共享总线访问顺序触发了触摸芯片的状态机限制

## 上电与复位基线

- `FT3168` datasheet 建议在上电前让 `INT`、`I2C` 保持低电平
- `RESETB` 应在上电前拉低
- 芯片初始化完成后会通过 `INT` 向主机报告可读状态
- 当前代码已使用独立复位脚 `GPIO9` 和中断脚 `GPIO38`

## 当前排障建议

1. 先区分是“完全扫不到 0x38”还是“运行一段时间后偶发失联”
2. 若是偶发失联，优先检查：
   - 是否刚访问了同总线其他从设备
   - 触摸控制器是否可能进入了 `Monitor/Sleep`
   - 是否有触摸唤醒后恢复通信的现象
3. 若后续要接 RTC/IMU/PMIC，优先保留总线访问顺序和失败重试日志

## 适用边界

- `0x38` 是当前代码和板级实现使用的地址，不是本次 datasheet 文本提取得到的固定地址声明。
- 本文的重点不是寄存器细节，而是共享总线下的通信约束。
