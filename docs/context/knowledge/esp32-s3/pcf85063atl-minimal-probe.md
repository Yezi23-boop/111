---
id: pcf85063atl-minimal-probe
tags: esp32-s3, rtc, pcf85063atl, i2c, probe, wakeup
summary: PCF85063ATL 在当前板上的最小探测地址、用途边界和接入顺序摘要。
last_reviewed: 2026-08-07
memory_type: semantic
scope: board
owners: docs/context/knowledge/esp32-s3/pcf85063atl-minimal-probe.md
triggers: pcf85063atl, minimal, probe
evidence_level: observed
status: active
---

# PCF85063ATL 最小探测

## 当前已知事实

- 原理图明确存在 `PCF85063ATL`
- 原理图还能看到 `RTC_INT`、32.768 kHz 晶振与 `ESP32_SCL/SDA`
- 新补充的管脚对照页可直接确认：`RTC_INT -> GPIO39`
- 当前代码中未发现 `PCF85063ATL` 驱动接入
- 官方资料显示其 I2C 从地址为 `0x51`

## 可直接使用的地址结论

- `0x51`

## 最小探测目标

1. 在共享 `I2C` 总线上确认 `0x51` 是否存在
2. 只验证 RTC 芯片可读，不先做系统时间接管
3. 明确 `RTC_INT` 后续是否接入 GPIO 中断

## 建议探测步骤

1. 用当前 `i2c_manager_scan()` 先确认 `0x51` 是否出现
2. 若 `0x51` 存在，再做最小寄存器读，优先读秒/分钟或控制寄存器
3. 先验证掉电后保时，再考虑把 `time_weather` 的时间来源接到 RTC
4. 唤醒、闹钟、中断必须在“保时稳定”之后再接

## 预期日志

- 与现有触摸/codec 设备共存，不应影响 `0x18`、`0x38`、`0x40`
- 新增设备预期为 `0x51`

## 失败信号

- `0x51` 不存在
- 读寄存器总是超时或返回固定值
- 加 RTC 探测后共享 `I2C` 其他设备出现异常

## 后续落点

- 第一阶段优先做本地 RTC 保时与读时
- 第二阶段再做：
  - 闹钟
  - `RTC_INT(GPIO39)` 中断
  - 离线启动时间基准

## 适用边界

- 本文只覆盖最小探测与接入顺序，不展开寄存器细节。
- 若后续把系统时间主来源切到 RTC，需要同步更新启动链路与低功耗文档。
