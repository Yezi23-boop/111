---
id: hermes-multi-agent-architecture
tags: project, architecture, hermes, ai-memory-watch, conversation, async-reply, v2.1, draft
summary: AI Memory Watch / Hermes V2.1 框架需求草案：主线改为手表发起复杂 Hermes 任务后的异步对话回传，server conversation 为真相源，ESP32 只缓存最近 5 轮用于显示；多入口、多设备不在 V2.1/V3 范围。
last_reviewed: 2026-06-27
memory_type: project_knowledge
scope: repo
owners: docs/context/knowledge/project/hermes-multi-agent-architecture.md
triggers: Hermes async reply, Hermes conversation, watch conversation, AI Memory Watch V2.1, conversation endpoint, async Hermes task
evidence_level: design
status: draft
route_area: "Hermes async conversation reply architecture"
---

# Hermes 异步对话回传与单手表架构草案

> 日期：2026-06-27
> 状态：V2.1 需求框架草案

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

V2.1 要新增的是 conversation 通道，不是把普通回复塞进 inbox。

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
- 如果用户仍在 Hermes 页面，回复直接显示在当前对话流。
- 如果用户已经退出 Hermes 页面，后台最小服务轮询到新回复后弹气泡。
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
- 只读通知，不参与 Hermes 对话流。
- 与 conversation 分表、分接口、分 UI 语义。

## 4. 推荐主流程

V2.1 主流程：

```text
1. 用户在 Hermes 页面按住说话。
2. ESP32 上传 Ogg Opus 到 watch endpoint。
3. server 做 ASR，得到用户文本。
4. server 写入 conversation：user message。
5. server 返回 accepted + asr_text + request_id。
6. ESP32 Hermes 页面先显示用户刚才说了什么，并显示处理中。
7. server 后台把 user text 交给 Hermes 执行。
8. Hermes 完成后，server 写入 conversation：assistant message。
9. ESP32 通过 conversation endpoint 轮询到 assistant reply。
10. 如果 Hermes 页面在前台：直接显示回复。
11. 如果 Hermes 页面不在前台：弹“回复已到达”气泡。
12. 用户点气泡回到 Hermes 页面，看到连续对话。
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

## 7. conversation endpoint

V2.1 新增 conversation endpoint，不复用 inbox：

```http
GET /v1/watch/conversation?device_id=watch-001
Authorization: Bearer <device_token>
```

返回最近 20 条 conversation messages。

最小响应：

```json
{
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

## 8. voice-command V2.1 响应

V2.1 的 `/v1/watch/voice-command` 不再默认等待 Hermes 最终回复。

它应该先完成：

```text
上传音频 -> ASR -> 写 user message -> 返回 accepted + asr_text
```

然后 server 后台继续执行 Hermes。

可以沿用 V1 固定 7 字段响应：

```json
{
  "request_id": "...",
  "status": "accepted",
  "action": "task_started",
  "asr_text": "帮我分析电池日志",
  "reply_text": "",
  "clarification_id": null,
  "error_code": null
}
```

ESP32 收到后：

- Hermes 页面立刻显示用户文本。
- 当前 request 标记为处理中。
- 后续通过 conversation endpoint 获取 assistant reply。

## 9. 前台/后台轮询

选择方案：

```text
A. 前台 Hermes 页面也通过 conversation endpoint 拿 reply。
```

理由：

- 前台/后台逻辑统一。
- 不长期占用 ESP32 HTTP/TLS 连接。
- 不依赖 120 秒长连接。
- 后续 MQTT 可以无缝替换“轮询触发”为“事件唤醒”。
- conversation endpoint 是数据真相源；MQTT 只做通知唤醒。

轮询节奏：

```text
Hermes 页面前台：2 秒轮询 conversation。
后台普通运行：60 秒轮询 conversation。
低功耗/熄屏：暂停或跟随维护窗口；后续 MQTT 接管唤醒。
```

前台行为：

```text
如果 Hermes 页面在前台，轮询到 assistant reply 后直接追加到对话 UI。
```

后台行为：

```text
如果 Hermes 页面不在前台，轮询到新 assistant reply 后弹“回复已到达”气泡。
点击气泡跳转到 Hermes 页面。
```

## 10. MQTT 未来关系

V2.1 先用 HTTP polling 打通。

未来 MQTT 不承载完整消息正文，而是作为唤醒信号：

```text
server 写 conversation
server 发 MQTT：有新 reply
ESP32 收到 MQTT
ESP32 拉 conversation endpoint
ESP32 显示/气泡
```

固定原则：

```text
conversation endpoint 是数据真相源；
MQTT 只是未来的低功耗唤醒信号。
```

## 11. ESP32 owner 边界

ESP32 端仍只保留一个 Hermes owner：

```text
memory_watch_service
```

它负责：

- 上传语音。
- 接收 `accepted + asr_text`。
- 维护本地最近 5 轮显示缓存。
- 前台/后台轮询 conversation。
- 合并 server conversation snapshot。
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
```

## 12. watch_notify 的位置

`watch_notify` 不再是 V2.1 主线。

它可以作为后续 proactive inbox 能力保留，但不要混淆：

```text
conversation_reply -> Hermes 页面连续对话
proactive_inbox -> 收件箱通知
```

V2.1 优先级：

```text
1. voice-command accepted + asr_text
2. watch_conversation store
3. conversation endpoint
4. ESP32 前台/后台轮询
5. 后台 reply 气泡
6. Hermes 页面多轮对话显示
```

`watch_notify` 可后续再作为主动通知工具，不阻塞 V2.1。

## 13. 待确认问题

以下问题尚未完全敲定：

- conversation 已读语义：进入 Hermes 页面是否立即清空 `unread_reply_count`。
- 是否需要单独 `POST /v1/watch/conversation/read`。
- server 后台 Hermes 任务最长执行时间、失败重试和超时状态如何落库。
- ASR 失败时是否也写入 conversation error message。
- ESP32 最近 5 轮缓存的具体内存分配方式：PSRAM 固定数组、动态分配，或后续 flash/SD。
- 后台 60 秒轮询与现有 inbox 轮询是否合并调度，避免两个 HTTP worker 竞争。

## 14. 当前定稿口径

```text
V2.1 = 手表发起复杂 Hermes 任务后的异步对话回传。
server watch_conversation 是真相源，每 device 最近 20 条 messages。
ESP32 只缓存最近 5 轮用于显示，优先 PSRAM。
voice-command ASR 完成后先返回 accepted + asr_text。
Hermes 后台执行完成后写 assistant message。
Hermes 页面前台 2 秒轮询 conversation。
后台普通运行 60 秒轮询 conversation。
前台直接显示新 reply，后台弹“回复已到达”气泡。
MQTT 未来只做唤醒，conversation endpoint 仍是数据真相源。
V2.1/V3 都不做多设备、多入口，默认只服务 watch-001。
```
