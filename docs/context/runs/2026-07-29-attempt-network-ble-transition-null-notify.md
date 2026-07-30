---
id: run-network-ble-transition-null-notify-2026-07-29
tags: context, runs, attempt-log, network-service, ble, freertos, task-notification, panic, foreground-session
summary: 修复冷启动 BLE transition worker 完成过快时对空 network task handle 调用 xTaskNotifyGive 导致的 xTaskGenericNotify assert。
last_reviewed: 2026-07-29
memory_type: run
scope: network
status: completed
owners: main/services/network/network_service.c, tests/test_network_service_ble_source.py, docs/context/plans/active/2026-07-14-watch-foreground-session-lifecycle-plan.md
evidence_level: build
---

# Attempt Log: Network BLE Transition Null Notify

## 背景

- 板端日志显示 IMU/Fall 默认关闭已经生效，没有 `原始表`、`采样表`、Fall 模型或窗口日志。
- 真正重启原因是冷启动联网阶段崩溃：`assert failed: xTaskGenericNotify tasks.c:5909 (xTaskToNotify)`。
- backtrace 指向 `network_service_finish_ble_transition()` -> `xTaskNotifyGive(s_network_task_handle)`；当 BLE transition worker 完成时，`s_network_task_handle` 仍可能尚未发布。

## 改动

- `network_service_task()` 入口立即用 `xTaskGetCurrentTaskHandle()` 发布真实 network owner task handle。
- `network_service_finish_ble_transition()` 在临界区内复制 notify target，并在 `notify_handle != NULL` 时才调用 `xTaskNotifyGive()`。
- 若 worker 极早完成但 task handle 仍未准备好，只记录 warning，不触发 FreeRTOS assert；network task 后续按周期继续回收 completed worker。
- source test 锁定 notify 判空、真实 task handle 发布和 BLE transition 异步 owner 合同。

## 验证

- `uv run python -m unittest tests.test_network_service_ble_source tests.test_power_integration_source tests.test_main_screen_ble_toggle_source`：26 tests passed。
- `git diff --check -- main/services/network/network_service.c tests/test_network_service_ble_source.py`：无 whitespace error，仅 LF/CRLF warning。
- `. D:/esp-idf/v5.5.3/esp-idf/export.ps1; idf.py build`：通过；`111.bin` size `0xabd7a0`，最小 app 分区剩余 `0x342860` / 23%。
- `scripts/board/agent_serial_monitor.ps1 -Port COM7 -Action app-flash-monitor -DurationSeconds 30 -Tag network-ble-null-notify-fix -QuietConsole`：`panic_log_seen=false`；日志 `board_logs/2026-07-29-13-19-30-network-ble-null-notify-fix.log` 显示 `BLE transition complete: enabled=0 generation=1 result=ESP_OK gate_held=0`，随后出现 `boot_stage: imu_service_disabled_by_default` 与 `boot_stage: fall_detection_disabled_by_default`。

## 风险与下一步

- 本轮修复的是空 task handle 通知导致的 panic，不改变 BLE presence/provisioning 产品语义。
- 后续 BLE 阶段 3 仍需继续收敛 presence/provisioning 的完整活跃期 gate；本轮只关闭冷启动 notify 空句柄 panic。
