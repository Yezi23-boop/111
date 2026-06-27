---
id: ai-memory-watch-hermes-v2-2-foreground-ws-background-conversation-polling-plan
tags: context, plans, ai-memory-watch, hermes, websocket, conversation, polling, esp32s3, v2.2
summary: AI Memory Watch / Hermes V2.2 执行计划：前台 Hermes 页面使用 WebSocket 实时体验，离页后关闭 WS，pending 任务改用 HTTP conversation polling 取回 Hermes 回复。
last_reviewed: 2026-06-27
memory_type: task
scope: task
owners: docs/context/plans/active/2026-06-27-ai-memory-watch-hermes-v2.2-foreground-ws-background-conversation-polling-plan.md
triggers: AI Memory Watch V2.2, Hermes foreground websocket, background conversation polling, pending reply, last_seen_conversation_id
evidence_level: design
status: active
---

# AI Memory Watch / Hermes V2.2 前台 WS + 后台 Conversation Polling 执行计划

## 目标与全局

- 目标：把 V2.1 的“WS 等到最终回复”升级为更适合手表的前后台通讯模型：前台 Hermes 页面保持 WS 实时体验，离开 Hermes 页面后关闭 WS 释放资源，未完成任务用 HTTP conversation polling 低频取回结果。
- 当前已知：V2.1 已完成 `WSS /v1/watch/ws`、server `watch_conversation`、ESP32 本地最近 5 轮显示缓存、ASR 先显示用户侧消息、assistant reply 后显示 Hermes 侧消息。
- 本轮要解决：用户离开 Hermes 页面后不再保持 WS 长连接，但 Hermes 任务不能丢；server conversation store 必须成为 pending 任务的结果真相源。

固定目标状态：

```text
前台 Hermes：WS 实时
离页 pending：HTTP conversation polling，每 5 秒一次
单次请求 timeout：4 秒
总等待：10 分钟 / 600000 ms
无 pending：只 inbox 低频轮询
重新进入 Hermes：先显示本地 conversation cache；下一次 WS auth 携带 last_seen_conversation_id 同步缺失消息
```

## 范围与非目标

本轮明确要做：

- server 增加 `GET /v1/watch/conversation`，按 `device_id + after` 返回 conversation messages。
- server 修改 WS `audio_end` 后的任务处理：ASR/Hermes job 必须后台化，WS 断开只影响实时推送，不影响 conversation 落库。
- ESP32 增加 conversation HTTP polling client，复用 watch endpoint `base_url/device_id/device_token`。
- `memory_watch_service` 持有 pending request、last seen conversation id、前后台状态和 conversation poll 调度。
- Hermes 页面前台进入时先显示本地 conversation cache；下一次语音 WS auth 携带 `last_seen_conversation_id`，同步缺失消息。
- Hermes 页面离开时关闭当前 WS；若有 pending request，启动 conversation polling。
- 离页后 assistant reply 到达：合并本地 conversation cache，清 pending，弹“回复已到达”全局气泡。

本轮不做：

- 不做多设备、多入口。
- 不把普通 conversation reply 写入 inbox。
- 不让 inbox 走 WebSocket。
- 不做 MQTT 唤醒。
- 不做 TTS。
- 不把 ESP32 本地缓存升级为长期历史存储。
- 不修改 `official_chat` 业务主线。
- 不公开 Hermes `8642` 或 Dashboard `9119`。

## 接口与协议

新增 HTTP conversation endpoint：

```http
GET /v1/watch/conversation?device_id=watch-001&after=msg_xxx
Authorization: Bearer <device-token>
```

`after` 可选：

- 有 `after` 且 message_id 存在：返回该 message 之后最多 20 条。
- 无 `after`：返回最近 20 条，按旧到新排序。
- `after` 未知：回退最近 20 条，和当前 `ConversationRepo.list_after()` 行为一致。

响应：

```json
{
  "messages": [
    {
      "message_id": "msg_xxx",
      "request_id": "watch-001-xxxx-0001",
      "role": "assistant",
      "text": "已帮你整理电池日志重点。",
      "created_at": "2026-06-27T10:30:00Z",
      "status": "done"
    }
  ],
  "has_more": false
}
```

