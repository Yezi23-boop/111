---
id: hermes-multi-agent-architecture
tags: project, architecture, hermes, ai-memory-watch, conversation, websocket, async-reply, inbox-polling, v2.1, v2.2
summary: AI Memory Watch / Hermes 固件侧会话架构锚点（V2.1/V2.2 起落地）：前台对话走 WebSocket，离页 pending 用 HTTP conversation polling；收件箱走独立低频 HTTP 轮询；server conversation 是断线补发真相源，ESP32 只缓存最近 5 轮；Relay 演进见 V2.7/V2.9 completed 计划。
last_reviewed: 2026-06-27
memory_type: project_knowledge
scope: repo
owners: docs/context/knowledge/project/hermes-multi-agent-architecture.md
triggers: Hermes async reply, Hermes conversation, watch conversation, AI Memory Watch V2.1, AI Memory Watch V2.2, WebSocket, WSS, conversation polling, inbox polling, async Hermes task
evidence_level: design
status: active
route_area: "Hermes WebSocket async conversation reply architecture"
---

# Hermes WebSocket 与后台 Conversation Polling 异步对话回传草案

> 日期：2026-06-27
> 状态：V2.1/V2.2 需求框架草案

## 0. 当前结论

V2.1 的真实主线不是先做 `watch_notify` 主动通知工具，而是：

```text
手表发起复杂 Hermes 任务后，用户可以退出 Hermes 页面；
Hermes 完成后，回复回到 Hermes 对话页；
如果用户不在 Hermes 页面，则用气泡提醒“回复已到达”。
```

一句话：

```text
V2.1 = 手表 Hermes 对话的异步回传机制。
```

V2 已完成范围仍以 `docs/context/plans/completed/2026-06-25-hermes-inbox-global-notification-plan.md` 为准：

```text
V2 = Hermes 主动提示回到手表：server inbox + ESP32 收件箱 + 全局气泡通知。
```

V2.1 要新增的是活跃 conversation 的 WebSocket 通道，不是把普通回复塞进 inbox，也不是让 inbox 强行复用 WebSocket 长连接。

## 1. 产品目标

用户在 Hermes 页面按住说话，向 Hermes 提出复杂要求，例如：

```text
帮我分析最近的电池日志，找出耗电原因。
```

期望体验：

- 用户上传语音后，不必一直停留在 Hermes 页面等待。
- ASR 文本尽快显示在 Hermes 对话页面，让用户看到“刚才说了什么”。
- Hermes 可以慢慢执行复杂任务。
- Hermes 完成后，回复写入 conversation。
- 如果用户仍在 Hermes 页面，回复通过 WebSocket 直接显示在当前对话流。
- V2.2 起如果用户已经退出 Hermes 页面但还有 pending request，`memory_watch_service` 关闭前台 WebSocket，并用 HTTP `GET /v1/watch/conversation` 每 5 秒轮询结果。
- 如果当前没有 pending request，退出 Hermes 页面后可以关闭 WebSocket，避免为了收件箱长期占用连接资源。
- 收件箱通知独立走低频 HTTP 轮询，不依赖 WebSocket 常开。
- 用户点气泡回到 Hermes 页面，看到连续对话记录。
- Hermes 页面可以显示多次对话记录，而不是只显示最后一次回复。

## 2. 固定非目标

V2.1 不做：

- 不做多设备，目标固定 `watch-001`。
- 不做多入口，不接网页、终端、微信等入口。
- V3 默认也不做多入口、多设备；如果未来路线变化，需要另起计划。
- 不做完整多 agent 编排。
- 不做 TTS 语音下发。
- 不做手机通知聚合。
- 不把普通对话回复写入 inbox。
- 不让 ESP32 端新增多个 Hermes 后台服务。
- 不把 conversation 历史长期保存在 ESP32 作为真相源。
- 不为了收件箱常驻 WebSocket；没有活跃对话任务时，WebSocket 可以关闭。

## 3. 两条消息通道

### 3.1 conversation_reply

用途：手表用户和 Hermes 的连续对话。

显示位置：

```text
Hermes 页面 UI
```

特点：

- 保存 user / assistant 消息。
- 支持多轮历史显示。
- 活跃任务通过 WebSocket 回传 ASR、任务状态和 assistant reply。
- 后台完成时可以弹“回复已到达”气泡。
- 不进入收件箱 inbox。
- 不使用 inbox 的 `kind/title/preview/body` 结构。

### 3.2 proactive_inbox

用途：Hermes 主动提醒、主动提示、任务结果通知。

显示位置：

```text
收件箱 + 全局气泡
```

特点：

