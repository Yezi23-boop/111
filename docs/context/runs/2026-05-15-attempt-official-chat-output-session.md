---
id: 2026-05-15-attempt-official-chat-output-session
tags: [run, official-chat, audio, resource-management, output-session]
summary: official_chat 输出播放接入 audio_codec output session，补齐框架复查发现的播放 owner 缺口。
result: success
status: active
last_reviewed: 2026-05-15
memory_type: episodic
scope: repo
owners: components/official_chat/audio/local_audio_codec_adapter.*, tests/test_audio_codec_port_source.py
triggers: official_chat, audio_output, output_session, audio_codec, resource_framework
evidence_level: verified
---

# Attempt Log: official-chat-output-session

## Goal

修复框架复查发现的缺口：`official_chat` 的输出播放路径此前直接 `audio_codec_write()`，未持有 `audio_codec` output session，导致 AI 播放不会出现在 output owner snapshot 中。

## Changes

- `LocalAudioCodecAdapter` 使用 `input_session_acquired_ / output_session_acquired_` 作为麦克风和播放启用的单一事实，不再维护额外 `input_enabled_ / output_enabled_` 影子状态。
- `EnableOutput(true)` 成功申请 `audio_codec_acquire_output(AUDIO_CODEC_OWNER_OFFICIAL_CHAT, 500U)` 后才启用 PA。
- `EnableOutput(false)` 仅在本地确实持有 session 时关闭 PA 并释放 `AUDIO_CODEC_OWNER_OFFICIAL_CHAT`，避免未持有 session 的关闭路径触碰播放硬件。
- `OutputData()` 在未持有 output session 时直接返回，避免绕过仲裁写播放链路。
- `Shutdown()` 兜底调用 `EnableOutput(false)`，避免异常关闭遗留 output owner。

## Evidence

- `uv run python -m pytest tests/test_audio_codec_port_source.py tests/test_official_chat_service_source.py tests/test_safety_monitor_session_source.py`：18 passed。
- `uv run python scripts/context/validate_context.py --level standard --q "official_chat output session audio_codec acquire_output release_output" --brief`：通过。
- `. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`：通过。

## Notes

- 本轮只补 output session owner，不新增 output router，不改变 official_chat public API。
- P0 alert 的抢占策略仍由 `app_alert_manager` 和 `audio_codec` output owner snapshot 处理；本轮不扩展成通用抢占调度器。
