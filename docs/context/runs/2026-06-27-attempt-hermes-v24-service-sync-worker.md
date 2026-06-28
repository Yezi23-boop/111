---
id: attempt-hermes-v24-service-sync-worker
tags: context, runs, attempt, ai-memory-watch, hermes, v2.4, sync, esp32s3, freertos, memory-watch-service
summary: 记录 AI Memory Watch / Hermes V2.4 阶段 3：memory_watch_service 后台 pending 从 conversation polling 换到统一 /sync。
last_reviewed: 2026-06-27
memory_type: run
scope: evidence
owners: docs/context/runs/2026-06-27-attempt-hermes-v24-service-sync-worker.md
triggers: memory_watch_service, foreground_reconcile, pending sync, conversation worker, server session
evidence_level: observed
---

# Hermes V2.4 Service Sync Worker Attempt

## 背景

V2.4 阶段 2 已让 ESP32 具备 `memory_watch_voice_client_sync()`，但 `memory_watch_service` 仍通过旧 `GET /v1/watch/conversation` 轮询后台 pending，并保留本地 10 分钟长任务 timeout。阶段 3 的目标是让 ESP32 后台不再把 Hermes 长任务状态当真相源。

## 改动

- 保留 `memory_watch_service_conversation_worker_task`、queue 和 staging 外壳，降低对 UI/气泡路径的扰动。
- worker 内部 HTTP 调用从 `memory_watch_voice_client_conversation_poll()` 改为 `memory_watch_voice_client_sync()`。
- 离页 pending：
  - `mode=background`
  - 带 `pending_request_id`
  - 带 `after_message_id`
  - 固定 `max_messages=MEMORY_WATCH_SYNC_DEFAULT_MAX_MESSAGES`
- 进入 Hermes 页面：
  - 触发 `memory_watch_service_start_foreground_reconcile()`
  - 使用 `mode=foreground_reconcile`
  - 用最近 `last_seen_conversation_id` 做对账游标。
- 删除本地 `kConversationPendingMaxWaitMs` 和 `conversation_poll_timeout` 终态制造逻辑。
- server 返回公开 `session_state=done/error/timeout/canceled` 时，service 才收敛 pending；如果 `done` 但本轮没有 assistant message，service 不伪造回复内容，只收敛状态。
- 401/403 sync auth 失败时停止高频 pending sync，并进入配置错误路径，避免 5 秒循环打服务器。

## 验证

```powershell
.\server\watch_voice_endpoint\.venv\Scripts\python.exe -m pytest tests/test_memory_watch_service_source.py tests/test_memory_watch_ui_source.py tests/test_memory_watch_ws_client_source.py -q
```

结果：`40 passed`

```powershell
.\server\watch_voice_endpoint\.venv\Scripts\python.exe -m pytest server/watch_voice_endpoint/tests -q
```

结果：`140 passed`

```powershell
. 'D:\esp-idf\v5.5.3\esp-idf\export.ps1'; idf.py build
```

结果：通过。`111.bin` size `0xabefb0`，最小 app 分区剩余 `0x341050`（23%）。

## 后续观察点

- 需要 COM3 真机复测前台 WS、离页 pending 气泡、重新进入 Hermes 页面后对话补齐是否无回归。
- 阶段 4 再评估 `memory_watch_ws_client` 是否需要进一步意图级封装；本轮不重写 WS transport。