- 使用 V2 已完成的 inbox 机制。
- 单独走低频 HTTP 轮询；进入 Hermes 页面、打开收件箱或后台维护窗口时拉取。
- 只读通知，不参与 Hermes 对话流。
- 与 conversation 分表、分接口、分 UI 语义。
- 不要求 WebSocket 为了 inbox 长期开着。

## 4. 推荐主流程

V2.1 主流程：

```text
1. 用户在 Hermes 页面按住说话。
2. memory_watch_service 建立或复用 WSS /v1/watch/ws。
3. ESP32 通过 WebSocket 发送 auth，携带 device_id/device_token 和 last_seen_conversation_id。
4. server 返回 auth_ok，并按 last_seen_conversation_id 补发断线期间缺失的 conversation messages。
5. ESP32 发送 audio_start，然后连续发送 Ogg Opus binary chunks，松手后发送 audio_end。
6. server 做 ASR，得到用户文本。
7. server 写入 conversation：user message。
8. server 通过 WebSocket 推 asr_result，ESP32 Hermes 页面先显示用户刚才说了什么。
9. server 后台把 user text 交给 Hermes 执行，并通过 WebSocket 推 task_started。
10. Hermes 完成后，server 写入 conversation：assistant message。
11. 如果 WebSocket 仍在线：server 通过 WebSocket 推 conversation_message。
12. 如果 WebSocket 已断开：server 只保存消息，等待下次重连按 last_seen_conversation_id 补发。
13. 如果 Hermes 页面在前台：直接显示回复；如果不在前台：弹“回复已到达”气泡。
```

收件箱不走这条实时任务链路：

```text
proactive_inbox 继续通过 HTTP 低频轮询获取；
没有 pending conversation request 时，WebSocket 可以关闭。
```

## 5. 存储边界

### 5.1 server 是 conversation 真相源

V2.1 新增 server 侧 conversation store，建议使用 SQLite。

保留策略：

```text
每个 device 保留最近 20 条 conversation messages。
```

注意：20 条是 message 数，不是 20 轮；`user` 和 `assistant` 各算一条，因此大约是最近 10 轮。

### 5.2 ESP32 只做显示缓存

ESP32 本地只缓存最近几轮，用于 Hermes 页面快速显示。

当前口径：

```text
ESP32 缓存最近 5 轮对话。
```

推荐 V2.1 先放 PSRAM：

- 不磨 flash。
- 不引入 SD card 生命周期。
- 重启后可从 server conversation 重新拉取。

Flash / NVS / SD card 可作为后续离线历史体验再讨论，不作为 V2.1 必做。

## 6. server 表设计

conversation 和 inbox 分开两张表：

```text
watch_inbox
watch_conversation
```

原因：

- inbox 是主动通知，有 `notification_id/kind/title/preview/body/read`。
- conversation 是对话历史，有 `message_id/request_id/role/text/status/created_at`。
- 两者 UI、生命周期和语义不同；共表会让后续逻辑混乱。

## 7. WebSocket conversation 通道

V2.1 的活跃 Hermes 对话主通道改为 WebSocket，不复用 inbox。

```text
WSS /v1/watch/ws
```

连接后第一条消息必须认证：

```json
{
  "type": "auth",
  "device_id": "watch-001",
  "device_token": "***",
  "last_seen_conversation_id": "msg_123"
}
```

server 返回：

```json
{
  "type": "auth_ok",
  "server_time": "2026-06-27T10:30:00+08:00"
}
```

断线补发也通过 WebSocket 完成：

```json
{
  "type": "conversation_snapshot",
  "messages": [
    {
      "message_id": "msg_xxx",
      "request_id": "watch-001-boot-seq",
      "role": "user",
      "text": "帮我分析电池日志",
      "created_at": "2026-06-27T10:30:00+08:00",
      "status": "done"
    },
    {
      "message_id": "msg_yyy",
      "request_id": "watch-001-boot-seq",
      "role": "assistant",
      "text": "分析完成，主要问题是待机耗电偏高。",
      "created_at": "2026-06-27T10:31:12+08:00",
      "status": "done"
    }
  ],
  "unread_reply_count": 1
}
```

V2.1 限定：

```text
role: user | assistant
status: pending | done | error | timeout | canceled
```

### 7.1 WebSocket 音频上传帧

按住说话开始：

```json
{
  "type": "audio_start",
  "request_id": "req_xxx",
  "format": "ogg_opus",
  "sample_rate": 16000
}
```

音频数据使用 binary frame 发送：

```text
binary: Ogg Opus chunk
binary: Ogg Opus chunk
binary: Ogg Opus chunk
```

松开发送：

```json
{
  "type": "audio_end",
  "request_id": "req_xxx"
}
```

server 侧接收时应边收边写临时文件或受控缓冲，不把完整音频长期堆在 ESP32 或 server 的无界 RAM 中。

