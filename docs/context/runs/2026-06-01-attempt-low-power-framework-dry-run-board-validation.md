---
id: attempt-2026-06-01-low-power-framework-dry-run-board-validation
date: 2026-06-01
status: completed
result: partial
summary: 上板验证低功耗框架 Phase 1/3：power_budget 在 ACTIVE/STANDBY 下发布 sleep 字段，sleep_coordinator 默认 DRY_RUN，STANDBY 后 LIGHT_ALLOWED 但不进入真实 sleep。
last_reviewed: 2026-06-01
scope: repo
owners: main/services/power_policy.c, main/services/sleep_coordinator.c, main/services/network_service.c, main/ui/ui_refresh_policy.c
tags: attempt, low-power, power-budget, sleep-coordinator, standby, dry-run, board-validation
record_because: 首次上板验证 low-power-framework-execution-plan 的 power_budget 与 sleep_coordinator dry-run 链路。
---

# Low Power Framework Dry-run Board Validation

## Background

本轮按 `docs/context/plans/active/2026-06-01-low-power-framework-execution-plan.md` 执行 Phase 1 和 Phase 3 的板端证据采集。目标是确认：

- `power_policy` 能发布带 sleep 字段的 `power_budget_change`。
- `sleep_coordinator` 默认只做 `DRY_RUN`，不进入真实 `Light Sleep / Deep Sleep`。
- `STANDBY` 后预算可以表达 `LIGHT_ALLOWED`，但真实 sleep 仍被测试模式挡住。

## Environment

- Board: ESP32-S3 watch project board on `COM3`
- Command: `scripts/board/agent_serial_monitor.ps1 -Port COM3 -Action app-flash-monitor -DurationSeconds 240 -Tag low-power-framework-dry-run-retry`
- Full log: `board_logs/2026-06-01-18-56-59-low-power-framework-dry-run-retry.log`
- Summary: `board_logs/2026-06-01-18-56-59-low-power-framework-dry-run-retry.summary.json`

第一次 75 秒窗口在 app 写入约 79% 时到时，状态为 `no_boot_seen`。这是采集窗口覆盖了大 app 刷写耗时，不是固件启动崩溃；随后改用 240 秒窗口完成 `app-flash-monitor`。

## Observations

启动后首先进入 ACTIVE 预算：

```text
power_budget_change: state=ACTIVE ... sleep=NONE blockers=none interval_ms=0 ...
sleep_coordinator: dry_run: count=1 permission=NONE blockers=none interval_ms=0
sleep_coordinator: started: mode=DRY_RUN
```

UI 首帧、网络和 SNTP 后续均正常完成：

```text
boot_stage: ui_first_frame_ready
network state: WIFI_READY -> SERVICE_READY
system_time: SNTP sync ok
```

约 30 秒无交互后进入 STANDBY，并发布允许测试 Light Sleep 的预算：

```text
ui_refresh_policy: refresh state active -> standby
power_budget_change: state=STANDBY standby_reason=1 display=2 ui=1 network=2 background=2 cpu=2 poll=1 sleep=LIGHT_ALLOWED blockers=none interval_ms=8000 ...
```

网络 owner 消费预算，启用 Wi-Fi power save 并暂停非关键同步：

```text
network_service: Wi-Fi power save enabled by power budget
network_service: network state: SERVICE_READY -> WIFI_READY (network sync paused by power budget)
```

`sleep_coordinator` 在 STANDBY 后持续 dry-run，只打印 readiness，不调用真实 sleep：

```text
sleep_coordinator: dry_run: count=17 permission=LIGHT_ALLOWED blockers=none interval_ms=8000
sleep_coordinator: dry_run: count=61 permission=LIGHT_ALLOWED blockers=none interval_ms=8000
```

RTC evidence 仍保持安全口径，真实 Light Sleep 测试没有开启：

```text
wakeup_evidence: light_sleep_test_skipped: disabled_for_usb_console_safety
```

本轮摘要中 `fatal` 计数为 0，未观察到 `Guru / panic / abort / NO_MEM / watchdog / flash checksum mismatch`。

## Conclusion

本轮已完成低功耗框架 Phase 1/3 的板端 dry-run 证据：

- `ACTIVE` 下 `sleep_permission=NONE`。
- `STANDBY` 下 `sleep_permission=LIGHT_ALLOWED`、`sleep_blockers=none`、`sleep_interval_hint_ms=8000`。
- `sleep_coordinator` 默认 `DRY_RUN`，不会进入真实 `esp_light_sleep_start()` 或 `esp_deep_sleep_start()`。
- STANDBY 后 UI、网络、sleep readiness 三条消费链路都能在日志里被观察。

该结论仍是 partial：只证明默认安全固件和 dry-run 链路成立，不证明真实 Light Sleep / Deep Sleep 唤醒恢复。

## Unverified

- 真实 `LOW_BATTERY_WARN` 板端触发；当前板端日志为 `soc=100`、`low_battery=0`。
- 显式 `LIGHT_TEST / DEEP_TEST`。
- sleep 前后 UI / touch / Wi-Fi / Safety Monitor 的恢复闭环。
- `AXP_IRQ/EXIO5` 最终 MCU 映射与外部唤醒。
- 真实低电量、拔电、长时间续航场景。

## Notes

本轮 `agent_serial_monitor` 的自定义 evidence 计数为 0，是因为 `-Pattern` 传入了带空格的合并正则字符串，summary 未按预期拆分多个模式。实际证据通过完整日志人工检索确认。后续可优先改用多个 `-LiteralPattern` 或修正 wrapper 调用方式。
