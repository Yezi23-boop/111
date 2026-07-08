---
id: 2026-07-08-attempt-official-chat-ws-preconnect
tags: context, runs, attempt-log, official-chat, websocket, ai-ui, freertos
summary: 记录 AI 页面进入后先预连接 official_chat WebSocket/音频通道的实现与验证，避免首次按住说话才进入 CONNECTING 导致按钮消失。
last_reviewed: 2026-07-08
memory_type: run
scope: attempt
owners: components/official_chat, main/services/official_chat_service.c, main/ui/custom/ai_ui_controller.c
triggers: official_chat, AI 页面, WebSocket, 预连接, 按住说话, CONNECTING, voice button
evidence_level: verified
status: completed
---

# official_chat AI 页面 WebSocket 预连接

## 背景

用户真机反馈：official_chat 激活已能成功进入 `idle`，但第一次按下语音按钮后才开始 WebSocket 连接，状态立刻变为 `CONNECTING`。AI 页面原逻辑只在 `IDLE/LISTENING` 显示按钮，因此按钮会在按下后消失，体验上表现为“按键 UI 一按就没了”。

关键证据：

- `ai_ui_voice_press_event()` 直接调用 `official_chat_service_start_listening()`。
- `HandleStartListeningEvent()` 在通道未打开时执行 `IDLE -> CONNECTING` 并异步 `OpenAudioChannel()`。
- `ai_ui_refresh_status()` 仅按 service state 判断按钮可见，没有区分“IDLE 但 WebSocket 未预连接”和“IDLE 且通道已打开”。

## 本轮改动

- `official_chat::Application` 新增 `PrepareAudioChannel()` / `IsAudioChannelReady()`。
- 新增 `kMainEventPrepareAudioChannel`、`HandlePrepareAudioChannelEvent()` 和 `ContinuePrepareAudioChannel()`：
  - 只打开协议音频通道。
  - 不调用 `SetListeningMode()`。
  - 不发送 start listening。
  - 不启用麦克风采集。
- C API 新增：
  - `official_chat_prepare_audio_channel()`
  - `official_chat_is_audio_channel_ready()`
- `official_chat_service_snapshot_t` 新增 `audio_channel_ready`，service 事件回调在状态变化时同步底层通道 ready 状态。
- AI 页面进入 `SERVICE_READY` 后：
  - 先请求 `official_chat_service_enter_foreground()`。
  - 当 service 进入 `IDLE` 但 `audio_channel_ready=false` 时，每 15 秒最多请求一次预连接。
  - 预连接未完成时隐藏按住说话按钮并显示“正在连接”。
  - 只有 `IDLE && audio_channel_ready` 或 `LISTENING` 时显示按住说话按钮。
- host preview mock 同步补齐 `official_chat_service_get_snapshot()` 和 `official_chat_service_prepare_audio_channel()`。
- 根据真机日志追加资源让路：
  - `official_chat_service` 进入前台时 acquire `FOREGROUND_RUNTIME_OWNER_OFFICIAL_CHAT`，让 ESP-DL / Safety Monitor 观察 runtime gate 后让路。
  - 进入前台和每次预连接前打开 `background_https_gate_quiet_for(8000ms)`，阻止天气、inbox、health 等低优先级 HTTPS 在 TLS 握手窗口叠加内存峰值。
  - 离开 official_chat 前台时 release runtime gate。

## 预期行为

```text
进入 AI 页面
  -> official_chat 前台启动
  -> 激活完成，状态 IDLE
  -> UI 自动请求 prepare_audio_channel
  -> 状态 CONNECTING，按钮隐藏
  -> WebSocket/音频通道打开，状态回到 IDLE
  -> audio_channel_ready=true，按钮显示
  -> 用户按住说话时直接进入 LISTENING
```

这解决的是“首次按键承担连接动作”的 UX 问题，不改变 protocol config 的 NVS/fallback 选择逻辑，也不扩大 UI 对底层 WebSocket 的所有权。

## 验证

- `uv run python -m unittest tests.test_official_chat_source tests.test_official_chat_service_source tests.test_ai_ui_entry_source`
  - 结果：`Ran 30 tests ... OK`
- `uv run python -m unittest tests.test_official_chat_source tests.test_official_chat_service_source tests.test_ai_ui_entry_source tests.test_foreground_runtime_gate_source tests.test_safety_monitor_session_source`
  - 结果：`Ran 38 tests ... OK`
- `idf.py build`
  - 预连接基础改动后：通过，`111.bin` `0xacdd90`，最小 app 分区剩余 `0x332270`，约 23%
  - 资源让路与重试节流后：通过，`111.bin` `0xacdeb0`，最小 app 分区剩余 `0x332150`，约 23%

## 注意事项

- `components/official_chat/application.cc` 在本轮开始前已经包含未提交的 activation task / SR 相关改动，`git diff` 相对 HEAD 会显示较大范围变化；本轮新增语义只围绕 WebSocket/音频通道预连接。
- 本轮尚未做真机点击验证。下一步建议刷入后观察日志：
  - 进入页面后应看到 `command: prepare_audio_channel`。
  - 进入前台后应看到 `foreground acquired: owner=OFFICIAL_CHAT` 和 `background https quiet window`。
  - WebSocket 连接应发生在按键显示前。
  - 第一次按住说话不应再因为 `CONNECTING` 状态导致按钮消失。
- 若仍出现 `esp-aes: Failed to allocate memory`，说明还存在更深的 internal RAM 峰值/碎片问题；下一步应继续看握手前 `internal_free/largest`，而不是继续缩短重试间隔。