### 7.2 WebSocket 结果帧

ASR 成功：

```json
{
  "type": "asr_result",
  "request_id": "req_xxx",
  "message_id": "msg_user_xxx",
  "text": "帮我分析电池日志"
}
```

Hermes 开始执行：

```json
{
  "type": "task_started",
  "request_id": "req_xxx"
}
```

Hermes 完成：

```json
{
  "type": "conversation_message",
  "request_id": "req_xxx",
  "message_id": "msg_assistant_xxx",
  "role": "assistant",
  "text": "分析完成，主要问题是待机耗电偏高。",
  "created_at": "2026-06-27T10:31:12+08:00",
  "status": "done"
}
```

ESP32 收到后可发送轻量 ACK：

```json
{
  "type": "ack",
  "scope": "conversation",
  "message_id": "msg_assistant_xxx"
}
```

ACK 不取代 server conversation 持久化，只用于更新设备的 last seen 状态和减少重复补发。

## 8. HTTP conversation endpoint 的位置

V2.1 WebSocket-first 时，`GET /v1/watch/conversation` 主要用于调试、脚本验收或应急补偿。

V2.2 起它成为离页 pending 的正式后台取回路径：

```text
前台 Hermes 页面：WebSocket 实时
离开 Hermes 页面且 request 未完成：关闭 WS，HTTP conversation polling 每 5 秒拉取
单次 HTTP timeout：4 秒
总等待：10 分钟
```

V2.1 server 部署采用方案 A：

```text
保留现有 ai-memory-watch-voice-endpoint 容器和 127.0.0.1:8787；
在同一容器内新增 /v1/watch/ws；
旧 HTTP V1/V2 接口继续保留；
用 WATCH_WS_ENABLED 之类 feature flag 控制 WS 实验路径。
```

这样 WS 开发失败时，仍可回退到已验证的 HTTP `/v1/watch/voice-command`、`/v1/watch/inbox` 和公网 smoke。

如果保留，接口语义仍是：

```http
GET /v1/watch/conversation?device_id=watch-001
Authorization: Bearer <device_token>
```

用途：

```text
- 调试 server conversation store。
- 脚本模拟手表读取历史。
- WebSocket 实装前的过渡验证。
- 极端场景下手动补查消息。
```

前台页面仍不做 2 秒级 conversation 轮询；只有离页 pending 才启动后台 conversation polling。

## 9. 收件箱 HTTP 低频轮询

收件箱仍是 V2 已完成的主动提醒机制，不走 WebSocket 主链路。

理由：

- inbox 是“主动提醒/消息箱”，不是活跃对话任务。
- 没有 pending conversation request 时，WebSocket 可以关闭节省资源。
- 收件箱可接受低实时性，适合 60 秒级或页面触发拉取。
- 避免为了 inbox 让手表长期维持 TLS/WebSocket 连接。

收件箱推荐触发：

```text
1. 进入 Hermes 页面时拉一次 inbox unread/第一页。
2. 打开收件箱页面时拉取最近 items。
3. 后台普通运行按预算低频拉取，例如 60 秒或维护窗口。
4. 低功耗/熄屏时可暂停或跟随后续 MQTT 唤醒策略。
```

收件箱 HTTP 接口沿用 V2 已完成的语义；conversation reply 不进入 inbox。

## 10. WebSocket 开关策略

WebSocket 只服务“活跃 Hermes 对话任务”。

推荐状态机：

```text
Hermes 页面打开：
  建立或复用 WebSocket。

用户发起语音任务：
  WebSocket 保持在线，pending_request=true。

用户离开 Hermes 页面：
  如果 pending_request=true，关闭 WebSocket，启动 HTTP conversation polling。
  如果 pending_request=false，可以关闭 WebSocket。

Hermes 回复到达：
  写入本地最近 5 轮缓存。
  如果页面在前台，直接显示。
  如果页面不在前台，弹“回复已到达”气泡。
  pending_request=false。
  如果页面不在前台，可以关闭 WebSocket。
```

server 仍保存 conversation。WebSocket 断开不代表消息丢失；离页期间通过 HTTP conversation polling 取回，重连时仍可通过 `last_seen_conversation_id` 补发。

## 11. HTTP voice-command 的位置

V1 的 `/v1/watch/voice-command` 可以作为兼容和脚本 smoke 保留，但 V2.1 的正式手表交互改为 WebSocket 音频帧。

保留理由：

- 现有 V1 真机与公网 smoke 已跑通，可以继续作为回归门禁。
- 服务器侧 ASR/Hermes 链路仍可用 HTTP 脚本模拟。
- WebSocket 实装失败时有可验证退路。
- 不另起旁路 WS 容器，避免 env、device token、Cloudflare 路由和 release gate 分散；同容器加 feature flag 更容易回退。

