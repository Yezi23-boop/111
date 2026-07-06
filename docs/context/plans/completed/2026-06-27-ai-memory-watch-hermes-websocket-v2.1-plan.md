---
id: ai-memory-watch-hermes-websocket-v2-1-plan
tags: context, plans, ai-memory-watch, hermes, websocket, conversation, inbox-polling, esp32s3, v2.1
summary: AI Memory Watch / Hermes V2.1 WebSocket-first 执行计划：活跃对话走单 WSS，收件箱保留 HTTP 低频轮询，server 负责 conversation 断线补发，ESP32 只维护 memory_watch_service 一个 owner。
last_reviewed: 2026-06-27
memory_type: task
scope: task
owners: docs/context/plans/completed/2026-06-27-ai-memory-watch-hermes-websocket-v2.1-plan.md
triggers: AI Memory Watch V2.1, Hermes WebSocket, watch ws, conversation reply, inbox polling
evidence_level: design
status: archived
---

# AI Memory Watch / Hermes WebSocket V2.1 执行计划

## 目标与全局

- 任务目标：把 AI Memory Watch / Hermes V2.1 从 HTTP conversation 轮询路线调整为 WebSocket-first 活跃对话链路。
- 为什么现在做：V1 已跑通真机语音到 Hermes；V2 已完成 server inbox + ESP32 收件箱 + 全局气泡。V2.1 要解决复杂 Hermes 任务的异步回传体验，用户希望参考小智 AI 的 WebSocket 对话模式，减少前台交互通讯复杂度。
- 完成后用户会看到什么变化：进入 Hermes 页面后，手表通过一条 WebSocket 完成语音上传、ASR 回显、任务状态和 Hermes 回复下发；如果离开页面但任务未完成，后台可继续等回复并弹气泡。收件箱仍独立低频 HTTP 轮询，不要求 WebSocket 常开。

## 范围与非目标

本轮明确要做：

- server 新增 `WSS /v1/watch/ws`，承载 auth、Ogg Opus binary 上传、ASR 回显、task 状态和 assistant reply。
- server 采用方案 A：保留现有 `ai-memory-watch-voice-endpoint` 容器和 `127.0.0.1:8787` 端口，在同一容器内新增 WS endpoint；旧 HTTP V1/V2 接口不删除。
- server 增加 feature flag，例如 `WATCH_WS_ENABLED=true/false`，用于在不破坏旧链路的前提下启停 WS 实验路径。
- server 新增或完善 `watch_conversation` store，作为断线补发真相源，每 device 保留最近 20 条 messages。
- server 重连时按 `last_seen_conversation_id` 补发缺失 conversation messages。
- ESP32 侧由 `memory_watch_service` 作为唯一 owner 管理 WebSocket 连接、重连、音频发送、JSON 事件接收和本地最近 5 轮显示缓存。
- Hermes 页面显示多轮 conversation，并在前台直接追加 `asr_result` 与 assistant reply。
- 用户离开 Hermes 页面但有 pending request 时，允许后台继续保持 WebSocket；没有 pending request 时可以关闭 WebSocket。
- 收件箱 `proactive_inbox` 保留独立 HTTP 低频轮询，不走 WebSocket 主链路。
- 参考 `components/official_chat` / 小智 AI 的 WebSocket 技术骨架：JSON 控制帧、binary 音频帧、send mutex、事件回调、连接状态等待。

本轮明确不做：

- 不把 Hermes 协议塞进 `components/official_chat`，不修改小智 AI 主线业务状态机。
- 不复用小智 AI 的 wake word、listening mode、MCP、server hello 音频参数、incoming audio 播放路径。
- 不做多设备、多入口；默认只服务 `watch-001`。
- 不做 TTS 语音下发。
- 不做手机通知聚合。
- 不把普通对话回复写入 inbox。
- 不把 conversation 历史长期保存在 ESP32 作为真相源。
- 不公开 Hermes API Server `8642` 或 Dashboard `9119`。

## 进度

