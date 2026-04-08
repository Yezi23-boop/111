---
id: power-wakeup-control-map
tags: project, power, wakeup, axp2101, rtc, button, boot
summary: 基于原理图与现有代码整理的电源、按键、RTC 和唤醒控制链路摘要。
last_reviewed: 2026-04-08
---

# 电源与唤醒控制图

## 原理图中能确认的电源/控制器件

- `AXP2101`：主 PMIC
- `PCF85063ATL`：RTC
- `VBUS`、`VBAT1`、`VBAT2`：USB/电池相关电源网络
- `PWRON`、`PWROK`、`AXP_IRQ`：PMIC 关键控制/状态网络
- `RTC_INT`：RTC 中断网络

## 当前代码中已接入的控制入口

- 软件按键当前只明确使用 `GPIO10`，定义在 `main/app/hardware_init.c`
- 该按键当前单击会触发 BLE 配网，三连击才会触发 `wifi_provision_start_apcfg()` 进入 AP 配网
- 当前代码中未发现 `AXP2101`、`PCF85063ATL`、`AXP_IRQ`、`PWRON`、`PWROK` 或 `RTC_INT` 的驱动接入

## 当前电源/唤醒现状

- 系统启动主要依赖主控上电后进入 `app_main()`，软件层尚未体现 PMIC 电源策略
- 未看到背光、电池电量、充电状态、RTC 唤醒或 PMIC 中断的统一管理模块
- 低功耗相关知识当前主要停留在通用检查清单层，尚未落成板级实现路径

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

## 适用边界

- 本文基于 `ESP32-S3-Touch-AMOLED-2.06.pdf` 文本提取结果以及当前 `main/components` 代码搜索结果整理。
- 由于当前环境缺少 PDF 页面渲染工具，若后续要精确定位引脚页签、封装方向或丝印名称，请再做一次视觉复核。
