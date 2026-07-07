---
id: run-fall-danger-alert-silent-audio-2026-07-08
tags: context, runs, attempt-log, imu, fall-detection, danger-alert, audio-alert, app-alert-manager
summary: 固定跌倒告警默认可上传 danger alert，但本机不播放危险提示音。
last_reviewed: 2026-07-08
memory_type: run
scope: imu
status: completed
owners: main/services/fall_detection_service.c, main/features/alerts/app_alert_manager.c, docs/context/plans/active/FALL_DETECTION_V1_111_DEPLOYMENT_DESIGN.md
evidence_level: build
---

# Attempt Log: Fall Danger Alert Silent Audio

## 决策

- 跌倒模型确认后可以继续调用 `watch_endpoint_service_post_danger_alert()` 上传 `danger_type="fall"`。
- `APP_ALERT_SOURCE_FALL_DETECTION` 默认不播放 `audio_alert_player` 的危险提示音，也不抢占普通音频输出。
- 本地告警仍保留 `app_alert_manager` 的屏幕/震动路径，便于板端观测和后续 UI 证据。

## 验证

- `uv run python -m unittest tests.test_fall_detection_service_source tests.test_haptic_alert_player_source tests.test_danger_detection_service_source tests.test_audio_codec_port_source`：33 tests passed。
- `git diff --check -- main/services/fall_detection_service.c main/services/fall_detection_service.h main/features/alerts/app_alert_manager.c tests/test_fall_detection_service_source.py`：无 whitespace error，仅 LF/CRLF warning。
- `. "$env:IDF_PATH\export.ps1"; idf.py build`：通过；`111.bin` size `0xae0cb0`，最小 app 分区剩余 `0x31f350` / 22%。
