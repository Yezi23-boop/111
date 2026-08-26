---
id: attempt-hermes-v24-sync-client-psram-buffer
tags: context, runs, attempt, ai-memory-watch, hermes, v2.4, sync, esp32s3, psram
summary: 记录 AI Memory Watch / Hermes V2.4 阶段 2：ESP32 /sync 窄客户端与 PSRAM response buffer 验证。
last_reviewed: 2026-06-27
memory_type: run
scope: evidence
owners: docs/context/runs/2026-06-27-attempt-hermes-v24-sync-client-psram-buffer.md
triggers: memory_watch_voice_client_sync, watch sync, PSRAM response buffer, V2.4 thin client
evidence_level: observed
---

# Hermes V2.4 Sync Client PSRAM Buffer Attempt

## 背景

V2.4 的目标是把 ESP32-S3 手表继续做薄：前台 Hermes 页面仍用 WebSocket，后台 pending、页面重入对账和 inbox 摘要收敛到统一 `GET /v1/watch/sync`。阶段 1.5 已完成 server `/sync` endpoint 与契约测试，阶段 2 需要让 ESP32 侧具备窄 client 能力，但还不改 `memory_watch_service` 行为。

## 改动

- `main/services/memory_watch_voice_client.h`
  - 新增 `memory_watch_sync_mode_t`。
  - 新增 `memory_watch_voice_client_sync_cursor_t`。
  - 新增 `memory_watch_voice_client_sync_result_t`。
  - 新增 `memory_watch_sync_inbox_summary_t`，避免与 service 层 `memory_watch_inbox_summary_t` 命名冲突。
  - 新增 `memory_watch_voice_client_sync()`。
- `main/services/memory_watch_voice_client.c`
  - 新增 `/v1/watch/sync` URL 构建。
  - 解析 `schema_version`、公开 `session_state`、conversation messages、`inbox.unread_count`、`latest_unread.notification_id/title/preview/created_at`。
  - response buffer 继续使用 `memory_watch_voice_client_alloc()`，优先 PSRAM，避免新增 task 栈大对象。
- `tests/test_memory_watch_service_source.py`
  - 增加 V2.4 sync client source test，锁住 endpoint 字段、PSRAM 分配路径和“不引入 poll_after/server 内部状态名”的源码边界。

## 发现与修复

- 第一次 source test 失败：注释里写入了 `poll_after_ms`，与“不让 ESP32 持有这个协议概念”的目标冲突。已改为泛称“轮询节奏由 service owner 持有”。
- 第二次 source test 失败：注释里写入了 server 内部 session state 名。已改为“server 内部细分状态”。
- 第一次 `idf.py build` 失败：`memory_watch_voice_client.h` 新增的 `memory_watch_inbox_summary_t` 与 `memory_watch_service.h` 既有同名类型冲突。已改名为 `memory_watch_sync_inbox_summary_t`。

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

结果：通过。`111.bin` size `0xabecc0`，最小 app 分区剩余 `0x341340`（23%）。

## 下一步

- 阶段 3 将 `memory_watch_service` 后台 pending / foreground reconcile / inbox 摘要路径逐步接到 `memory_watch_voice_client_sync()`。
- 不要在阶段 3 一次性大删旧 conversation/inbox worker；先保持体验可回退，再逐步削掉重复本地职责。
