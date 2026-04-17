---
id: power-wakeup-control-map
tags: project, power, wakeup, axp2101, rtc, button, boot
summary: 基于原理图与现有代码整理的电源、按键、RTC 和唤醒控制链路摘要。
last_reviewed: 2026-04-17
---

# 电源与唤醒控制图

## 原理图中能确认的电源/控制器件

- `AXP2101`：主 PMIC
- `PCF85063ATL`：RTC
- `VBUS`、`VBAT1`、`VBAT2`：USB/电池相关电源网络
- `PWRON`、`PWROK`、`AXP_IRQ`：PMIC 关键控制/状态网络
- `RTC_INT`：RTC 中断网络
- 从本次 AXP2101 原理图片段还能直接确认：
  - `AXP_IRQ` 经过 `10k` 上拉到 `VCC_RTC`
  - `AXP_IRQ` 网络接向 `EXIO5`
  - `PWROK` 直接接到 `CHIP_PU`
  - `RTCLDO` 输出 `VCC_RTC`
  - `VBACKUP` 接 `VBAT2`

## 当前代码中已接入的控制入口

- 软件按键当前只明确使用 `GPIO10`，定义在 `main/app/hardware_init.c`
- 该按键当前单击会触发 BLE 配网，三连击才会触发 `wifi_provision_start_apcfg()` 进入 AP 配网
- 当前代码中已经接入：
  - `components/axp2101`
  - `main/app/board_power.[ch]`
  - `main/services/power_service.[ch]`
- 当前代码中仍未发现：
  - `PCF85063ATL` 正式驱动
  - `AXP_IRQ` 中断处理链路
  - `RTC_INT` 唤醒/中断链路
  - `PWRON`、`PWROK` 的应用层直接控制
- 结合 `KEYS` 原理图页，`GPIO10` 现在可以收敛为：
  - 它接的是 `SYS_OUT`
  - `SYS_OUT` 来自 `PWRON` 物理按键链路经 `BSS138` 的镜像输出
  - 因而 `GPIO10` 不是 `PWRON` 原始网络，但它和电源键事件并非无关

## 当前电源/唤醒现状

- 系统启动主要依赖主控上电后进入 `app_main()`，但软件层已经具备：
  - AXP2101 probe 与只读快照
  - `board_power` 板级电源状态语义
  - `power_service` 后台发布与抖动抑制
  - `ui_refresh_policy` 的运行态 dim/降频策略
- 当前仍未落成的部分主要是：
  - RTC 定时/唤醒闭环
  - PMIC IRQ 事件闭环
  - Standby / Deep Sleep 级别的系统级电源策略

## 对后续功能的影响

- 若要做电池电量、充电动画、按键长按开关机、低功耗待机、RTC 定时唤醒，需要先接入 `AXP2101` 和 `PCF85063ATL`
- 若要做稳定的待机/唤醒，不能只看 FreeRTOS 任务挂起，还要建模：
  - PMIC 输出轨
  - RTC 保活
  - 外设断电顺序
  - 唤醒源恢复顺序

## 当前最小可观测建议

- 先把 `GPIO10`、`WIFI_CONNECT_BIT`、`AXP_IRQ/RTC_INT` 这些控制点分开建模，不要把“按键配网”和“电源键/唤醒键”混为一类
- 若后续接 PMIC/RTC，优先补：
  - 中断来源日志
  - 电池/充电状态读数
  - 进入待机前后的电流测量点
- 当前已可直接复用的事实源是：
  - `axp2101_read_snapshot()`
  - `board_power_get_cached_state()`
  - `power_service_get_state()`
- 结合本次原理图页，当前应补充的硬约束是：
  - 不要尝试把 `PWROK` 当应用层普通 GPIO 去读写
  - `AXP_IRQ` 应按 active-low、外部上拉已存在的输入来建模
  - RTC 保活链路要把 `VCC_RTC/VBAT2` 一起考虑进去
  - `GPIO10` 可以作为电源键事件的镜像观测点，但不能等价替代 `PWRON` 原始脚语义

## 适用边界

- 本文基于 `ESP32-S3-Touch-AMOLED-2.06.pdf` 文本提取结果以及当前 `main/components` 代码搜索结果整理。
- 由于当前环境缺少 PDF 页面渲染工具，若后续要精确定位引脚页签、封装方向或丝印名称，请再做一次视觉复核。
