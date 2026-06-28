---
id: attempt-hermes-v24-ws-intent-api
tags: context, runs, attempt, ai-memory-watch, hermes, v2.4, websocket, esp32s3
summary: 记录 AI Memory Watch / Hermes V2.4 阶段 4：memory_watch_ws_client 意图级 API 与业务事件映射。
last_reviewed: 2026-06-27
memory_type: run
scope: evidence
owners: docs/context/runs/2026-06-27-attempt-hermes-v24-ws-intent-api.md
triggers: memory_watch_ws_client, websocket turn api, ws event kind, thin client
evidence_level: observed
---

# Hermes V2.4 WS Intent API Attempt

## 背景

V2.4 阶段 4 目标是让 `memory_watch_service` 不再直接理解 WebSocket frame 细节。V2.2/V2.3 已验证的 transport 保留，只收窄 service 与 ws client 的边界。

## 改动

- `memory_watch_ws_client.h`
  - 新增 `memory_watch_ws_event_kind_t`。
  - `memory_watch_ws_event_t` 新增 `kind` 字段。
  - 新增 `memory_watch_ws_client_send_audio_turn()`。
- `memory_watch_ws_client.cc`
  - 新增 `MapEventKind()`，把原始 JSON frame 映射到业务事件：
    - ASR ready
    - assistant reply
    - error
  - 新增 turn-level 发送 API，内部负责 start/chunk/end。
- `memory_watch_service.c`
  - `memory_watch_service_ws_event_cb()` 改为判断业务 event kind。
  - `memory_watch_service_send_voice_over_ws()` 改为调用 `memory_watch_ws_client_send_audio_turn()`，不再手写 start/chunk/end 序列。

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

结果：通过。`111.bin` size `0xabeff0`，最小 app 分区剩余 `0x341010`（23%）。

## 后续观察点

- 仍需真机前台 WS smoke，确认业务 event kind 映射不影响 ASR 先显和 assistant reply 气泡。
- 阶段 5 可继续评估是否保留旧 `send_audio_start/chunk/end` 对外函数；当前保留以降低回退成本。
