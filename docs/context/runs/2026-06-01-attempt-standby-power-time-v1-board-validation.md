---
id: attempt-2026-06-01-standby-power-time-v1-board-validation
date: 2026-06-01
status: completed
result: partial
summary: 子代理复查 P0/P1 修复后，上板验证 STANDBY 第一版、system_time 启动摘要、SNTP 写回和 Wi-Fi 预算消费；低电量真实触发与触摸退出 STANDBY 未在无人条件下验证。
last_reviewed: 2026-06-01
scope: repo
owners: main/ui/ui_refresh_policy.c, main/services/power_policy.c, main/services/network_service.c, main/services/system_time_service.c
tags: attempt, standby, low-power, power-policy, system-time, board-validation
record_because: 首次用无人 app-flash-monitor 观察 30 秒无交互进入运行态 STANDBY、渐进变暗和 Wi-Fi power save 预算消费。
---

# STANDBY / Power Time V1 上板验证

## 背景

本轮目标是验证 `2026-06-01-power-time-visible-status-plan.md` 的第一版实现：

- `system_time_boot` 能输出 RTC / OS / source / sys_valid 摘要。
- SNTP 成功后能输出 `system_time_sync`，并显示 RTC 写回和 drift。
- 30 秒无交互后进入运行态 `STANDBY`，但不进入 ESP sleep。
- 屏幕按运行态策略渐进降亮到 0%。
- `network_service` 消费 `power_policy` 预算，打开 Wi-Fi modem power save，并暂停非关键云端探测，不主动断开 AP。

## 环境

- 仓库：`D:\esp32S3\111`
- 目标：ESP32-S3 / ESP-IDF 5.5.3
- 串口：`COM3`
- 验证命令：`scripts/board/agent_serial_monitor.ps1`
- 方式：`app-flash-monitor`
- 采集窗口：`190s`
- 日志：`board_logs/2026-06-01-12-30-46-standby-power-time-v1-final.log`
- 摘要：`board_logs/2026-06-01-12-30-46-standby-power-time-v1-final.summary.json`

## 子代理复查处理

high reasoning 子代理复查后提出三点：

- P0：首次 P0 危险提醒如果发生在 `STANDBY` 下，需要先唤醒 UI 再展示提醒。已修复为 `APP_ALERT_SEVERITY_DANGER` raise 时立即通知 UI activity。
- P1：`LOW_BATTERY_WARN` 第一版是“可见提示”，不应顺手禁止普通网络同步或 UI 高刷。已修复为只发布 `low_battery_warn` 与提示事实，不附带网络/UI 预算降级。
- P2：`network_service` 已进入云端探测函数时，当前不会中途打断探测，只会在下一轮循环消费 STANDBY 预算。该项记录为后续优化，不阻塞本轮第一版。

## 观测

关键启动和时间日志：

```text
I (2579) system_time_srv: system_time_boot: rtc_present=1 os=0 source=RTC sys_valid=1 rtc=2026-06-01 12:32:44 reason=rtc_bootstrap_ok
I (9389) system_time: system_time_sync: source=SNTP rtc_writeback=1 drift_sec=7 drift_known=1
```

关键 STANDBY 与网络预算日志：

```text
I (33109) ui_refresh_policy: refresh state active -> standby
I (33139) power_policy: policy_state_change: state=STANDBY danger=1 net_sync=0 maintenance=0 ui_high_refresh=0 low_battery=0 external_power=1 bat_valid=1 soc=100 vbat=4123mV
I (33889) NETWORK_SERVICE: Wi-Fi power save enabled by power budget
I (33889) NETWORK_SERVICE: network state: SERVICE_READY -> WIFI_READY (network sync paused by power budget)
```

关键渐进变暗日志：

```text
I (33109) ui_refresh_policy: apply brightness state=standby force=0 user=100% target=99%
I (33639) ui_refresh_policy: apply brightness state=standby force=0 user=100% target=89%
I (34159) ui_refresh_policy: apply brightness state=standby force=0 user=100% target=78%
I (35699) ui_refresh_policy: apply brightness state=standby force=0 user=100% target=48%
I (37249) ui_refresh_policy: apply brightness state=standby force=0 user=100% target=17%
I (38279) ui_refresh_policy: apply brightness state=standby force=0 user=100% target=0%
```

工具摘要：

