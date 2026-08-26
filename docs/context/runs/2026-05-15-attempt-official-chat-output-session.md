---
id: 2026-05-15-attempt-official-chat-output-session
tags: [run, official-chat, audio, resource-management, output-session]
summary: official_chat 输出播放接入 audio_codec output session，补齐框架复查发现的播放 owner 缺口。
result: success
last_reviewed: 2026-05-15
memory_type: episodic
scope: repo
owners: components/official_chat/audio/local_audio_codec_adapter.cc, components/official_chat/audio/local_audio_codec_adapter.h, tests/test_audio_codec_port_source.py
triggers: official_chat, audio_output, output_session, audio_codec, resource_framework
evidence_level: verified
---

# Attempt Log: official-chat-output-session

## 背景

- 本次要验证什么：修复框架复查发现的缺口，让 `official_chat` 的输出播放路径持有 `audio_codec` output session。
- 对应任务或计划：official-chat-output-session
- 结果状态：success
- 长期记录理由：owner-architecture, framework-constraint, evidence

## 环境

- 分支/工作区状态：未记录
- 设备/串口/板型：本轮以 source tests 和 build 为主
- 关键前置条件：official_chat input session 仲裁已落地，P0 alert output owner 已落地

## 操作

- `LocalAudioCodecAdapter` 使用 `input_session_acquired_ / output_session_acquired_` 作为麦克风和播放启用的单一事实，不再维护额外 `input_enabled_ / output_enabled_` 影子状态。
- `EnableOutput(true)` 成功申请 `audio_codec_acquire_output(AUDIO_CODEC_OWNER_OFFICIAL_CHAT, 500U)` 后才启用 PA。
- `EnableOutput(false)` 仅在本地确实持有 session 时关闭 PA 并释放 `AUDIO_CODEC_OWNER_OFFICIAL_CHAT`，避免未持有 session 的关闭路径触碰播放硬件。
- `OutputData()` 在未持有 output session 时直接返回，避免绕过仲裁写播放链路。
- `Shutdown()` 兜底调用 `EnableOutput(false)`，避免异常关闭遗留 output owner。

## 观测

- `uv run python -m pytest tests/test_audio_codec_port_source.py tests/test_official_chat_service_source.py tests/test_safety_monitor_session_source.py`：18 passed。
- `uv run python scripts/context/validate_context.py --level standard --q "official_chat output session audio_codec acquire_output release_output" --brief`：通过。
- `. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`：通过。

## 结论

- 本轮只补 output session owner，不新增 output router，不改变 official_chat public API。
- P0 alert 的抢占策略仍由 `app_alert_manager` 和 `audio_codec` output owner snapshot 处理；本轮不扩展成通用抢占调度器。

## 未验证风险

- 需要继续用真机 AI 播放和 P0 alert 并发日志确认 output owner 可观测性与提醒优先级。