约束：

- 复用 device token 鉴权，不新增 key。
- 不返回 token、Authorization、Hermes 原始响应、音频内容。
- conversation message schema 与 WS `conversation_snapshot/conversation_message` 保持一致。
- `has_more` V2.2 固定返回 `false`，因为 server 每 device 只保留最近 20 条；未来分页再扩展。

## 实现阶段

### 阶段 1：server 断线安全

- 将 WS `audio_end` 后的处理拆成后台 job：
  - 复制 finished audio bytes 和 request_id。
  - 后台执行 ASR。
  - ASR 成功后写 `role=user` conversation。
  - Hermes 完成后写 `role=assistant` conversation。
  - WS 仍在线时 best-effort 推 `asr_result/task_started/conversation_message`。
  - WS 已断开时只落库，不把断开视为任务失败。
- 保留现有 WS 实时行为和旧 HTTP fallback。
- 新增 `GET /v1/watch/conversation` 和 pytest。

### 阶段 2：ESP32 HTTP conversation client

- 在 `memory_watch_voice_client` 增加 conversation polling 窄接口。
- 构建路径 `/v1/watch/conversation?device_id=<encoded>&after=<encoded>`。
- 单次 timeout 固定 4000 ms。
- 解析最多 20 条 messages；调用方只合并最近 10 条显示缓存。
- 响应体上限按 conversation 20 条设置受控上限，不放 task 栈。
- 日志不打印 token，不打印 Authorization。

### 阶段 3：memory_watch_service 前后台状态

- 新增或收敛以下 service 私有状态：
  - `foreground_active`
  - `pending_request_id`
  - `pending_started_at_ms`
  - `last_seen_conversation_id`
  - `conversation_poll_busy/pending`
- 前台 Hermes：
  - 进入页面时打开新 WS。
  - auth 带 `last_seen_conversation_id`。
  - 合并 `conversation_snapshot`。
  - ASR 到达立即显示用户侧消息。
  - assistant reply 到达立即显示 Hermes 侧消息，并更新 `last_seen_conversation_id`。
- 离开 Hermes：
  - 关闭当前 WS。
  - 不取消 `pending_request_id`。
  - 若 pending 未完成，启动每 5 秒 conversation polling。
- 后台 pending：
  - 每 5 秒 poll 一次。
  - 每次最多等 4 秒。
  - 总等待 10 分钟，超时后停止 polling，并把当前 request 标记超时。
  - 拉到 assistant `status=done/error/timeout/canceled` 后停止 polling。
- 无 pending：
  - 不 poll conversation。
  - 只保留现有 inbox 低频轮询。

### 阶段 4：UI 与气泡

- Hermes 页面只显示 conversation 流，不把 ASR 当全局通知。
- 离页后 ASR 到达只写本地 cache，不弹全局气泡。
- 离页后 assistant reply 到达才弹“回复已到达”。
- 点气泡进入 Hermes 页面后：
  - 先显示本地 cache。
  - 下一次语音 WS auth 用 `last_seen_conversation_id` 补齐缺失消息。

## 进度

- `[x]` V2.1 已完成前台 WS、server conversation store、ASR 先显用户侧消息。
- `[x]` server：实现 WS 断开后 ASR/Hermes 后台 job 仍继续并落库。
- `[x]` server：新增 `GET /v1/watch/conversation`。
- `[x]` server：补 pytest 和公网脚本验证 conversation polling。
- `[x]` ESP32：新增 conversation HTTP client。
- `[x]` ESP32：`memory_watch_service` 新增前后台 WS/polling 状态机。
- `[x]` ESP32：离页关闭 WS，有 pending 时每 5 秒 poll，4 秒 timeout，总等待 10 分钟。
- `[x]` ESP32：重新进入 Hermes 时先显示本地 conversation cache；下一次 WS auth 携带 `last_seen_conversation_id`。
- `[x]` UI：离页 assistant reply 到达弹“回复已到达”，ASR 不弹全局气泡。
- `[x]` 验证：脚本模拟 WS 断开后仍可通过 HTTP conversation 拉到 reply。
- `[x]` 验证：用户真机反馈前台 Hermes 页面可正常使用，离开 Hermes 页面后也能通过气泡收到回复。
- `[x]` 修复：离页 conversation polling 拉到 terminal assistant 后，避免同一回复先由 server conversation 合并、再由 worker done 二次追加到本地对话。
- `[x]` 验证：重复回复修复已 `idf.py build`、`idf.py -p COM3 app-flash`，30 秒启动 smoke 通过。
- `[x]` 验证：用户真机确认刷入重复回复修复后，当前实测无问题；前台/离页场景不再重复显示回复。

