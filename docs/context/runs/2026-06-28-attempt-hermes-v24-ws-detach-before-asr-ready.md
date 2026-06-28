---
id: attempt-hermes-v24-ws-detach-before-asr-ready
tags: context, runs, ai-memory-watch, hermes, v2.4, websocket, sync, freertos, event-group, esp32s3
summary: 记录 V2.4 真机离页后一直思考中的根因：ESP32 在 server 创建 session 前关闭 WS，导致后台 /sync 持续 session=none；修复为等 ASR ready 后再 detach。
last_reviewed: 2026-06-28
memory_type: run
scope: project
owners: docs/context/runs
triggers: AI Memory Watch V2.4 thinking forever, background sync session none, WebSocket detach, ASR ready
evidence_level: observed
---

# V2.4 WS 过早 detach 导致后台 /sync 一直 session=none

## 错误签名

真机 1 分钟监控：

- `board_logs/2026-06-28-19-52-48-hermes-v24-stage6-background-sync-bubble-60s-retry.log`
- ESP32 录音成功：`voice record result: err=ESP_OK audio_len=6526 dur_ms=2220`
- WS 已连接：`official_ws_client: websocket connected`
- 随后本地关闭：`official_ws_client: closing websocket locally`
- ESP32 启动后台 pending：`conversation: background polling started request_id=watch-001-f8bc9e26-0001`
- 后台 `/sync` 多次返回：`conversation: sync ok messages=0 session=none terminal=0`
- 手表 UI 表现：一直“思考中”。

server 侧复核：

```text
watch_session_count=0
watch_conversation_count=0
```

server access log 只有这条 request 的 `/v1/watch/sync?...pending_request_id=watch-001-f8bc9e26-0001...`，没有对应 session/conversation 数据。

## 根因

ESP32 在 `audio_end` 发送后，如果用户已经离开 Hermes 页面，会立即关闭 WS 并返回 `conversation_pending`。但 server 只有在收到并处理 `audio_end` 后才会执行 `_ws_finish_audio()`，创建 session、做 ASR、写 user conversation。

在这次实测中，ESP32 关闭 WS 的时机早于 server 创建 session，导致：

1. ESP32 本地认为 request 已交给后台。
2. server 实际没有 session/conversation。
3. 后台 `/sync` 永远只能返回 `session_state=none`。
4. UI 一直停在“思考中”。

## 修复

固件侧新增 `kWsWaitAsrReadyBit` 和局部 `asr_ready_seen`：

- `MEMORY_WATCH_WS_EVENT_TURN_ASR_READY` 到达时设置 `kWsWaitAsrReadyBit`。
- `memory_watch_service_send_voice_over_ws()` 在检测到已离开 Hermes 页面时，不再立即关闭 WS。
- 只有 `asr_ready_seen=true`，也就是 server 已经 ASR 完成并写入 user conversation 后，才允许关闭 WS 并切后台 `/sync`。
- 如果前台继续等待，仍按原逻辑等 assistant reply、error、disconnect 或超时。

这个策略保持 ESP32 thin client 思路：手表不理解 server 内部 session 状态，只要求在 detach 前拿到“server 已接管”的最小确认。

## 验证

```text
Memory Watch source tests: 40 passed
server test_sync.py: 14 passed
idf.py build: passed
111.bin: 0xabef90
app free: 0x341070 / 23%
```

## 复测

`idf.py -p COM3 app-flash` 后，用户重新做阶段 6 真机场景：

1. Hermes 页面按住说慢任务。
2. 松手后立刻离开 Hermes 页面。
3. 观察后台 `/sync` 是否还会长期 `session=none`。

用户确认：后台 `/sync` 不再长期 `session=none`，此前离页后一直“思考中”的阻塞解除。

## 后续

V2.4 阶段 6 已可收尾。后续如继续优化，应转向体验细节、低功耗参数或计划归档，而不是继续按本错误签名排查。