```text
AGENT_SERIAL_MONITOR_STATUS=ok
fatal.guru_meditation=0
fatal.panic=0
fatal.no_mem=0
fatal.watchdog=0
```

## 结论

- `system_time_boot` 和 `system_time_sync` 摘要日志已在板端出现。
- 30 秒无交互进入 `STANDBY` 已在板端出现。
- `STANDBY` 下 `power_policy` 仍允许 danger：`danger=1`，没有普通待机暂停 Safety Monitor 的证据。
- `network_service` 消费预算后启用 Wi-Fi modem PS，并把服务状态从 `SERVICE_READY` 降到 `WIFI_READY`，符合“暂停非关键云端探测，不断 AP”的第一版约束。
- `background_service_manager` 当前没有除 Safety Monitor 外的可暂停后台任务；普通 `STANDBY` 下 Safety Monitor 按安全约束继续允许运行，非关键网络同步由 `network_service` 这个资源 owner 降级。
- 屏幕亮度从 99% 逐步降到 0%，符合“面板逐渐变暗、不进入 CO5300 sleep-in”的第一版约束。
- 采集窗口内未发现 panic、Guru、NO_MEM、watchdog。

## 未验证项

- `LOW_BATTERY_WARN` 真机触发未验证：当前供电和电池状态为 `soc=100`，只能由 source tests 证明路径和 owner 边界。
- 触摸/按键退出 `STANDBY` 未在无人验证中执行；当前 source tests 和代码路径证明 `ui_refresh_policy_notify_activity()` 可退出，但缺少板端触摸日志证据。
- P0 危险提醒强制退出 `STANDBY` 未在本次无人验证中触发；当前代码路径和 source tests 已锁定提醒调用 `ui_refresh_policy_notify_activity()`，后续仍需结合 Safety Monitor 或调试触发做板端证据。

## 2026-06-01 晚间补充：app-flash-monitor 窗口语义修复后复测

本次发现首次 `agent_serial_monitor.ps1 -Action app-flash-monitor -DurationSeconds 75` 会把 app 刷写时间也算入采集窗口；当前 app 写入约 74 秒，旧窗口在写入约 79% 时容易超时并误报 `no_boot_seen`。随后修复工具语义：`FlashTimeoutSeconds` 负责刷写和等待启动，看到 `ESP-ROM` / `app_main` 后才开始计算 `DurationSeconds` 观察窗口。

复测命令：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -Command "& { .\scripts\board\agent_serial_monitor.ps1 -Port COM3 -DurationSeconds 75 -FlashTimeoutSeconds 240 -Action app-flash-monitor -Tag standby-app-flash-observe -Pattern @('standby=state=STANDBY','startup=boot_stage: startup_sequence_done') -LiteralPattern @('reason=') -TailLines 160 }"
```

复测日志：

```text
board_logs/2026-06-01-19-01-08-standby-app-flash-observe.log
```

关键证据：

```text
Wrote 9954592 bytes (4320644 compressed) at 0x00010000 in 74.4 seconds
I (2120) main_task: Calling app_main()
I (2830) MAIN: boot_stage: startup_sequence_done
I (33160) power_policy: power_budget_change: state=STANDBY standby_reason=1 display=2 ui=1 network=2 background=2 cpu=2 poll=1 sleep=LIGHT_ALLOWED blockers=none interval_ms=8000 ...
I (33910) NETWORK_SERVICE: Wi-Fi power save enabled by power budget
I (33910) NETWORK_SERVICE: network state: SERVICE_READY -> WIFI_READY (network sync paused by power budget)
I (38300) ui_refresh_policy: apply brightness state=standby force=0 user=100% target=0%
I (63000) ui_refresh_policy: refresh state standby -> active
I (63170) power_policy: power_budget_change: state=ACTIVE standby_reason=0 ...
```

补充结论：

- app 分区完整写入已确认，本次不是固件崩溃导致 `no_boot_seen`。
- 修复后的窗口语义能够覆盖完整刷写、启动、进入 `STANDBY`、Wi-Fi power save、亮度降到 0%。
- 日志中还观察到约 63 秒回到 `ACTIVE`，同步恢复 Wi-Fi 非省电策略；原因需结合当时是否有触摸/按键/PMIC IRQ 或 UI activity 再做单独判读。
- 该次长输出采集没有生成同名 summary，因此工具随后补 `QuietConsole` 模式，长刷写采集默认建议静默控制台，只保留完整 log 与 summary。
