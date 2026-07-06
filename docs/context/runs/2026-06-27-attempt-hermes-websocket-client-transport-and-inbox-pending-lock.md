---
id: attempt-hermes-websocket-client-transport-and-inbox-pending-lock
tags: context, runs, attempt-log, hermes, ai-memory-watch, websocket, freertos, psram, official-chat-transport
summary: AI Memory Watch / Hermes V2.1 ESP32 WebSocket 客户端编译闭环，并修复 inbox pending 裸 volatile 同步边界。
last_reviewed: 2026-06-27
memory_type: episodic
scope: task
status: active
result: success
owners: docs/context/runs
triggers: attempt-log, hermes websocket client, memory_watch_ws_client, static volatile, inbox pending, idf build
evidence_level: observed
record_reasons: route-choice, error-signature, evidence
---

# Attempt Log: Hermes WebSocket Client Transport 与 Inbox Queue 同步修复

## 背景

- 本次要验证什么：
  1. ESP32 侧 `memory_watch_ws_client` 能否复用当前仓库已有小智 AI WebSocket transport 技术骨架，并通过 `idf.py build`。
  2. 是否能避免 `main` 直接 include `components/official_chat/net/websocket_client.h` 私有头。
  3. 修复 source test 暴露出的 `memory_watch_service` 跨任务 pending 标志仍使用裸 `static volatile` 的同步边界问题。
- 对应计划：`docs/context/plans/completed/2026-06-27-ai-memory-watch-hermes-websocket-v2.1-plan.md`

## 操作

- 修改过的文件或 owner：
  - `components/official_chat/include/official_chat_websocket_transport.h` / `official_chat_websocket_transport.cc`：新增窄 WebSocket transport 适配，只包装连接、发送、回调和关闭，不包含 wake/listening/MCP/TTS 业务语义。
  - `main/services/memory_watch_ws_client.h` / `.cc`：实现 Hermes WS 客户端窄模块，负责 URL 转换、auth、`audio_start`、binary audio、`audio_end`、ACK 和入站 JSON 事件分发。
  - `main/services/memory_watch_service.c`：将 `s_inbox_poll_pending` 从裸 `static volatile bool` 改为 `portMUX_TYPE s_inbox_poll_lock` 保护的 getter/setter。
  - `main/services/memory_watch_service.c`：修复 inbox queue-copy 后的 client config 指针失效；`inbox_job_t` 通过 FreeRTOS queue 按值复制后，worker 必须把 `client_config.base_url/device_id/device_token` 重新指向副本内的数组。
  - `main/services/memory_watch_service.c`：在 `CONFIG_MEMORY_WATCH_WEBSOCKET_ENABLED` 下将 voice upload worker 接到 `memory_watch_ws_client`，发送 `audio_start` / binary Ogg Opus chunk / `audio_end`，并用静态 EventGroup 等待 `conversation_message`、`error`、disconnect 或 timeout。
  - `main/services/memory_watch_service.h` / `.c`：新增最近 5 轮 conversation 显示缓存 API，service 保存最多 10 条 user/Hermes 短消息和 `conversation_generation`。
  - `main/services/memory_watch_service.c`：补齐 ASR 先显时序，`asr_result` 到达即追加 `MEMORY_WATCH_SERVICE_CONVERSATION_USER`；conversation append 对同一 `request_id + role + text` 去重，避免最终 worker result 再追加一次用户消息。
  - `main/ui/custom/memory_watch_controller.c`：不再自持 conversation 历史和去重状态，改为按 generation 从 service 复制缓存给 view model。
  - `main/ui/custom/memory_watch_controller.c`：返回离开 Hermes 页面时不再取消 `UPLOADING/THINKING` pending request，保留 worker 后台等待和全局 reply 气泡；页面内取消按钮仍显式调用 `memory_watch_service_cancel_waiting()`。
  - `tests/test_memory_watch_ws_client_source.py`、`tests/test_memory_watch_service_source.py`：锁定 WS 客户端边界与当前 PSRAM task stack 策略。
- 已尝试并确认：
  - 直接 include `net/websocket_client.h` 会依赖 `official_chat` 私有 include 目录，长期边界不清。
  - 本机 ESP-IDF 5.5.3 组件树没有可直接复用的 `esp_websocket_client` 组件。

## 路线取舍

- 当前采用：用 `official_chat_websocket_transport` 窄适配复用现有 WebSocket transport。
- 采用原因：避免复制一套 transport 或引入新依赖，先完成 V2.1 ESP32 client 可编译小闭环。
- 边界说明：该适配不是把 `official_chat` 定义为通用网络 owner；Hermes 协议仍必须停留在 `memory_watch_ws_client` 和后续 `memory_watch_service`。
- 后续若要长期收敛，可把 transport 抽到中性组件或改用可管理的官方 WebSocket 组件，但不应把 Hermes JSON 协议移入 `components/official_chat`。

## 观测