## 验证与验收

server 验证：

```powershell
uv run python -m pytest server/watch_voice_endpoint/tests -q
.\server\watch_voice_endpoint\runtime_status.ps1 -BaseUrl "https://watch.934000.xyz" -SkipDocker -SkipHermesApi -SkipServiceHealth -AssertPrivateNotExposed
.\server\watch_voice_endpoint\websocket_smoke_test.ps1 -BaseUrl "wss://watch.934000.xyz/v1/watch/ws"
```

需要新增或扩展脚本验收：

```powershell
# 伪流程：WS 上传 audio_end 后主动断开，随后用 HTTP conversation polling 拉 reply。
.\server\watch_voice_endpoint\conversation_polling_smoke_test.ps1 -BaseUrl "https://watch.934000.xyz"
```

ESP32 验证：

```powershell
uv run python -m pytest tests/test_memory_watch_service_source.py tests/test_memory_watch_ui_source.py tests/test_memory_watch_ws_client_source.py -q
idf.py build
.\scripts\board\agent_serial_monitor.ps1 -Port COM3 -Action app-flash-monitor -DurationSeconds 120 -FlashTimeoutSeconds 240 -Tag hermes-v22-background-conversation-polling -Pattern 'memory_watch|conversation|inbox|voice-ws|websocket|watch request result|reply arrived|panic|Guru|stack overflow|HTTP_CLIENT|Error parse url'
```

真机验收步骤：

1. 进入 Hermes 页面。
2. 按住说话发起复杂任务。
3. ASR 到达后用户侧消息先显示。
4. 立刻离开 Hermes 页面。
5. 确认 WS 关闭，pending 未取消。
6. 后台 conversation polling 每 5 秒尝试一次。
7. Hermes reply 到达后弹“回复已到达”。
8. 点气泡回 Hermes 页面，看到用户消息和 Hermes 回复。
9. 无 Guru、panic、stack overflow、URL 乱码、token 泄露。

## 决策记录

- 日期：2026-06-27
- 决策：V2.2 离页后关闭 WS，不保持长连接。
- 原因：WS 是实时通道，不是任务本体；任务真相源应是 server conversation store。

- 日期：2026-06-27
- 决策：离页 pending 使用 HTTP conversation polling，间隔 5 秒、单次 timeout 4 秒、总等待 10 分钟。
- 原因：开发期和 V2.2 第一版体验优先，只需要比一直保持 WS 少占一点资源，后续低功耗版本再拉长间隔。

- 日期：2026-06-27
- 决策：重新进入 Hermes 页面时不为空闲补齐单独新建 WS；页面先显示本地 conversation cache，下一次语音 WS auth 携带 `last_seen_conversation_id`。
- 原因：V2.2 已有后台 conversation polling 负责离页 pending 补齐；空闲重进页面再开 WS 会增加资源占用，不符合“离页关闭 WS”的资源模型。

## 幂等与恢复

- 如果 server 后台 job 改造失败，保持 V2.1 WS 等待模式可回退。
- 如果 HTTP conversation polling 失败，旧 WS 前台实时链路仍应可用。
- 如果 ESP32 后台 polling 引入资源问题，先保留 server `GET /conversation` 和前台 WS，回退离页 pending 为 V2.1 行为。
- 所有 key/token 只放仓库外 env、NVS 或 sdkconfig 开发配置；文档、日志、测试输出不得打印真实值。
