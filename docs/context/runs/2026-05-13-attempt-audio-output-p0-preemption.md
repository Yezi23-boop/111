---
id: attempt-audio-output-p0-preemption-20260513
tags: run, watch, resource-management, audio, p0-alert, safety-monitor
summary: audio-output-p0-preemption；结果：success。
status: active
last_reviewed: 2026-05-13
evidence_level: verified
owners: components/audio_codec, components/mp3_player, main/features/alerts/audio_alert_player.c, main/features/alerts/app_alert_manager.c
---

# Attempt Log: audio-output-p0-preemption

## Goal

按手表整体资源框架执行“音频资源仲裁与 P0 提醒抢占”的最小代码切片：普通播放通过 `audio_codec` output session 表达占用，P0 危险提醒在播放前让普通播放让路，并由 `audio_alert_player` 持有自己的 P0 output session。

## Changes

- `audio_codec_owner_t` 增加 `AUDIO_CODEC_OWNER_ALERT_PLAYER`，用于区分 P0 危险提醒和普通 `AUDIO_CODEC_OWNER_AUDIO_PLAYER`。
- `mp3_player` 播放、暂停、恢复、停止和退出路径接入 `audio_codec_acquire_output/release_output`。
- `app_alert_manager` 在 P0 危险提醒前读取 output session 快照；如果普通播放持有输出，打印 `resource_preempt` 并调用 `mp3_player_stop()`。
- `audio_alert_player` 播放固定提示音前申请 `alert_player` output session，播放结束后释放。

## Evidence

- `uv run python -m pytest tests/test_audio_codec_port_source.py tests/test_safety_monitor_session_source.py tests/test_danger_detection_controller_source.py tests/test_power_integration_source.py`：19 passed。
- `. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`：success，生成 `build/111.bin`，app partition 剩余约 12%。

## Boundary

- 本次没有引入通用 `ResourceManager`。
- 本次不处理 `official_chat` 的 output session；它属于前台语音链路，后续应单独确认 P1 语音输出与 P0 危险提醒的关系。
- 本次不实现普通播放自动恢复；提醒结束后是否恢复仍由播放 owner 决定。
