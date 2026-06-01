---
id: rtc-pmic-wakeup-evidence-loop
tags: project, power, wakeup, rtc, pcf85063atl, axp2101
summary: RTC_INT(GPIO39) 与 PMIC IRQ 的首版只读证据闭环，先观测中断/状态寄存器，不进入 ESP sleep。
last_reviewed: 2026-05-30
memory_type: semantic
scope: repo
owners: components/pcf85063atl, components/axp2101, main/services/wakeup_evidence_service.c
triggers: rtc, pmic, wakeup, evidence, rtc_int, axp_irq, pcf85063atl
evidence_level: source
---

# RTC/PMIC 唤醒证据闭环

## 当前结论

本阶段只做“唤醒源证据闭环”，不做真正 `Light Sleep / Deep Sleep`：

- `PCF85063ATL` 通过共享 I2C 总线接入，地址固定为 `0x51`。
- `RTC_INT` 按原理图证据接到 `GPIO39`，首版配置为只读输入，不开 GPIO ISR。
- `AXP2101` 继续复用现有只读 PMIC 组件，只读取/清除 IRQ 状态寄存器。
- `wakeup_evidence_service` 负责上电后打印 RTC 时间快照、启动 RTC 倒计时中断、采样 `RTC_INT(GPIO39)` 与 PMIC IRQ bank；首次 `TF/GPIO39` 证据成立后停止运行态 RTC timer，避免后续每 8 秒重复刷屏。
- `RTC_INT(GPIO39)` 观测必须启用输入上拉；板测显示清除 `TF` 后电平能从 `0` 恢复为 `1`。
- Light Sleep 自动实验代码默认禁用；主 USB 串口/JTAG 链路下直接自动睡眠会丢失可观测性和下载稳定性。
- 本阶段运行路径不调用 `esp_light_sleep_start()`、`esp_deep_sleep_start()`、`esp_sleep_enable_*()`，也不写 AXP2101 供电轨控制寄存器；若保留实验代码，必须默认关闭，并通过明确测试开关启用。

这样做的目的，是先确认“RTC 能否拉低 GPIO39、PMIC 是否能提供可读事件源”，再把这些事实接进后续睡眠唤醒状态机。

## 代码 owner

- `components/pcf85063atl/pcf85063atl.c`
  - 最小 RTC driver。
  - 只实现 probe、读时间、读 `Control_2`、清中断标志、启动/停止秒级 countdown timer。
- `main/services/wakeup_evidence_service.c`
  - 只做证据日志，不拥有低功耗策略。
  - 固定观察 `GPIO39`，并复用 `axp2101_read_irq_status()` / `axp2101_clear_irq_status()`。
- `main/app/app_main.c`
  - 在 `power_policy_start()` 之后启动证据服务，确保 shared I2C 与 PMIC 基础能力已经可用。

## 板端验收口径

上板后应重点看这些日志：

- `wakeup evidence init: rtc_present=1 rtc_int_gpio=39 level=...`
- `rtc_time_snapshot: ...`
- `rtc_timer_armed: seconds=8 rtc_int_gpio=39 initial_level=...`
- `rtc_int_sample: gpio=39 level=... control2=0x...`
- `rtc_timer_flag_observed: clearing_tf`
- `rtc_timer_stopped_after_evidence`
- `axp_irq_snapshot: irq0=... irq1=... irq2=...`

期望路径：

- 启动约 8 秒后，RTC countdown timer 置位 `TF`，`RTC_INT(GPIO39)` 应出现可观测电平变化。
- 清除 `TF` 后，`RTC_INT` 应释放为高电平；当前在 `GPIO_PULLUP_ENABLE` 下已观察到 `level_after=1`。
- 首次运行态 RTC 证据成立后，服务应打印 `rtc_timer_stopped_after_evidence`，之后不应再每约 8 秒重复出现 `control2=0x08`。
- 插拔 USB、电池或电源键事件若触发 PMIC IRQ bank，服务会打印并清除对应 IRQ 状态。

## 当前未闭环风险

- `AXP_IRQ` 当前只确认到 `EXIO5`，还没有确认最终 MCU GPIO，所以本阶段不配置 PMIC GPIO 中断。
- 如果 `axp_irq_snapshot` 长期没有日志，不能直接判定 PMIC IRQ 不工作；还需要继续确认 IRQ enable bit 与 `EXIO5` 到 MCU 的映射。
- 本阶段仍然处于运行态观测，不能证明 ESP sleep 后能唤醒，只能证明 RTC timer 与 `RTC_INT(GPIO39)` 在运行态可观测。
- 下一轮 Light Sleep 测试要先准备外部 UART 日志或手动恢复方案，不应默认随启动自动执行。

## Light Sleep 测试硬约束

- 测试入口必须是显式 opt-in，默认值保持关闭；禁止普通启动路径自动进入 `esp_light_sleep_start()`。
- V1 timer-based 显式 Light Sleep 实验的主唤醒源是 ESP32-S3 internal RTC timer；`RTC_INT(GPIO39)` 与 AXP2101 IRQ 只作为外部唤醒增强证据，不阻塞该显式实验。
- 若测试同时观测 `RTC_INT(GPIO39)`，必须确认输入上拉、RTC `TF` 清除路径和 GPIO 电平变化都能从日志区分；但不要把 GPIO39 外部唤醒闭环当作 timer-based 实验的前置条件。
- 主 USB 串口/JTAG 可能在 Light Sleep 后丢失日志或下载可观测性；仅依赖 USB 串口时，不能把“无日志/连不上”解释成 RTC 唤醒失败。
- timer-based Light Sleep 测试的执行权已归 `sleep_coordinator`；一次有效测试至少要记录 `light_test_enter`、`light_test_woke`、`esp_sleep_get_wakeup_cause()`、timer interval、elapsed 和返回错误。
- 不外接 USB-TTL 时，Light Sleep 返回后的第一件事是写入 `sleep_coordinator_sleep_test_result_t` 本地快照，再打印日志；UI 或后续日志读取 `sleep_coordinator_get_sleep_test_result()`，避免只依赖 COM3 实时输出。
- 如果测试同时观测 `RTC_INT(GPIO39)`，RTC `Control_2/TF`、GPIO39 电平和清标志后的电平恢复仍由 `wakeup_evidence_service` 运行态日志提供；它不再调用 ESP sleep API。
- 若测试后 `pyserial` 无输出或 `esptool chip_id` 无法连接，先回退到默认关闭测试的安全固件，再继续分析；不要在不稳定状态下追加更多 sleep 改动。

## 下一阶段门槛

只有同时满足下面证据，才把 RTC/PMIC 外部唤醒增强接入 Light Sleep / Deep Sleep 策略：

- RTC countdown timer 能稳定触发 `RTC_INT(GPIO39)`。
- 清除 RTC `TF` 后，`RTC_INT` 能恢复非触发电平。
- PMIC 关键事件能通过 IRQ bank 或轮询状态被稳定识别。
- `AXP_IRQ` 的最终 MCU GPIO 或不可用结论明确。
- PCF85063ATL 的时间/中断寄存器访问在共享 I2C 上没有与音频 codec、touch、PMIC 产生恢复失败。

timer-based 显式 Light Sleep / Deep Sleep 实验不受上述外部唤醒增强前置条件阻塞，但仍必须默认关闭、显式 opt-in，并由 `sleep_coordinator` 按 `power_budget` 中的 `sleep_permission / sleep_blockers / interval_hint` 执行。