- 关键验证：
  - `uv run python -m pytest tests/test_memory_watch_service_source.py tests/test_memory_watch_ui_source.py tests/test_memory_watch_ws_client_source.py -q`：`37 passed`。
  - `git diff --check`：通过，仅有 Windows LF/CRLF 提示。
  - `. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`：通过。
  - `.\server\watch_voice_endpoint\runtime_status.ps1 -BaseUrl "https://watch.934000.xyz" -SkipDocker -SkipHermesApi -SkipServiceHealth -AssertPrivateNotExposed`：通过，watch health `ok` / Hermes `online`，私有路径 `/health`、`/v1/models`、`/v1/responses` 均 404。
  - `.\server\watch_voice_endpoint\websocket_smoke_test.ps1 -BaseUrl "wss://watch.934000.xyz/v1/watch/ws"`：通过，收到 `asr_result`、`task_started`、`conversation_message`，`reply_status=done`。
  - `.\scripts\board\agent_serial_monitor.ps1 -Port COM3 -Action app-flash-monitor ... -Tag hermes-ws-v21-service-cache-boot`：刷入成功，`board_logs/2026-06-27-12-09-40-hermes-ws-v21-service-cache-boot.log` 记录 `network_service_ready`、`memory_watch_ready`、`startup_sequence_done`，无 Guru/panic/stack overflow。
  - 用户后续实机日志：`watch endpoint health result: hermes_online=1 err=ESP_OK` 后，inbox poll 拼出 `https://#/v1/watch/inbox?device_id=...`，`device_id` 也出现百分号乱码；这证明 endpoint 本身可用，错误位于 ESP32 侧 inbox job 的 URL/device_id 生命周期。
  - `.\scripts\board\agent_serial_monitor.ps1 -Port COM3 -Action app-flash-monitor -DurationSeconds 45 ... -Tag hermes-ws-v21-inbox-rebind`：重刷后 `board_logs/2026-06-27-12-56-31-hermes-ws-v21-inbox-rebind.log` 记录 `inbox: poll ok items=0 unread=0`，未再出现 URL parse 或乱码 `device_id`。
  - 同一轮板端日志完成真机 WSS 语音闭环：`mw_upload stack: stage=voice-ws-done`，`watch request result: ... status=done action=conversation_reply error_code=none asr_chars=18 reply_chars=9`。
  - 用户指出页面内“问和答同时出现”不符合需求后，复查代码确认 server 已先发 `asr_result`，问题在 ESP32 侧只暂存 ASR，未立即 append 到 conversation cache。修复后 source tests `37 passed`，`idf.py build` 通过，`111.bin binary size 0xabdd80 bytes`，最小 app 分区剩余 `0x342280 bytes (23%)`。
  - `.\scripts\board\agent_serial_monitor.ps1 -Port COM3 -Action app-flash-monitor -DurationSeconds 40 ... -Tag hermes-asr-first-user-conversation`：生成 `board_logs/2026-06-27-13-58-24-hermes-asr-first-user-conversation.log`，启动、health 与 inbox 正常，WSS 语音闭环再次成功，`watch request result ... asr_chars=18 reply_chars=9`，无 Guru/panic/stack overflow。
- 构建证据：
  - 生成 `build/111.bin`。
  - `111.bin binary size 0xabdd20 bytes`。
  - 最小 app 分区剩余 `0x3422e0 bytes (23%)`。
- 修复过的错误签名：
  - `implicit declaration of function 'memory_watch_service_inbox_is_poll_pending'`：新增前向声明后消失。
  - source test 中 `static volatile` 禁用断言：通过 `portMUX_TYPE` 临界区保护后消失。
  - `HTTP_CLIENT: Error parse url https://#/v1/watch/inbox?...` 与乱码 `device_id`：`memory_watch_service_inbox_get_client_config()` 填充字符串快照后调用 `memory_watch_service_rebind_client_config(out)`，`memory_watch_service_inbox_worker_task()` 在 `xQueueReceive` 后调用 `memory_watch_service_rebind_client_config(&job.client_config)`，避免 queue 按值复制后继续使用投递方临时 job 的悬空指针。

## 结论

- 可以确认：
  1. `memory_watch_ws_client` 已能编入 `main` 并通过 ESP-IDF build。
  2. `memory_watch_service` inbox pending 标志不再依赖裸 `static volatile` 做跨任务同步。
  3. 当前旧 HTTP voice-command/inbox 路径未删除，仍可作为 V2.1 回退链路。
  4. `memory_watch_service_upload_worker_task` 已在 `MEMORY_WATCH_WEBSOCKET_ENABLED=y` 时把主语音上传切到 WS，并复用现有 `WORKER_DONE` / V1 response 收敛。
  5. 最近 5 轮 conversation 显示缓存已由 `memory_watch_service` 持有；controller 只做只读复制和 role 映射，符合唯一 owner 边界。
  6. 离开 Hermes 页面不再取消已上传/思考中的 request；WS 在 worker 等待结果收敛后关闭，收件箱 HTTP polling 不受 WS 生命周期影响。
  7. inbox poll 的 queue-copy 指针失效已按 owner 边界修复，并由 source tests 与 COM3 真机重刷日志锁定；重刷后 `https://#` 不再出现。
  8. 当前 V2.1 真机 WSS 主链路已通过一次真实语音闭环，ASR 与 Hermes reply 均返回有效字符数。
  9. Hermes 页面内 ASR 显示时序已修正为“ASR 到达先显示用户侧对话消息，assistant reply 到达后再显示 Hermes 侧消息”；全局气泡仍只用于离页后的 assistant reply。
- 尚未完成：
  1. 后续产品化仍可优化后台 detach/reconnect 体验、页面对话展示细节、低功耗预算和未来 MQTT 唤醒信号；这些不再阻塞当前 V2.1 验收。
  2. standard context validate 仍需在收尾命令跑完后补最新结果。

## 下一步

- 下一步最小动作：运行 standard context validate，然后按用户新需求进入下一轮 V2.2/V3 迭代。
- 保留 HTTP fallback，直到后续版本确认不再需要；当前 V2.1 WSS 真机主链路已有一次成功证据。
