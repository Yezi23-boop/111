---
id: attempt-hermes-v22-background-conversation-polling
tags: context, runs, attempt-log, hermes, ai-memory-watch, websocket, conversation, polling, freertos, psram
summary: AI Memory Watch / Hermes V2.2 前台 WebSocket + 离页后台 conversation polling 闭环实现与验证记录。
last_reviewed: 2026-06-27
memory_type: episodic
scope: task
status: completed
result: success
owners: docs/context/runs
triggers: attempt-log, hermes v2.2, foreground websocket, background conversation polling, last_seen_conversation_id
evidence_level: observed
record_reasons: route-choice, evidence, owner-architecture
---

# Attempt Log: Hermes V2.2 前台 WS + 后台 Conversation Polling

## 背景

- 目标：执行 active plan `2026-06-27-ai-memory-watch-hermes-v2.2-foreground-ws-background-conversation-polling-plan.md`。
- 旧 V2.1 行为：ESP32 upload worker 发完 `audio_end` 后继续保持 WS 等 assistant reply。
- 新 V2.2 行为：前台 Hermes 页面仍用 WS 实时；离开 Hermes 页面后关闭 WS，pending request 改用 HTTP `GET /v1/watch/conversation` 每 5 秒取回回复。

## 改动

- `server/watch_voice_endpoint/app.py`
  - 新增 `GET /v1/watch/conversation`，复用 device token 鉴权，返回 `messages + has_more=false`。
  - 将 WS `audio_end` 后的 ASR/Hermes 处理改为后台 task；WS 仍在线时 best-effort 推 `asr_result/task_started/conversation_message`，WS 断开不影响 conversation 落库。
- `server/watch_voice_endpoint/conversation_polling_smoke_test.ps1`
  - 新增公网/本机脚本：WS 上传音频后主动断开，再 HTTP polling conversation 拉取 assistant reply。
- `main/services/memory_watch_voice_client.[ch]`
  - 新增 conversation polling 窄接口、20 条消息上限、12 KiB 响应上限，响应缓冲从 PSRAM 分配。
- `main/services/memory_watch_service.[ch]`
  - 新增 `memory_watch_service_set_foreground()` 页面生命周期入口。
  - 新增 `mw_conv` conversation worker task、深度 1 queue、PSRAM staging。
  - 新增 `last_seen_conversation_id`、离页 pending request、5 秒 poll interval、4 秒单次 timeout、10 分钟总等待。
  - upload worker 在 `audio_end` 后如果发现 Hermes 页面离开前台，会关闭 WS 并返回 `conversation_pending`；owner 保持 request active 和 `THINKING`，启动后台 conversation polling。
- `main/ui/custom/memory_watch_controller.c`
  - 进入/离开 Hermes 页面只向 service 发布 foreground 状态，不直接控制 WS 或 HTTP。
  - 后台气泡收紧为 done/error/timeout/canceled 终态触发，不把 clarification 当普通回复气泡。

## 验证

- 用户真机反馈：
  - 前台 Hermes 页面已经能正常使用。
  - 离开 Hermes 页面后也能通过全局气泡收到 Hermes 回复。
  - 暴露问题：同一回复会显示两次相同信息。
  - 修复：后台 conversation polling 拉到 terminal assistant 时，该消息已经通过 server conversation 合并进本地显示缓存；worker done 收尾只更新状态/回复文本，不再二次 append 到 conversation cache。
  - 修复后用户再次真机实测：当前没有问题，前台/离页回复不再重复显示。
- `uv run --with-requirements server/watch_voice_endpoint/requirements.txt python -m pytest server/watch_voice_endpoint/tests -q`
  - 结果：`91 passed`。
- `uv run python -m pytest tests/test_memory_watch_service_source.py tests/test_memory_watch_ui_source.py tests/test_memory_watch_ws_client_source.py -q`
  - 结果：`39 passed`。
- `. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`
  - 结果：通过；`111.bin` 大小 `0xabec30`，最小 app 分区剩余 `0x3413d0` bytes（23%）。
- `. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py -p COM3 app-flash`
  - 结果：通过，仅刷 app 分区。
- `.\scripts\board\agent_serial_monitor.ps1 -Port COM3 -Action monitor -DurationSeconds 30 -Tag hermes-v22-duplicate-reply-fix-boot -Pattern 'memory_watch|conversation|inbox|voice-ws|websocket|watch request result|reply arrived|panic|Guru|stack overflow|HTTP_CLIENT|Error parse url|network_service_ready|memory_watch_ready'`
  - 结果：通过；日志 `board_logs/2026-06-27-16-22-39-hermes-v22-duplicate-reply-fix-boot.log` 可见 `network_service_ready`、`memory_watch_ready`、Hermes health `hermes_online=1`、inbox poll ok，未见 Guru/panic/stack overflow/URL 乱码。
- `docker compose -f .\server\watch_voice_endpoint\compose.local.yml up -d --build --force-recreate`
  - 结果：`ai-memory-watch-voice-endpoint` 重建并 healthy。
- `.\server\watch_voice_endpoint\runtime_status.ps1 -BaseUrl "https://watch.934000.xyz" -SkipDocker -SkipHermesApi -SkipServiceHealth -AssertPrivateNotExposed`
  - 结果：watch health `ok/online`；公网 `/health`、`/v1/models`、`/v1/responses` 均未公开。
- `.\server\watch_voice_endpoint\websocket_smoke_test.ps1 -BaseUrl "wss://watch.934000.xyz/v1/watch/ws"`
  - 结果：通过，收到 `asr_result/task_started/conversation_message`，assistant `status=done`。
- `.\server\watch_voice_endpoint\conversation_polling_smoke_test.ps1 -BaseUrl "https://watch.934000.xyz"`
  - 结果：通过；WS 发完音频后断开，HTTP conversation polling 拉到 user message 与 assistant `done` reply。

## 结论

- server 侧已经满足“WS 断开不丢任务，conversation store 是真相源”。
- 公网脚本已经证明“WS detach 后通过 HTTP conversation polling 取回 reply”。
- ESP32 侧已实现 V2.2 需要的 FreeRTOS owner/worker/polling 骨架并通过 build。
- 重复回复 hardening 已通过用户真机复测，V2.2 当前定义可作为完成闭环。

## 尚未完成

- 空闲重进 Hermes 页面不再作为 V2.2 必须新建 WS 的目标；当前实现为先显示本地 conversation cache，下一次语音 WS auth 携带 `last_seen_conversation_id`。

## 下一步

- 后续若继续优化 V2.2，优先做体验 hardening、异常文案和低功耗轮询参数；不要回退到离页保持 WS。
