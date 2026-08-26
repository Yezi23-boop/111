---
id: axp2101-minimal-probe
tags: esp32-s3, axp2101, power, pmic, i2c, probe
summary: AXP2101 在当前板上的最小探测前提、候选地址和接入顺序摘要。
last_reviewed: 2026-03-11
memory_type: semantic
scope: board
owners: components/axp2101, main/app/board_power.c
triggers: axp2101, minimal, probe
evidence_level: observed
status: active
---

# AXP2101 最小探测

## 当前已知事实

- 原理图明确存在 `AXP2101`
- 同页还能看到与其相关的 `PWRON`、`PWROK`、`AXP_IRQ`、`VBUS`、`VBAT1/2`
- 当前 `main/components` 代码中未发现 `AXP2101` 驱动或上层接入
- 当前共享控制总线是 `GPIO14/15` 对应的 `I2C_NUM_0`

## 默认地址

- `0x34`（7 位地址）

## 证据边界

- `AXP2101` datasheet 的 TWSI 章节给出默认地址 `0x68/0x69`，这是 8 位写/读地址
- 换算后对应的 7 位地址为 `0x34`
- 因此 `0x34` 可以视为 datasheet 已确认的默认地址，但仍需要板上扫描确认它确实挂在当前共享总线上

## 最小探测目标

1. `i2c_manager_scan()` 能在共享总线上看到一个额外设备
2. 该设备地址优先验证 `0x34`
3. 确认 `AXP_IRQ`、`PWRON`、`PWROK` 后续是否需要单独 GPIO 驱动

## 建议探测步骤

1. 在只初始化 `i2c_manager` 的前提下执行一次总线扫描
2. 记录 `0x18`、`0x38`、`0x40` 之外是否出现 `0x34`
3. 若看到 `0x34`，先做最小寄存器读探测，不要一上来改电源轨配置
4. 在未确认寄存器语义前，不要写 PMIC 控制寄存器

## 预期日志

- 已有设备：
  - `0x18 -> ES8311`
  - `0x38 -> FT3168/FT5x06`
  - `0x40 -> ES7210`
- 若 PMIC 已挂在同总线上，预期应再看到 `0x34`

## 失败信号

- 完全扫不到 `0x34`
- 一访问候选地址就导致共享 `I2C` 其他设备异常
- 引入 PMIC 探测后触摸或 codec 控制同时失效

## 后续落点

- 板级驱动入口建议放在独立组件，而不是直接塞进 `hardware_init.c`
- 第一阶段只做：
  - 设备存在性确认
  - 电池/充电状态只读
  - 中断脚存在性确认

## 适用边界

- 本文用于“是否存在 + 能否开始接入”的第一阶段探测。
- 真正进入 PMIC 控制前，必须再补寄存器级资料和回滚策略。
