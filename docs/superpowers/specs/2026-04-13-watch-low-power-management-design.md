---
title: 手表项目低功耗管理设计
date: 2026-04-13
status: approved
---

# 手表项目低功耗管理设计

## 问题陈述

当前仓库已经具备：

- `AXP2101` 第一阶段只读电源观测
- `board_power / power_service` 状态桥接
- `LVGL + CO5300 + FT5x06`
- `Wi-Fi`、音频、后台服务

但低功耗仍停留在“系统能跑”的阶段，还没有形成：

- 空闲降级
- 灭屏待机
- RTC 定时唤醒
- 电池低电量策略
- PMIC/RTC/主控协同的完整电源状态机

目标不是直接做“深睡一次解决”，而是按 Apple Watch 风格，建立一套分层降级的低功耗架构。

## 设计目标

- 保持现有启动链路稳定，不破坏显示、触摸、音频和共享 I2C。
- 先实现运行态到待机态的渐进式降功耗。
- 为后续 `RTC + AXP2101 IRQ + PKEY + 深睡/关机` 预留清晰接缝。
- 把“电源事实”“板级电源状态”“低功耗策略编排”继续分层。

## 非目标

- 第一阶段不直接改 `AXP2101 REG80+` 输出轨。
- 第一阶段不直接改 `REG25/26/27/28~2B` 的 sleep/wakeup/poweroff 序列。
- 第一阶段不把 `GPIO10` 重新定义为真实电源键。
- 第一阶段不做“PMIC 接管整机上电/关机”。

## 证据与约束

### 已确认硬件事实

- `AXP2101` 是整机电源中枢，不是普通 I2C 小外设。
- `AXP_IRQ` 是 active-low，接向 `EXIO5`。
- `PWROK -> CHIP_PU`，不能当普通 GPIO 建模。
- `RTC_INT -> GPIO39`。
- `GPIO10` 是 `SYS_OUT` 镜像，不是 `PWRON` 原始脚。

证据来源：

- `docs/context/knowledge/project/power-wakeup-control-map.md`
- `docs/context/knowledge/esp32-s3/axp2101-deep-dive.md`
- `docs/context/knowledge/esp32-s3/pcf85063atl-minimal-probe.md`

### 已确认软件约束

- `AXP2101`、触摸、音频控制面共用 `GPIO14/15` 的共享 I2C。
- 当前 `hardware_init()` 已承担大量基础初始化，不适合继续堆复杂策略。
- 目前 `AXP2101` 落地实现仍是“只读观测优先”。

证据来源：

- `docs/context/knowledge/project/axp2101-power-component-design.md`
- `docs/context/knowledge/project/axp2101-power-component-implementation.md`

## 方案比较

### 方案 A：直接做 ESP32 deep sleep

优点：

- 实现快
- 理论功耗最低

缺点：

- 会跳过显示、网络、音频、RTC、PMIC 的渐进降级
- 唤醒恢复路径复杂
- 不适合当前仍在梳理板级唤醒链路的阶段

结论：

- 不选，太激进。

### 方案 B：只做 UI 灭屏和任务降频

优点：

- 风险低
- 不碰 PMIC/RTC

缺点：

- 无法形成完整低功耗架构
- 后续很容易再次散落逻辑

结论：

- 可作为起步，但不应是最终结构。

### 方案 C：分层状态机

思路：

- `board_power / power_service` 保持事实与状态发布
- 新增 `power_policy` 负责“何时降级、降到哪一档、如何恢复”
- 分阶段演进：
  - 运行态省电
  - 待机态
  - RTC/PMIC 协同深睡或关机

优点：

- 与当前三层架构兼容
- 风险可控
- 易于验证和回退

结论：

- 选用本方案。

## 选定架构

```text
components/axp2101
  -> PMIC 事实源、IRQ/PKEY/状态寄存器

main/app/board_power.[ch]
  -> 板级电源语义

main/services/power_service.[ch]
  -> 周期采样、状态发布、低电量状态输入

main/services/power_policy.[ch]
  -> 低功耗状态机、超时逻辑、降级/恢复编排
```

## 低功耗状态机

### 1. Active

特征：

