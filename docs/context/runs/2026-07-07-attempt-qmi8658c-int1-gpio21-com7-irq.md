---
title: "Attempt Log: QMI8658C INT1 GPIO21 COM7 IRQ closed-loop"
id: run-2026-07-07-qmi8658c-int1-gpio21-com7-irq
date: 2026-07-07
tags: context, runs, attempt-log, imu, qmi8658c, wom, int1, gpio21, irq, board-test, com7
summary: COM7 当前板经完整刷写后，QMI8658C WoM 可通过 INT1/GPIO21 触发 GPIO IRQ，imu_service 采到 source=irq 的运动窗口。
last_reviewed: 2026-07-07
status: completed
evidence: board-log, source-test, build
owners: components/qmi8658c, main/app/board_imu.c, main/services/imu_service.c
---

# QMI8658C INT1(GPIO21) COM7 IRQ 闭环测试

## 背景

用户要求闭环验证当前板 `QMI8658C INT1 -> ESP32-S3 GPIO21` 中断链路能不能用。历史上下文中，2026-06-04 的 COM3 板测曾判断当时样板 GPIO21 路径浮空/开路，并使用 20 ms `STATUS1.WoM` 轮询 fallback。

本轮测试不把旧结论直接泛化到当前硬件，而是重新按当前 COM7 板和当前固件闭环验证。

## 操作

- 临时启用 `imu_service_start()` 以便在主固件启动后配置 WoM/INT1 并打印事件日志。
- 先执行完整构建：
  `& D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`
  - 结果：通过。
  - `111.bin` size `0xac7950`。
  - app 分区剩余 `0x3386b0`。
- 运行 source tests：
  `uv run python -m unittest tests.test_imu_service_source tests.test_qmi8658c_source tests.test_imu_motion_source tests.test_extract_imu_raise_samples_script`
  - 结果：23 passed。
- 首次使用 `agent_serial_monitor.ps1 -Action app-flash-monitor` 只刷 app，因板上仍是旧 8MB factory 分区表，启动失败：
  - `Image length 11303248 doesn't fit in partition length 8388608`
  - `No bootable app partitions`
  - 日志：`board_logs/2026-07-07-01-57-53-qmi-gpio-int-test.log`
- 随后执行 `idf.py -p COM7 flash` 完整刷写 bootloader、partition table、app 和资源分区。
- 使用 `agent_serial_monitor.ps1 -Action monitor -DurationSeconds 90` 采集启动和运动日志，期间人工晃动/翻转板子触发 WoM。

## 关键日志

日志文件：

- `board_logs/2026-07-07-02-06-40-qmi-gpio-int-test-after-full-flash.log`
- `board_logs/2026-07-07-02-06-40-qmi-gpio-int-test-after-full-flash.summary.json`

启动阶段：

```text
imu_service: started: qmi_wom_int1=log_only
imu_service: probe: present=1 who_am_i=0x05 revision_id=0x7b
MAIN: boot_stage: imu_service_ready
imu_service: int1_gpio_ready: gpio=21 level=0 intr=anyedge
imu_service: wom_configured: threshold_mg=120 blanking=16 int1_gpio=21 level=0 statusint=0x00 int1_mirror=0 mirror_stable=1 int1_usable=1 status1=0x00 action=log_only
```

真实 WoM/IRQ 事件：

```text
imu_service: wom_event: event_id=1 source=irq gpio=21 level=1 statusint=0x02 int1_mirror=1 status1=0x04 ...
imu_service: motion_window_start: event_id=1 source=irq samples=16 sample_period_ms=40 mode=physical_6axis action=log_only
imu_service: raise_result: event_id=1 source=irq ...

imu_service: wom_event: event_id=2 source=irq gpio=21 level=1 statusint=0x02 int1_mirror=1 status1=0x04 ...
imu_service: motion_window_start: event_id=2 source=irq samples=16 sample_period_ms=40 mode=physical_6axis action=log_only
imu_service: raise_result: event_id=2 source=irq ...

imu_service: wom_event: event_id=3 source=irq gpio=21 level=1 statusint=0x02 int1_mirror=1 status1=0x04 ...
imu_service: motion_window_start: event_id=3 source=irq samples=16 sample_period_ms=40 mode=physical_6axis action=log_only
imu_service: raise_result: event_id=3 source=irq ...
```

采集工具结果：

- `status=captured`
- `capture_stop_reason=duration_elapsed`
- `panic_log_seen=false`
- `residual_monitor_count=0`

## 结论

- 当前 COM7 板上，`QMI8658C INT1 -> GPIO21 -> GPIO ISR -> imu_service notification` 链路闭环可用。
- 关键判据不是单次 GPIO 电平，而是：
  - 启动检测 `int1_usable=1`。
  - WoM 事件中 `source=irq`。
  - `gpio=21 level=1` 与 `STATUSINT.INT1(statusint=0x02)`、`STATUS1.WoM(status1=0x04)` 同步出现。
  - 每次 IRQ 后进入 16 帧 `physical_6axis` 运动窗口并输出 `raise_result`。
- 这条证据限定为当前 COM7 板和当前固件配置。2026-06-04 COM3 的“GPIO21 浮空/开路”结论应视为当时样板证据，不能继续直接套到当前板。
- `app-flash` 失败不是 GPIO/IMU 问题，而是板上旧分区表与当前 app 大小不匹配；本轮通过完整 `idf.py flash` 更新分区表后恢复正常启动。

## 收尾

- 临时启用 IMU service 的 `app_main.c` 测试开关已恢复为 `#if 0`，避免正常固件静默启用 IMU service。
- 恢复默认源码后重新构建通过：
  `& D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`
  - `111.bin` size `0xac3230`。
  - app 分区剩余 `0x33cdd0`。
- 已用 `agent_serial_monitor.ps1 -Action app-flash-monitor` 将默认固件刷回 COM7：
  - 日志：`board_logs/2026-07-07-02-11-33-qmi-gpio-int-test-restored-default.log`
  - summary：`board_logs/2026-07-07-02-11-33-qmi-gpio-int-test-restored-default.summary.json`
  - `flash_completed=true`
  - `panic_log_seen=false`
  - `residual_monitor_count=0`
  - 启动日志只出现 `boot_stage: app_start` 和 `boot_stage: startup_sequence_done`，未出现 `imu_service:`、`imu_service_ready` 或 `wom_event`。
- 后续如果要长期启用 IMU service，应改成明确的 Kconfig/运行策略，而不是靠临时 `#if`。
- 由于当前板 IRQ 已通过，后续抬腕或 fall detection 实时采样可以优先使用 GPIO IRQ 作为 WoM 事件源，同时保留按板验证和必要 fallback。