但 ESP32 V2.1 页面主路径应使用 `WSS /v1/watch/ws`。

## 12. MQTT 未来关系

V2.1 先用 WebSocket 打通活跃对话，用 HTTP 低频轮询保留收件箱。

未来 MQTT 不承载完整消息正文，而是作为唤醒信号或 WebSocket 替代的低功耗事件层：

```text
server 写 conversation
server 发 MQTT：有新 reply 或 inbox unread
ESP32 收到 MQTT
ESP32 按需建立 WebSocket 或拉 HTTP inbox
```

固定原则：

```text
server conversation/inbox store 是数据真相源；
WebSocket 是活跃对话实时通道；
HTTP polling 是 inbox 低频通道；
MQTT 是未来的低功耗唤醒信号。
```

## 13. ESP32 owner 边界

ESP32 端仍只保留一个 Hermes owner：

```text
memory_watch_service
```

它负责：

- WebSocket 连接、认证、重连与关闭策略。
- 通过 WebSocket 上传 Ogg Opus 音频帧。
- 接收 `asr_result/task_started/conversation_message/error`。
- 维护本地最近 5 轮显示缓存。
- 合并 server conversation snapshot / 重连补发消息。
- 低频 HTTP 轮询 inbox。
- 离页 pending 时低频 HTTP 轮询 conversation。
- 向 Hermes 页面发布只读 snapshot。
- 在后台收到新 assistant reply 时通知气泡控制器。

`watch_notification_center` 仍是薄 UI 浮层：

- 只负责显示气泡、点击、右滑和跳转。
- 不联网。
- 不保存 conversation。
- 不调用 Hermes。

禁止新增长期服务：

```text
conversation_service
reply_service
task_service
inbox_network_service
```

## 14. watch_notify 的位置

`watch_notify` 不再是 V2.1 主线。

它可以作为后续 proactive inbox 能力保留，但不要混淆：

```text
conversation_reply -> Hermes 页面连续对话
proactive_inbox -> 收件箱通知
```

V2.1 优先级：

```text
1. WSS /v1/watch/ws 认证与重连
2. watch_conversation store
3. WebSocket Ogg Opus 上传
4. ASR result / task_started / assistant reply 推送
5. 断线后 last_seen_conversation_id 补发
6. 离页 pending request 用 HTTP conversation polling 取回结果
7. ESP32 后台 pending request 气泡
8. Hermes 页面多轮对话显示
9. inbox HTTP 低频轮询保持独立
```

`watch_notify` 可后续再作为主动通知工具，不阻塞 V2.1。

## 15. 待确认问题

以下问题尚未完全敲定：

- conversation 已读语义：进入 Hermes 页面是否立即清空 `unread_reply_count`。
- 是否需要单独 `POST /v1/watch/conversation/read`。
- server 后台 Hermes 任务最长执行时间、失败重试和超时状态如何落库。
- ASR 失败时是否也写入 conversation error message。
- ESP32 最近 5 轮缓存的具体内存分配方式：PSRAM 固定数组、动态分配，或后续 flash/SD。
- WebSocket binary frame 的最大 chunk 大小、单次音频最大时长和 server 临时文件清理策略。
- WebSocket 心跳间隔、重连退避和最大后台等待时间。
- inbox HTTP 轮询是否与现有 V2 worker 合并调度，避免两个 HTTP worker 竞争。

## 16. 当前定稿口径

```text
V2.1/V2.2 = 手表发起复杂 Hermes 任务后的异步对话回传。
前台活跃 Hermes 对话走单 WebSocket：auth、音频上传、ASR 回显、任务状态、assistant reply。
收件箱 proactive_inbox 不走 WebSocket，继续独立 HTTP 低频轮询。
离开 Hermes 页面后 WebSocket 关闭；如果仍有 pending request，使用 HTTP conversation polling 每 5 秒拉结果。
没有 pending request 且不在 Hermes 页面时，只保留 inbox 低频轮询。
server watch_conversation 是真相源，每 device 最近 20 条 messages。
ESP32 只缓存最近 5 轮用于显示，优先 PSRAM。
WebSocket 断开不丢消息，离页 pending 走 HTTP conversation polling，重连按 last_seen_conversation_id 补发。
HTTP voice-command 可保留为兼容和脚本验收；conversation endpoint 是 V2.2 离页 pending 正式路径。
前台直接显示新 reply，后台弹“回复已到达”气泡。
MQTT 未来只做低功耗唤醒，server conversation/inbox store 仍是数据真相源。
V2.1/V3 都不做多设备、多入口，默认只服务 watch-001。
```