- `[x]` 已敲定 V2.1 产品口径：活跃 Hermes 对话走 WebSocket，收件箱走 HTTP 低频轮询。
- `[x]` 已更新架构草案 `docs/context/knowledge/project/hermes-multi-agent-architecture.md` 和 `CHANGELOG.md`。
- `[x]` server：定义 WebSocket 协议常量、消息 schema 与错误码。
- `[x]` server：在现有 `ai-memory-watch-voice-endpoint` 容器内加 `WATCH_WS_ENABLED` feature flag，默认不删除旧 HTTP 入口。
- `[x]` server：新增 `watch_conversation` repository 与 retention 20 messages。
- `[x]` server：实现 `WSS /v1/watch/ws` auth、session manager、binary 音频接收与受控大小限制。
- `[x]` server：接入 ASR -> Hermes 后台任务 -> conversation message 落库 -> 在线 WS 推送。
- `[x]` server：实现 `last_seen_conversation_id` 重连补发与 ACK ok 响应。
- `[x]` server：补 pytest 覆盖 auth、binary upload、ASR/Hermes 成功、断线补发、错误帧和 token 不泄露。
- `[x]` server：扩展 smoke/release gate，支持脚本模拟 WebSocket 音频上传与回复接收。
- `[x]` ESP32：设计 `memory_watch_ws_client` 窄模块，参考 official_chat WebSocket 技术骨架但不复用业务协议。
- `[x]` ESP32：新增 official_chat WebSocket transport 窄适配层，`memory_watch_ws_client` 不直接 include `net/websocket_client.h` 私有头；当前仅复用传输能力，不引入 wake/listening/MCP/TTS 业务语义。
- `[x]` ESP32：将 Hermes 页面主语音上传 worker 从 HTTP voice-command 切到 WebSocket `audio_start` / binary Ogg Opus chunk / `audio_end`；HTTP voice-command 保留为 Kconfig 关闭 WS 时的 fallback。
- `[x]` ESP32：`memory_watch_service` 合并 ASR/Hermes reply 到本地最近 5 轮 conversation 显示缓存；server conversation 仍是真相源，ESP32 缓存只用于页面重建和气泡点击回看。
- `[x]` ESP32：前台 Hermes 页面显示多轮 user/assistant 对话流，controller 从 service 只读复制缓存，不再自己持有长期 conversation owner。
- `[x]` ESP32：后台 pending request 收到 reply 后弹“回复已到达”气泡，点击进入 Hermes 对话页。
- `[x]` ESP32：没有 pending request 且不在 Hermes 页面时关闭 WS；当前 WS 是 upload worker 局部连接，`conversation_message/error/disconnect/timeout` 收敛后立即 close，收件箱继续 HTTP polling。
- `[x]` 验证：server pytest、WebSocket smoke、source tests、`idf.py build`。
- `[x]` 验证：公网 `wss://watch.934000.xyz/v1/watch/ws` 不暴露 Hermes 私有路径。
- `[x]` 验证：真机按住说话至少成功一次，ASR 回显和 Hermes reply 均通过 WS 到达。

## 决策记录

- 日期：2026-06-27
- 决策：V2.1 使用 WebSocket-first 作为活跃 Hermes 对话主链路。
- 原因：用户希望减少前台通讯心智负担；小智 AI 已在当前仓库提供可参考的 ESP32-S3 WebSocket 对话技术骨架。

- 日期：2026-06-27
- 决策：收件箱不走 WebSocket，保留独立 HTTP 低频轮询。
- 原因：收件箱是主动提醒/消息箱，不是活跃对话任务；没有 pending request 时 WebSocket 可以关闭以节省资源。

- 日期：2026-06-27
- 决策：server conversation store 仍是断线补发真相源。
- 原因：WebSocket 重连只保证重新连接，不自动补回断线期间错过的 Hermes 回复；需要 `last_seen_conversation_id` 和 server store 保证不丢消息。

- 日期：2026-06-27
- 决策：参考小智 AI 的 WebSocket 技术骨架，但不复用 `official_chat` 业务主线。
- 原因：`official_chat` 是实时语音聊天，包含 wake/listening/MCP/TTS 播放等语义；AI Memory Watch 是 Hermes 任务终端，owner 应保持在 `memory_watch_service`。

- 日期：2026-06-27
- 决策：采用方案 A，保留旧 server 侧容器，在同一 `ai-memory-watch-voice-endpoint` 容器内新增 WebSocket endpoint。
- 原因：旧 HTTP `/v1/watch/voice-command`、inbox 和公网 smoke 已验证通过，是 V2.1 开发期间的回退安全垫；同容器新增 WS 可复用现有 env、ASR、Hermes adapter、device token 和 Cloudflare `/v1/watch/*` 路由，同时用 feature flag 控制风险。

