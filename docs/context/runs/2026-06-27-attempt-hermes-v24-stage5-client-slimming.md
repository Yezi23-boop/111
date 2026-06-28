---
id: attempt-hermes-v24-stage5-client-slimming
tags: context, runs, ai-memory-watch, hermes, v2.4, esp32s3, thin-client, memory-watch-service, sync
summary: 记录 AI Memory Watch / Hermes V2.4 阶段 5 ESP32 client 瘦身：删除旧 conversation poll client、移除本地 pending 起始时间计时，并修正 done 空回复兜底。
last_reviewed: 2026-06-27
memory_type: attempt
scope: task
owners: docs/context/runs
triggers: AI Memory Watch V2.4, ESP32 Hermes thin client, memory_watch_voice_client, memory_watch_service, watch sync
evidence_level: verified
---

# AI Memory Watch / Hermes V2.4 Stage 5 Client Slimming

## 背景

V2.4 阶段 3 已把后台 pending / foreground reconcile 的 HTTP 主路径改为 `GET /v1/watch/sync`，但 ESP32 `memory_watch_voice_client` 仍保留旧 `GET /v1/watch/conversation` polling 公开接口、path builder、parser 和 response buffer 宏。`memory_watch_service` 也仍保留旧 10 分钟本地 pending timeout 留下的起始时间字段。

## 本轮修改

- 删除 `memory_watch_voice_client_conversation_poll()`、`memory_watch_conversation_poll_result_t`、`MEMORY_WATCH_CONVERSATION_RESPONSE_MAX_BYTES`。
- 删除旧 `/v1/watch/conversation?device_id=` path builder 与 `has_more` parser。
- 保留 `memory_watch_conversation_message_t`，作为 `/v1/watch/sync` conversation delta 的消息结构。
- 删除 `s_conversation_poll_started_ms`，ESP32 不再保留本地长任务开始时间来推断 Hermes 是否超时。
- 修正 `/sync session_state=done` 但本轮没有 assistant message 时的兜底：继续按 5 秒补拉，不伪造空 `conversation_reply`；只有 `error/timeout/canceled` 允许无正文收口为错误终态。

## 验证

```text
Memory Watch source tests: 40 passed
server tests: 140 passed
idf.py build: passed
111.bin: 0xabef80
app free: 0x341080 / 23%
```

相对阶段 4：

```text
111.bin: 0xabeff0 -> 0xabef80
app free: 0x341010 -> 0x341080
```

## 遗留

- 本轮没有新的 COM3 真机日志，因此 internal RAM、PSRAM、conversation worker stack、inbox worker stack 仍待阶段 6 串口验收记录。
- `memory_watch_service` 仍保留 conversation worker/queue 外壳和 polling 命名，这是低风险兼容选择；后续若要继续瘦身，可以另起小闭环重命名为 sync worker，但不应影响已验证的前台 WS 与离页 pending 体验。
