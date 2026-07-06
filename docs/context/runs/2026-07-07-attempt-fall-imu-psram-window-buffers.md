---
id: attempt-2026-07-07-fall-imu-psram-window-buffers
tags: context, runs, attempt-log, imu, fall-detection, psram, internal-ram, freertos, esp-dl, com7
summary: 将 IMU/Fall 跌倒检测链路的大窗口缓冲和 fall_detect task 栈迁到 PSRAM，恢复 internal RAM 余量。
created: 2026-07-07
last_reviewed: 2026-07-07
status: completed
owners: main/services/imu_service.c, main/services/fall_detection_service.c, tests/test_imu_service_source.py, tests/test_fall_detection_service_source.py, docs/context/plans/active/2026-06-05-imu-runtime-framework-plan.md
---

# Attempt Log: Fall / IMU PSRAM Window Buffers

## 背景

用户对比 `c9b82a7fc3ba27a0dfbc9c93bbdb40d585f8ae68` 的 DS2413 闭环日志后指出当前 RAM 占用异常高。对比证据显示：

- 旧日志 `board_logs/2026-07-04-20-47-24-ds2413-normal-com7-pyserial.log`：`internal_free=49222`、`largest=30720`。
- 跌倒告警状态机日志 `board_logs/2026-07-07-07-26-15-fall-alert-state-machine-psram-alert-tasks.log`：`internal_free=1142`、`largest=672`。
- `size-files` 显示 `.espdl` 模型本体 `ram_st_total=0`，但 `imu_service.c.obj` 静态 RAM 约 8998B、`fall_detection_service.c.obj` 静态 RAM 约 7480B；窗口 ring、窗口副本、queue storage、model input 和 task stack 叠加导致 internal RAM 压力。

## 本轮变更

- `imu_service`：
  - `sample_ring[200]` 从静态数组改为 `heap_caps_calloc(..., MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`。
  - `publish_window` 从静态对象改为 PSRAM 分配的 `imu_service_accel_window_t *`。
  - `imu_service_init()` 启动前分配缓冲；失败时返回 `ESP_ERR_NO_MEM`，不进入采样任务。
- `fall_detection_service`：
  - `window_queue_storage`、`current_window`、`model_input[600]` 改为 PSRAM 分配。
  - `fall_detect` task 从 `xTaskCreate()` 改为 `xTaskCreateWithCaps(..., MALLOC_CAP_SPIRAM)`。
  - `StaticQueue_t`、lock、snapshot、句柄等小状态继续留在 internal RAM。
- source tests：
  - 锁定 IMU/Fall 大窗口缓冲必须使用 `heap_caps_*` + `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`。
  - 锁定 `fall_detect` 使用 `xTaskCreateWithCaps` 和 PSRAM 栈。

## 验证

- source tests：
  `uv run python -m unittest tests.test_haptic_alert_player_source tests.test_audio_codec_port_source tests.test_fall_detection_service_source tests.test_qmi8658c_source tests.test_imu_service_source tests.test_imu_motion_source tests.test_extract_imu_raise_samples_script tests.test_fall_detection_inference_source tests.test_danger_detection_service_source tests.test_watch_endpoint_service_source`
  - 结果：62 tests passed。
- whitespace：
  `git diff --check -- . ':!managed_components'`
  - 结果：无 whitespace error，仅 LF/CRLF warning。
- build：
  `& D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`
  - 结果：通过，`111.bin` size `0xae07a0`，app free `0x31f860`/约 22%。
- static RAM：
  `python -m esp_idf_size --files --format json -o build\size-files-after-psram-window.json build\111.map`
  - `libmain.a:imu_service.c.obj`：`ram_st_total=174`。
  - `libmain.a:fall_detection_service.c.obj`：`ram_st_total=224`。
  - `.espdl.S.obj`：`ram_st_total=0`、`flash_total=101812`。
- app-flash-monitor：
  `scripts\board\agent_serial_monitor.ps1 -Port COM7 -DurationSeconds 35 -Action app-flash-monitor -Tag fall-psram-window-buffers -StreamConsole`
  - log：`board_logs/2026-07-07-07-46-41-fall-psram-window-buffers.log`
  - summary：`board_logs/2026-07-07-07-46-41-fall-psram-window-buffers.summary.json`
  - `panic_log_seen=false`，`capture_stop_reason=duration_elapsed`，无残留 monitor。

## 板端证据

- heap 主池恢复：`heap_init: At 3FCC5D90 len 00023980 (142 KiB): RAM`。
- 冷启动资源快照：`RAM: 304 KB / 332 KB (91.8%)`。
- internal heap：`STACK: internal_free=26482 largest=24576 psram_free=6651280`。
- 采样和推理仍正常：
  - `sampling_started: rate_hz=50 window_frames=200`
  - `window_published: sequence=1 source_sample_count=200 ... frames=200`
  - `fall_window_result: sequence=1 ... label=ADL(0) ... fall_prob=0.1824`
- 告警链路仍正常：
  - `fall_alert_confirmed: alert_sequence=1 sequence=12 fall_prob=0.9399`
  - `haptic_alert_player: initial danger haptic started/finished`
  - `audio_alert_player: warning playback started/finished`
  - `watch_endpoint: danger alert dispatched: type=fall prob=0.9399 seq=1`
  - `fall_alert_cleared: alert_sequence=1 sequence=17 clear_windows=3`

## 结论

- 本轮已把 IMU/Fall 大窗口缓冲从 internal 静态 RAM 迁到 PSRAM，并将 `fall_detect` task 栈迁到 PSRAM。
- 运行时 internal RAM 从上一轮 `internal_free=1142 largest=672` 恢复到 `internal_free=26482 largest=24576`。
- ESP-DL 模型本体仍在 Flash rodata；本轮没有修改 `managed_components` 或 ESP-DL 源码。
- 观察到 ESP-DL 本次 `others internal=25740B`，比上一轮日志中的 8088B 更高，推测是 internal RAM 余量恢复后 ESP-DL allocator 选择了更多 internal。即便如此，系统最终 internal 余量仍明显改善。后续若继续压 RAM，应单独研究 ESP-DL allocator/模型加载策略，而不是再把窗口副本放回 internal。
