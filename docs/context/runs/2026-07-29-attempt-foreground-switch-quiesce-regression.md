---
id: attempt-foreground-switch-quiesce-regression-2026-07-29
tags: context, runs, foreground-session, resource-arbitration, ble, hermes, esp-dl
summary: 记录 foreground gate 自动切换、后台 quiesced ACK 与组合回归前验证结果。
last_reviewed: 2026-07-29
memory_type: run
scope: run
evidence_level: verified
status: completed
---

# Foreground 切换与 Quiesced ACK 验证记录

## 本轮完成

- Hermes 与 official_chat 的强前台 owner 在各自 service task 中等待 foreground gate，最长 5 秒；页面离开会取消等待，超时保持 fail-closed，不强制销毁旧 owner。
- `background_service_manager` 使用 quiesce generation 与静态 FreeRTOS EventGroup 等待后台 Safety Monitor/ESP-DL 让路确认；前台 owner 只有在 ACK 对应当前 generation 时才创建重资源。
- BLE/SoftAP provisioning 通过 network owner task 异步创建、停止和销毁，gate 持有覆盖真实 transport 生命周期；UI 只提交意图。
- runtime board test 增加 quiesce generation/ACK、gate owner、internal free/largest block、PSRAM、Hermes foreground 和 BLE foreground 观测。

## 验证证据

- 聚焦 source tests：`57 passed`。复查后补充 quiesce finish generation ownership、严格 ACK 条件和 BLE presence 恢复路径。
- layering：`warning_count=0`，`known_exception_count=2`。
- ESP-IDF build：通过；`111.bin=0xabe680`，最小 app 分区余量 `0x341980`，约 23%。
- `git diff --check`：无 whitespace error；仅已有 line ending warning。

## 冷启动回归证据

- COM7 `app-flash-monitor` 已执行，`DurationSeconds=45`，刷写成功，采集因 duration elapsed 正常结束。
- 日志：`board_logs/2026-07-29-19-22-03-foreground-session-lifecycle-final.log`；summary：`board_logs/2026-07-29-19-22-03-foreground-session-lifecycle-final.summary.json`。
- `panic_log_seen=0`、`residual_count=0`；启动完成到 `network_service_ready`。
- board test 观测：`internal_free=76971`、largest=55296`、psram_free=6803516`；日志未发现 panic/Guru/WDT/NO_MEM/assert/stack overflow。
- 全量 source tests：`438 passed`；本轮 build：`111.bin=0xabe680`，app free `0x341980`。
- 2026-07-29 复跑 COM7 `app-flash-monitor` 45 秒：日志 `board_logs/2026-07-29-19-46-16-foreground-session-lifecycle-final-rerun.log`，summary `board_logs/2026-07-29-19-46-16-foreground-session-lifecycle-final-rerun.summary.json`；`panic_log_seen=0`、`residual_count=0`，启动到 `startup_sequence_done`、`ui_first_frame_ready` 和 `SERVICE_READY`；冷启动快照 `internal_free=61423`、`largest=53248`、`psram_free=6777484`。

## 尚未覆盖

- 脚本化启动日志不能替代用户在 Wi-Fi 管理页实际操作 BLE provisioning、SoftAP provisioning、配网完成和普通 BLE 恢复；这些交互仍须单独验证。

## 下一步

真实 provisioning 页面操作仍需单独覆盖：Wi-Fi 管理页 BLE provisioning、SoftAP provisioning、配网完成、stop/deinit、gate release 和普通 BLE presence 恢复。
