---
id: watch-low-power-management-architecture
tags: project, power, low-power, axp2101, rtc, lvgl, wifi
summary: 当前手表项目低功耗管理的推荐状态机、模块分层、实施顺序与硬件边界。
last_reviewed: 2026-04-13
---

# 手表项目低功耗管理架构

## 推荐路线

当前项目不适合直接上“深睡一把梭”，更适合按 Apple Watch 风格做分层降级：

1. `Active`
2. `Idle-Dim`
3. `Standby`
4. `Deep Sleep / Power Off`

## 为什么不能一上来就深睡

- `PWROK -> CHIP_PU`，不能把 PMIC 关机/复位时序当普通 GPIO 功能来试错。
- `GPIO10` 是 `SYS_OUT` 镜像，不是 `PWRON`。
- `AXP2101`、触摸、音频控制面共用一条 I2C。
- `RTC_INT -> GPIO39` 还没接入驱动，RTC/唤醒链路还没闭环。

证据来源：

- `docs/context/knowledge/project/power-wakeup-control-map.md`
- `docs/context/knowledge/esp32-s3/axp2101-deep-dive.md`
- `docs/context/knowledge/esp32-s3/pcf85063atl-minimal-probe.md`

## 推荐分层

```text
components/axp2101
  -> PMIC 事实源

main/app/board_power.[ch]
  -> 板级电源语义

main/services/power_service.[ch]
  -> 状态采样与发布

main/services/power_policy.[ch]
  -> 低功耗状态机与跨模块编排
```

## 各阶段目标

### 阶段 1：运行态省电

- 空闲降亮度
- 降 LVGL 刷新
- 停非必要动画
- 先不 sleep

### 阶段 2：Standby

- 灭屏
- 降网络活跃
- 停音频
- 保留快速唤醒

### 阶段 3：RTC 准备

- 打通 `PCF85063ATL` 最小探测与读时
- 接入 `RTC_INT(GPIO39)` 可观测路径

### 阶段 4：PMIC 事件

- 接 `AXP_IRQ`
- 读 `VBUS insert/remove`
- 读 `charge start/done`
- 读 `PKEY short/long`

### 阶段 5：深睡/关机

- RTC/PMIC 协同
- 低电量保护
- 长时间待机

## 当前最值得先做的 3 件事

1. 做 `power_policy`
2. 先落 `Idle-Dim`
3. 再落 `Standby`

## 验证重点

- 状态切换日志
- 亮屏/Dim/Standby 的真实电流
- 唤醒后 UI、Wi-Fi、音频、触摸恢复

## 回滚策略

- `Idle-Dim` 出问题：退回正常亮屏
- `Standby` 出问题：退回只 dim
- RTC/PMIC 出问题：保留当前只读电源观测，不接深睡