- 屏幕正常亮度
- 正常 LVGL 刷新
- Wi-Fi 正常活跃
- 音频/触摸全功能

进入条件：

- 用户交互中
- 播放音频中
- 有网络交互

### 2. Idle-Dim

特征：

- 降低背光亮度
- 降低 LVGL 刷新频率
- 停止非必要动画
- 保持快速恢复

进入条件：

- 一段时间无用户操作
- 当前无关键前台任务

退出条件：

- 触摸
- 按键
- 新前台任务

### 3. Standby

特征：

- 灭屏
- UI 刷新基本停止
- Wi-Fi 降活跃或按策略断开
- 音频链路停止
- 系统保持可快速唤醒

唤醒源优先级：

- 用户交互
- `RTC_INT(GPIO39)`
- 后续 `AXP_IRQ`

### 4. Deep Sleep / Power Off

特征：

- 只有在 `RTC + PMIC + 唤醒源` 闭环完成后才启用
- 面向长时间待机或极低电量保护

当前不作为第一阶段实现目标。

## 模块职责

### `power_service`

- 持续提供：
  - `external_power_present`
  - `battery_present`
  - `charging`
  - `discharging`
  - `battery_percent`
  - `system_mv`
- 作为 `power_policy` 的输入，不直接决定 UI/睡眠状态

### `power_policy`

负责：

- 维护 `Active / Idle-Dim / Standby / Deep Sleep` 状态机
- 处理空闲计时
- 根据电池、电源和活动状态决定降级
- 调用各子系统的 enter/exit hook

不负责：

- 直接读 PMIC 寄存器
- 直接解析 RTC 原始寄存器

### 子系统 Hook

建议逐步补齐：

- `display_power_set_mode(active|dim|off)`
- `network_power_set_mode(active|standby|off)`
- `audio_power_set_mode(active|idle|off)`
- `touch_power_set_mode(active|standby)`

## 分阶段实施路线

### 阶段 1：运行态省电

- 实现 `Idle-Dim`
- 降低空闲时亮度
- 降低 LVGL 刷新频率
- 停止非必要动画
- 不进入 sleep

### 阶段 2：待机态

- 实现 `Standby`
- 灭屏
- 网络降活跃
- 音频停机
- 保留快速唤醒

### 阶段 3：RTC 接入

- 打通 `PCF85063ATL` 最小读时
- 接入 `RTC_INT(GPIO39)`
- 验证闹钟或外部事件唤醒

### 阶段 4：PMIC 事件模型

- 接 `AXP_IRQ`
- 接 `VBUS insert/remove`
- 接 `charge start/done`
- 接 `PKEY short/long`

### 阶段 5：深睡/关机

- 结合 `RTC + AXP2101` 完成长待机和低电保护策略

## 风险点

- `PWROK -> CHIP_PU`，误建模会导致整机 reset/启动异常。
- `GPIO10` 不是 `PWRON`，误用会让“配网键”和“电源键”语义冲突。
- 共享 I2C 上高频探测或复杂恢复会影响触摸和音频控制。
- 若先做深睡而不做分阶段降级，恢复路径很容易把 UI、网络、音频顺序打乱。

## 验证计划

### 日志验证

- 状态切换日志清晰：
  - `Active -> Idle-Dim`
  - `Idle-Dim -> Standby`
  - `Standby -> Active`

### 功耗验证

至少测 4 档：

- 亮屏空闲
- Dim
- Standby
- 后续 Deep Sleep

### 功能回归

- 触摸恢复正常
- 音频恢复正常
- Wi-Fi 能重新联网
- 电源状态采样不回归

## 回滚策略

- 若 `Idle-Dim` 出问题，只关闭亮度/刷新率降级。
- 若 `Standby` 出问题，退回“只 dim 不待机”。
- 若 RTC/PMIC 唤醒出问题，去掉策略层入口，保留当前 `AXP2101` 只读观测。

## 最终建议

当前项目最适合模仿 Apple Watch 的，是“分层降级”思路，而不是“直接深睡”。

也就是：

- 先让系统在运行态更省电
- 再做待机态
- 最后才做 RTC/PMIC 协同深睡与关机

这样最符合当前硬件证据、仓库结构和可验证性要求。