- 日期：2026-06-27
- 决策：ESP32 首轮先用 `official_chat_websocket_transport` 窄适配暴露底层 WebSocket transport，`memory_watch_ws_client` 只依赖该传输接口。
- 原因：当前 ESP-IDF 组件树没有可直接复用的官方 `esp_websocket_client`，复制一套 transport 会扩大改动；窄适配可先形成可编译小闭环。该适配不是把 `official_chat` 定义为通用网络 owner，Hermes 协议仍必须留在 `memory_watch_ws_client` / `memory_watch_service`。

## 意外与发现

- 当前 V2.1 架构草案此前已写成 HTTP conversation endpoint 前台/后台轮询；现在已被 WebSocket-first 路线 supersede。
- 小智 AI 的 `WebsocketClient` 已具备 client mask、binary/text frame、send mutex 和回调结构，可作为实现参考。
- `official_chat` 曾有 MQTT 停机触发 lwIP mutex 断言的历史经验；V2.1 后续实现 WebSocket 关闭/重连时要重点验证 shutdown 顺序。
- `memory_watch_service` 已经承担 inbox cache 与气泡触发，后续不能再加 `conversation_service` / `inbox_network_service` 之类长期 owner。
- `memory_watch_service` 旧的 `static volatile bool s_inbox_poll_pending` 已改为 `portMUX_TYPE s_inbox_poll_lock` 保护的 getter/setter，source test 不再允许裸 `static volatile` 作为跨任务 pending 标志。
- `memory_watch_service_upload_worker_task` 已在 `CONFIG_MEMORY_WATCH_WEBSOCKET_ENABLED` 下走 WS 同步等待：录音仍由 worker owner 执行，WS 用静态 EventGroup 等 `conversation_message` / `error` / disconnect / timeout，收到 assistant reply 后填回现有 V1 response 字段；这只是主传输切换，不等于多轮 UI 或后台气泡完成。
- 本地最近 5 轮 conversation 缓存已从 `memory_watch_controller` 收回到 `memory_watch_service`：service 持有最多 10 条 user/Hermes 短消息和 `conversation_generation`，controller 只按 generation 复制到 view model；这样页面销毁重建或气泡点击回到 Hermes 页时仍能显示最近对话。
- Hermes 页面返回不再取消 `UPLOADING/THINKING` 中的 pending request；离页只销毁 UI，worker 继续等 Hermes 回复，完成后全局气泡提示。页面内“取消”等待按钮仍保留显式取消语义。
- 用户实机日志显示 boot health 已成功但 inbox polling 拼出 `https://#/v1/watch/inbox?...` 和乱码 `device_id`，原因是 `inbox_job_t` 经 FreeRTOS queue 按值复制后，内部 `base_url/device_id/device_token` 指针仍指向投递方临时 job；worker 收到副本后必须 rebind 到副本自己的数组。

## 验证与验收

计划运行的验证命令：

```powershell
uv run python -m pytest server/watch_voice_endpoint/tests
uv run python scripts/context/validate_context.py --level standard --q "AI Memory Watch Hermes WebSocket V2.1" --brief
git diff --check
idf.py build
```

计划新增或扩展的脚本验收：

```powershell
.\server\watch_voice_endpoint\smoke_test.ps1 -BaseUrl "https://watch.934000.xyz" -SkipServiceHealth
.\server\watch_voice_endpoint\runtime_status.ps1 -BaseUrl "https://watch.934000.xyz" -SkipDocker -SkipHermesApi -SkipServiceHealth -AssertPrivateNotExposed
```

待补 WebSocket smoke：

```powershell
.\server\watch_voice_endpoint\websocket_smoke_test.ps1 -BaseUrl "wss://watch.934000.xyz/v1/watch/ws"
```

期望看到的结果：

- server WebSocket smoke 能模拟 auth -> audio_start -> binary Ogg Opus -> audio_end -> asr_result -> task_started -> conversation_message。
- 断开后重连携带 `last_seen_conversation_id`，server 能补发缺失 assistant reply。
- 收件箱 HTTP polling 与 WebSocket 对话互不阻塞。
- 公网只开放 `/v1/watch/*`，`/health`、`/v1/models`、`/v1/responses` 不公开。
- ESP32 真机 Hermes 页面能通过 WSS 完成至少一次按住说话闭环。

当前实际结果：

