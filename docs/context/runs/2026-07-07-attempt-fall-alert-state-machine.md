---
id: attempt-2026-07-07-fall-alert-state-machine
tags: context, runs, attempt-log, imu, fall-detection, alert, app-upload, app-alert-manager, freertos, psram, com7
summary: 为 ESP-DL IMU 跌倒检测接入单窗口确认告警状态机，并完成 COM7 本地告警与 App 上传闭环。
created: 2026-07-07
last_reviewed: 2026-07-07
status: completed
owners: main/services/fall_detection_service.c, main/features/alerts/app_alert_manager.c, main/features/alerts/audio_alert_player.c, main/features/alerts/haptic_alert_player.c, docs/context/plans/active/2026-06-05-imu-runtime-framework-plan.md
---

# Attempt Log: Fall alert state machine

## 背景

用户确认跌倒策略不采用“连续 2 个 FALL”确认，而是：

- `IDLE` 中单个 4 秒窗口 `fall_prob >= 0.80` 立即进入 `FALL_CONFIRMED`。
- 进入确认态时只触发一次完整本地告警，并向 App 上传一次。
- `FALL_CONFIRMED` 期间后续 FALL 窗口不重复告警、不重复上传。
- 连续 3 个窗口 `fall_prob < 0.50` 后清除，回到 `IDLE`。

本轮仍保持 fall service 的输入边界：只消费 `imu_service` 投递的完整加速度窗口，不直接读取 IMU driver 或 UI。

## 本轮变更

- `fall_detection_service` 新增轻量状态机和 snapshot 字段：
  - `FALL_DETECTION_ALERT_STATE_IDLE`
  - `FALL_DETECTION_ALERT_STATE_CONFIRMED`
  - `alert_sequence`
  - `clear_window_count`
  - `last_alert_window_sequence`
  - `last_alert_fall_prob`
  - `last_alert_error`
- `IDLE -> FALL_CONFIRMED` 时记录 `fall_alert_confirmed`，调用：
  - `app_alert_manager_raise(APP_ALERT_SOURCE_FALL_DETECTION, APP_ALERT_LABEL_FALL)`
  - `watch_endpoint_service_post_danger_alert(danger_type="fall", message="检测到跌倒")`
- `FALL_CONFIRMED` 期间不重复触发本地告警或 App 上传；App 上传失败只记 warning，不做后续窗口重试。
- 连续 3 个低风险窗口后调用 `app_alert_manager_clear(APP_ALERT_SOURCE_FALL_DETECTION)` 并记录 `fall_alert_cleared`。
- `app_alert_manager` 增加 `APP_ALERT_SOURCE_FALL_DETECTION`、`APP_ALERT_LABEL_FALL`，中文标签为 `跌倒`。
- 板测中发现默认 `xTaskCreate()` 在告警瞬间因 internal RAM 压力导致 haptic/audio 短生命周期 task 创建失败；已将 `audio_alert_player` 与 `haptic_alert_player` 的 task 栈改为 `xTaskCreateWithCaps(..., MALLOC_CAP_SPIRAM)`，不改变播放/震动逻辑。

## 验证

- source tests：
  `uv run python -m unittest tests.test_haptic_alert_player_source tests.test_audio_codec_port_source tests.test_fall_detection_service_source tests.test_qmi8658c_source tests.test_imu_service_source tests.test_imu_motion_source tests.test_extract_imu_raise_samples_script tests.test_fall_detection_inference_source tests.test_danger_detection_service_source tests.test_watch_endpoint_service_source`
  - 结果：62 tests passed。
- whitespace：
  `git diff --check -- . ':!managed_components'`
  - 结果：无 whitespace error，仅 LF/CRLF warning。
- build：
  `& D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`
  - 结果：通过，`111.bin` size `0xae4420`，app partition free `0x31bbe0`，约 22%。
- app-flash：
  `& D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py -p COM7 app-flash`
  - 结果：写入 app 并 hash verified。

## 板端证据

第一次状态机板测：

- log：`board_logs/2026-07-07-07-18-20-fall-alert-state-machine-no-retry.log`
- summary：`board_logs/2026-07-07-07-18-20-fall-alert-state-machine-no-retry.summary.json`
- `sequence=22` 输出 `fall_prob=0.9707` 后立即 `fall_alert_confirmed`。
- `fall_app_upload_queued: alert_sequence=1 sequence=22 retry=0` 后，`watch_endpoint: danger alert dispatched: type=fall prob=0.9707 seq=1`。
- 后续 FALL 窗口没有重复出现新的 `fall_alert_confirmed` 或新的 App 上传。
- 连续低风险窗口后出现 `fall_alert_cleared: alert_sequence=1 sequence=28 clear_windows=3 fall_prob=0.1480 clear_threshold=0.50`。
- 该轮发现 `haptic_alert_player` 和 `audio_alert_player` task 创建因 `ESP_ERR_NO_MEM` 失败，但 `display_alert: danger overlay shown` 正常。

PSRAM task 修复后第二次板测：

- log：`board_logs/2026-07-07-07-26-15-fall-alert-state-machine-psram-alert-tasks.log`
- summary：`board_logs/2026-07-07-07-26-15-fall-alert-state-machine-psram-alert-tasks.summary.json`
- `sequence=12` 输出 `fall_prob=0.8520` 后立即 `fall_alert_confirmed`。
- 本地完整告警成功：
  - `haptic_alert_player: initial danger haptic started`
  - `haptic_alert_player: initial danger haptic finished`
  - `display_alert: danger overlay shown`
  - `audio_alert_player: warning playback started`
  - `audio_alert_player: warning playback finished`
- App 上传成功：`watch_endpoint: danger alert dispatched: type=fall prob=0.8520 seq=1`。
- 清除成功：`fall_alert_cleared: alert_sequence=1 sequence=19 clear_windows=3 fall_prob=0.1480 clear_threshold=0.50`。
- summary `panic_log_seen=false`，无残留 monitor。

## 结论

- 跌倒状态机按用户要求实现：单窗口确认、同一次确认期间不重复告警/上传、连续 3 个低风险窗口清除。
- 本地完整告警和 App 上传在 COM7 已闭环；haptic/audio 因 internal RAM 峰值失败的问题已通过 PSRAM task 栈修复并实测。
- 后续重点不在状态机，而在真实样本评估：需要离线 replay/evaluator 统计 ADL 误报、FALL 召回和 `0.50~0.80` 灰区动作。
