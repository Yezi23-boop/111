---
id: 2026-05-13-attempt-official-chat-microphone-arbitration
tags: [run, official-chat, audio, resource-management, safety-monitor]
summary: official_chat 接入 audio_codec input session 仲裁，进入前台时通知后台管理器暂停 Safety Monitor，adapter 用 OFFICIAL_CHAT owner 申请/释放麦克风。
result: success
status: active
last_reviewed: 2026-05-15
memory_type: episodic
scope: repo
owners: components/official_chat/audio/local_audio_codec_adapter.cc, components/official_chat/audio/local_audio_codec_adapter.h, main/services/official_chat_service.c, tests/test_audio_codec_port_source.py, tests/test_official_chat_service_source.py
triggers: official_chat, microphone, audio_codec, acquire_input, foreground_audio, safety_monitor
evidence_level: verified
---

# Attempt Log: official-chat-microphone-arbitration

## 背景

- 本次要验证什么：把 official_chat 从直接读取 `audio_codec_read()` 的隐式麦克风使用，接入正式 input session 仲裁。
- 对应任务或计划：official-chat-microphone-arbitration
- 结果状态：success
- 长期记录理由：owner-architecture, framework-constraint, evidence

## 环境

- 分支/工作区状态：未记录
- 设备/串口/板型：source tests、build，后续用户已上板验收 AI 页进入/退出
- 关键前置条件：Safety Monitor 已由 `background_service_manager` 管理后台运行

## 操作

- `LocalAudioCodecAdapter` 使用 `input_session_acquired_` 作为麦克风启用的单一事实；`EnableInput(true)` 成功申请 `AUDIO_CODEC_OWNER_OFFICIAL_CHAT` 后才允许 `InputData()` 读取麦克风，`EnableInput(false)` 与 `Shutdown()` 负责释放。
- `official_chat_service` 在 AI 前台入口声明 `foreground_audio_active=true`，在离开前台和 shutdown 请求路径声明 false，让 `background_service_manager` 先暂停 Safety Monitor。
- 本轮不改变 official_chat 的 STL 音频队列、Opus 队列和 public API，也不接入 official_chat output session。

## 观测

- `uv run python -m pytest tests/test_audio_codec_port_source.py tests/test_official_chat_service_source.py tests/test_safety_monitor_session_source.py`：18 passed。
- `. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`：通过，生成 `build/111.bin`；app 大小 `0x8d5bf0`，factory 剩余 `0x12a410`（12%）。

## 结论

- 2026-05-13 用户上板验收通过：安全监听运行中进入 AI 页，official_chat 前台音频声明后 Safety Monitor 可停止或被麦克风资源阻塞；退出/关闭 AI 后，Safety Monitor 按用户开关恢复。
- official_chat 麦克风使用已经进入 `audio_codec` input owner 仲裁，不再绕过后台 Safety Monitor 资源判断。

## 未验证风险

- 本轮不处理 official_chat output session；后续已作为独立 gate 处理。
