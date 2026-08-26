---
id: power-wakeup-control-map
tags: project, power, wakeup, axp2101, rtc, button, boot
summary: 基于原理图与现有代码整理的电源、按键、RTC 和唤醒控制链路摘要。
last_reviewed: 2026-06-01
memory_type: semantic
scope: repo
owners: components/axp2101, main/app/board_power.c, main/services/power/power_service.c
triggers: power, wakeup, control, map
evidence_level: observed
status: active
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
  - 当前 PDF 的 ESP32 GPIO 汇总表未列出 `AXP_IRQ` 或 `EXIO5`，因此不能确认它最终接到 MCU 哪个 GPIO
  - `PWROK` 直接接到 `CHIP_PU`
  - `RTCLDO` 输出 `VCC_RTC`
  - `VBACKUP` 接 `VBAT2`

## 当前代码中已接入的控制入口

- 软件按键当前只明确使用 `GPIO10`，定义在 `main/app/hardware_init.c`
- 该按键当前已不再承担 BLE / AP 配网入口语义
- 当前代码中已经接入：
  - `components/axp2101`
  - `components/pcf85063atl`
  - `main/app/board_power.[ch]`
  - `main/services/power/power_service.[ch]`
  - `main/services/power/wakeup_evidence_service.[ch]`
- 当前代码中仍未发现：
  - `AXP_IRQ` GPIO 中断处理链路
  - `RTC_INT` 作为 ESP sleep wakeup source 的链路
  - `PWRON`、`PWROK` 的应用层直接控制
- 当前原理图证据也不支持直接新增 `AXP_IRQ` GPIO 中断：
  - 已确认的是 `AXP2101 IRQ -> AXP_IRQ / EXIO5`
  - 未确认的是 `EXIO5 -> ESP32-S3 GPIO`
  - 因此只能先通过 AXP2101 IRQ bank / 状态寄存器轮询做 PMIC 事件证据，不应把 `AXP_IRQ` 配成 MCU GPIO 中断或 sleep wakeup source
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
  - RTC/PMIC 真实 sleep 唤醒闭环
  - Standby / Deep Sleep 级别的系统级电源策略

## 2026-05-16 证据闭环阶段

- `PCF85063ATL` 已新增最小 driver，先通过 countdown timer 触发 `RTC_INT(GPIO39)`，避免依赖 RTC 当前时间是否准确。
- `wakeup_evidence_service` 已接入启动链路，负责打印 RTC 时间、`Control_2`、`RTC_INT(GPIO39)` 电平与 AXP2101 IRQ bank。
- 该阶段仍是只读/观测阶段：
  - 不进入 `Light Sleep / Deep Sleep`
  - 不把 `AXP_IRQ` 当已确认 MCU GPIO 使用
  - 不把 `EXIO5` 猜测成任意空闲 ESP32-S3 GPIO
  - 不写 PMIC 电源轨或关机寄存器
- 后续 sleep 实验必须显式 opt-in，默认关闭；若只通过主 USB 串口/JTAG 观测，测试失联只能说明观测链路不可靠，不能直接判定 RTC/PMIC 唤醒失败。
  - 不把 `PWROK` 当普通 GPIO 控制

## 对后续功能的影响

- 若要做电池电量、充电动画、按键长按开关机、低功耗待机、RTC 定时唤醒，需要先接入 `AXP2101` 和 `PCF85063ATL`
- 若要做稳定的待机/唤醒，不能只看 FreeRTOS 任务挂起，还要建模：
  - PMIC 输出轨
  - RTC 保活
  - 外设断电顺序
  - 唤醒源恢复顺序

## 当前最小可观测建议

- 先把 `GPIO10`、网络 UI 入口、`AXP_IRQ/RTC_INT` 这些控制点分开建模，不要把“主界面进入配网”和“电源键/唤醒键”混为一类
- 若后续接 PMIC/RTC，优先补：
  - 中断来源日志
  - 电池/充电状态读数
  - 进入待机前后的电流测量点
- 当前已可直接复用的事实源是：
  - `axp2101_read_snapshot()`
  - `pcf85063atl_arm_countdown_timer()`
  - `board_power_get_cached_state()`
  - `power_service_get_state()`
  - `wakeup_evidence_service_start()`
- 结合本次原理图页，当前应补充的硬约束是：
  - 不要尝试把 `PWROK` 当应用层普通 GPIO 去读写
  - `AXP_IRQ` 应按 active-low、外部上拉已存在的 PMIC IRQ 线来建模，但在确认 `EXIO5` 到 MCU 的路径前不能配置 GPIO ISR
  - RTC 保活链路要把 `VCC_RTC/VBAT2` 一起考虑进去
  - `GPIO10` 可以作为电源键事件的镜像观测点，但不能等价替代 `PWRON` 原始脚语义
- PMIC IRQ 闭环验收口径应拆成两段：
  - 寄存器闭环：插拔 USB/充电状态变化时 `REG48/49/4A` 或对应 IRQ bank 有事件位，清除后事件位消失
  - 硬件线闭环：`AXP_IRQ/EXIO5` 的物理线能被确认到具体 MCU GPIO，且清 IRQ 后该线释放回高电平

## 适用边界

- 本文基于 `ESP32-S3-Touch-AMOLED-2.06.pdf` 的 Poppler 渲染截图、文本提取结果以及当前 `main/components` 代码搜索结果整理。
- 第 1 页电路图可用于网络连接判断；第 2/3 页更适合做 PCB 器件位置辅助，不应单独用于推断网络连接。
- 在没有 PCB 网表、更多板级资料或实测导通证据前，`AXP_IRQ -> EXIO5` 只能视为 PMIC IRQ 线到板级网络，不能视为 MCU wakeup GPIO。
