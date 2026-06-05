---
id: hardware-capability-gap-map
tags: project, hardware, gap, rtc, imu, power, roadmap, qmi8658c, wom
summary: 当前板级硬件已经接入的能力与仍未闭环的中断、唤醒和产品链路摘要。
last_reviewed: 2026-06-04
memory_type: semantic
scope: repo
owners: components/axp2101, components/pcf85063atl, components/qmi8658c, main/app/board_power.c, main/app/board_imu.c, main/services/wakeup_evidence_service.c, main/services/imu_service.c
triggers: hardware, capability, gap, map, qmi8658c, gpio21, rtc-int, axp-irq
evidence_level: observed
---

# 硬件能力缺口图

## 已有硬件且代码已接入

- 显示：`CO5300` + `LVGL`
- 触摸：`FT5x06/FT3168` 兼容路径
- 音频播放/录音：`ES8311 + ES7210`
- Wi-Fi 配网：`network_manager + network_provisioning_adapter + ap_portal_adapter + wifi_control`
- 存储：`SPIFFS` + `SD` 卡
- 电源观测：`components/axp2101 -> board_power -> power_service`
- RTC 运行态证据：`components/pcf85063atl -> wakeup_evidence_service`
- IMU/WoM/抬腕证据链：`components/qmi8658c -> board_imu -> imu_service`

## 已接入但尚未闭环

- QMI8658C Rev A 的原始数据、内部 WoM 状态和 CTRL9 握手可用；当前样板已确定 `QMI_INT1 -> GPIO21` 物理通路浮空/开路，正式 service 已用 20 ms `STATUS1.WoM` 轮询降级恢复软件动作窗口，但真实硬件 IRQ 仍需导通修复或 `INT2/TP15` 飞线。
- `RTC_INT -> GPIO39` 已有运行态 timer/flag 证据，但真实 ESP Light/Deep Sleep 唤醒仍未闭环。
- `AXP_IRQ -> EXIO5` 尚未确认最终 MCU GPIO，不能配置成 ISR 或 sleep wakeup source。
- 抬腕规则已经具备 `WoM -> raw accel/gyro -> imu_motion -> final pose -> raise_result` 骨架；20 ms fallback 已能进入动作窗口，下一缺口是采集明确标注的真实佩戴抬腕/非抬腕样本并调优规则阈值。

## 证据边界

- QMI8658C COM3 原始数据诊断已得到 `WHO_AM_I=0x05`、`revision_id=0x7c` 和 `result: PASS`。
- QMI `STATUS1.WoM`、原始六轴动作帧和最终姿态判断已有运行日志；确定性诊断中三次真实 WoM 均观察到 `STATUSINT.INT1/STATUS1.WoM` 正常而 GPIO21 与全部安全候选 GPIO 不跟随，GPIO21 内部下拉隔离也确认当前样板通路浮空/开路。
- 当前 `revision_id=0x7c` 对应 Rev A；该版本 `CTRL9 0x0C` 是 Tap 配置命令，`STATUS0.bit3` 是保留位，不存在旧 Rev0.6 资料定义的 MoD/sDA/dQ 路径。主固件已移除该伪能力并切换到原始六轴软件规则。
- COM3 90 秒日志已观察到 5 次 WoM、5 个 16 帧原始六轴窗口和 5 个规则结果，且没有 `source=ae_dq`、`mod=1`、动作窗口失败或 panic/watchdog。
- RTC 与 PMIC 的详细证据边界分别由对应运行记录和低功耗知识卡维护，不应从“已接入”推断为“真实 sleep 已完成”。

## 当前功能与 UI 的差距

- UI 资源里已经有心率、AI、游戏、闹钟等页面图标，但当前代码里未看到对应传感器、BLE 同步或健康数据链路
- RTC 和 IMU 已有底层与后台证据链，但还没有形成完整的用户可见闹钟、抬腕亮屏或健康数据产品闭环
- 功耗检查清单已存在，`AXP2101 + board_power + power_service` 也已接入只读观测，但尚未形成板级 PMIC 控制闭环

## 推荐闭环顺序

1. 使用当前 `source=raw_motion` 的 20 ms WoM fallback 固件采集真实佩戴抬腕/非抬腕样本，调 `imu_motion`、final pose 和 `raise_detected` 阈值。
2. 若产品功耗或即时性要求必须使用真实 IRQ，断电检查并修复 `QMI_INT1(GPIO21)`，或验证 `INT2/TP15` 后飞线。
3. 闭环 `AXP_IRQ/EXIO5` 最终 MCU 映射和真实 sleep 唤醒证据。
4. 在底层证据稳定后，再接抬腕亮屏、闹钟和健康数据等产品链路。

## 为什么这样排

- 当前器件存在性和基本数据链路已经不再是主要缺口。
- 旧的 10 秒恢复轮询会错过动作窗口，不能用于评价抬腕算法；20 ms fallback 已把 WoM 通知时机恢复到可验证范围，原始六轴路径也已在板上产生真实变化帧，因此真实 IRQ 修复可以与算法样本采集解耦。

## 适用边界

- 本文用于规划和排障优先级；“已接入”只表示代码与部分板级证据存在，不代表所有中断、sleep 或产品体验均已验收。
- QMI8658C 的详细稳定事实和 INT1 排查顺序见 `docs/context/knowledge/esp32-s3/qmi8658c-minimal-probe.md`。