- V1 HTTP voice-command 与 V2 inbox 已完成。
- server 侧 V2.1 WebSocket 第一阶段已实现：`WATCH_WS_ENABLED`、`/v1/watch/ws`、`conversation_repo.py`、WebSocket pytest、`websocket_smoke_test.ps1` 和 release gate 可选 `-IncludeWebSocketSmoke`。
- server pytest 当前 `88 passed`；默认 `release_gate.ps1 -SkipAcceptance` 通过，且旧 HTTP 路由默认未被删除。
- 架构文档已切到 WebSocket-first + inbox HTTP polling 口径。
- ESP32 侧 `memory_watch_ws_client` 当前已编入 `main`，支持 watch endpoint URL 转 `wss/ws`、auth、`audio_start`、binary audio chunk、`audio_end`、`ack` 和 JSON 事件分发；`memory_watch_service` 主语音 upload worker 已接入该 WS 路径。
- 当前验证：`tests/test_memory_watch_service_source.py tests/test_memory_watch_ui_source.py tests/test_memory_watch_ws_client_source.py` 为 `37 passed`；`git diff --check` 通过（仅 Windows LF/CRLF 提示）；`idf.py build` 通过，`111.bin` 大小 `0xabdd20`，最小 app 分区剩余 `0x3422e0`。
- 公网 runtime gate 通过：`https://watch.934000.xyz/v1/watch/health` 返回 `ok/hermes_status=online`，`/health`、`/v1/models`、`/v1/responses` 均为 404，未公开 Hermes 私有路径。
- 公网 WebSocket smoke 通过：`wss://watch.934000.xyz/v1/watch/ws` 完成 auth -> `audio_start` -> binary Ogg Opus -> `audio_end` -> `asr_result` -> `task_started` -> `conversation_message`，返回 `reply_status=done`。
- 真机已刷入最新 app 并启动：COM3 `app-flash-monitor` 生成 `board_logs/2026-06-27-12-09-40-hermes-ws-v21-service-cache-boot.log`，看到 `network_service_ready`、`memory_watch_ready`、`startup_sequence_done`，无 Guru/panic/stack overflow；但 Wi-Fi STA 连接 `li` 连续失败并回到 `OFFLINE`，因此尚不能完成真机 WSS 按住说话闭环。
- 已修复 inbox queue-copy 指针失效：`memory_watch_service_inbox_get_client_config()` 填充快照后 rebind，`memory_watch_service_inbox_worker_task()` 每次 `xQueueReceive` 后对 job 副本 rebind；source test 已锁定该行为。下一步需重刷并确认不再出现 `https://#` 或乱码 `device_id`。
- 2026-06-27 12:56 真机重刷监控通过：`board_logs/2026-06-27-12-56-31-hermes-ws-v21-inbox-rebind.log` 显示 `watch endpoint health result: hermes_online=1 err=ESP_OK`、`inbox: poll ok items=0 unread=0`，未再出现 `https://#`、乱码 `device_id`、Guru/panic/stack overflow。
- 同一轮真机日志完成一次 WSS 语音闭环：`mw_upload stack: stage=voice-ws-done`，`watch request result ... status=done action=conversation_reply error_code=none asr_chars=18 reply_chars=9`。因此当前 V2.1 定义的 server WSS + ESP32 主语音路径 + inbox HTTP polling 共存验收已完成。
- 完成后 UX 时序修正：server 原本已先发 `asr_result`，但 ESP32 之前等最终 worker result 才把用户/assistant 一起 append 到本地对话。现改为 `asr_result` 到达即追加 Hermes 页面用户侧对话消息；assistant `conversation_message` 到达后再追加 Hermes 侧消息，并用同一 `request_id + role + text` 去重防止用户消息重复。

## 幂等与恢复

- 如果中途中断，下次从 server WebSocket 协议和 `watch_conversation` repository 继续，先让 PC 脚本模拟 WS 闭环，不先改 ESP32 真机。
- 如果 WebSocket server 实现失败，保留 V1 HTTP voice-command，不破坏现有真机语音链路。
- 如果 WS feature flag 关闭，旧 `ai-memory-watch-voice-endpoint` 容器仍应继续通过 V1/V2 HTTP smoke。
- 如果 ESP32 WebSocket 切换失败，Hermes 页面可临时回退到 V1 HTTP voice-command 路径验证 ASR/Hermes 是否仍可用。
- 所有 key/token 只放仓库外 env、NVS 或 sdkconfig 开发配置；文档、日志、测试输出不得打印真实值。

## 下一步

- 下一步最小动作：按用户新需求进入 V2.2/V3 迭代，不再把 V2.1 主链路作为未完成项重复排查。
- 后续产品动作：可继续优化后台 detach/reconnect 体验、页面对话展示细节、低功耗预算和未来 MQTT 唤醒信号。
