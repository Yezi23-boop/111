---
id: ai-memory-watch-hermes-v2-5-conversation-reliability-plan
tags: context, plans, ai-memory-watch, hermes, v2.5, conversation, websocket, sync, long-running-job, idempotency, cancellation, sqlite, utf8, inbox-auth, observability
summary: AI Memory Watch / Hermes V2.5 对话可靠性修复计划：修复 120 秒长任务上限、request_id 幂等竞态、WebSocket 假取消、重启恢复、中文 UTF-8 截断、Inbox 写入口鉴权与主链路观测缺口。
last_reviewed: 2026-07-14
memory_type: task
scope: task
owners: docs/context/plans/active/2026-07-14-ai-memory-watch-hermes-v2.5-conversation-reliability-plan.md, server/watch_voice_endpoint/app.py, server/watch_voice_endpoint/session_repo.py, server/watch_voice_endpoint/conversation_repo.py, server/watch_voice_endpoint/tests, main/services/memory_watch/memory_watch_service.c, main/services/memory_watch/memory_watch_voice_client.c, main/services/memory_watch/memory_watch_ws_client.cc
triggers: Hermes conversation reliability, Hermes 长任务, watch request 幂等, WebSocket cancel, watch session recovery, UTF-8 truncation, internal inbox auth, watch job worker
evidence_level: design
status: active
---

# AI Memory Watch / Hermes V2.5 Conversation Reliability 修复计划

## 目标与全局

- 任务目标：把当前“短任务可用”的 Hermes 对话链路修到可以可靠承载复杂后台任务，同时保持现有前台 WebSocket、离页 `/sync`、对话气泡和主动 Inbox 体验不回退。
- 为什么现在做：当前线上 watch endpoint 与仓库源码一致，但 Hermes 调用仍受 120 秒同步 HTTP timeout 限制；WS request 的取消、并发幂等、重启恢复和 conversation 淘汰后的 replay 存在真实正确性缺口。
- 完成后用户会看到：
  - ASR 到达后仍立即显示用户消息。
  - 用户可留在 Hermes 页面等待，也可离页让任务继续。
  - 超过 120 秒的任务不再被手表或 watch endpoint 误判失败。
  - 相同 request 重试不会重复创建提醒或重复执行工具。
  - 取消后不会再收到该 request 的普通成功回复。
  - 中文回复不会出现半个汉字、乱码或无声截断。
  - Hermes 主动 Inbox 写入不再复用可被设备持有的 device token。

一句话边界：

```text
watch endpoint 是任务状态、对话结果和设备通知的 server owner；
ESP32 是输入、短缓存、状态展示和补拉终端；
Hermes 是推理、记忆与工具执行 owner。
```

## 当前基线与审查证据

当前正式链路：

```text
前台 active turn：
ESP32 --WSS /v1/watch/ws--> watch endpoint
  -> Ogg Opus -> MiMo ASR
  -> Hermes /v1/responses
  -> watch_conversation + watch_session
  -> WS conversation_message

离页 pending：
ESP32 --HTTPS GET /v1/watch/sync every 5s--> session + conversation delta + inbox summary

无 pending：
ESP32 只保留 Inbox 低频轮询
```

已确认基线：

- 当前分支：`codex/ai-memory-watch-hermes-api`。
- 阿里云 watch endpoint 容器中的 `app.py` 与仓库文件 SHA-256 一致。
- 阿里云实际配置：`HERMES_TIMEOUT_SECONDS=120`、`WATCH_REQUEST_TIMEOUT_SECONDS=115`、`WATCH_WS_ENABLED=true`。
- 当前测试：server `144 passed`；ESP32 Hermes 相关 source tests `54 passed`。
- 当前测试未覆盖：并发相同 request、WS running cancel、超过 120 秒、conversation 淘汰后终态 replay、进程中断恢复。
- 当前云端资源观测：Hermes 约 417 MiB、watch endpoint 约 61 MiB、cloudflared 约 15 MiB；第一版不需要 Redis/Celery。
- 当前公网边界：`/health`、`/v1/models`、`/v1/responses` 为 404；`/v1/watch/inbox` 公网可达但需要 device token。

## 成功标准

硬性完成线：

- 一个 `(device_id, request_id)` 在并发、断线和重试下最多只提交一次 Hermes 执行。
- terminal session 即使 conversation message 已被 20 条保留策略淘汰，也只能 replay，不能重新执行 Hermes。
- 用户取消 WS request 时，server session 进入 `canceled`，本地 worker 停止或抑制后续结果，`/sync` 不再返回普通成功回复。
- 手表离页后任务可运行超过 30 分钟；ESP32 不保持 WS，也不以本地 120 秒判定 server 任务失败。
- 进程重启后，未开始的任务可恢复；已经调用 Hermes、无法证明是否产生副作用的任务不得自动重放。
- server 与 ESP32 对可显示文本使用一致的 UTF-8 字节契约，不截断到非法编码。
- Inbox 写入使用独立 internal credential；公网 device token 只能读 Inbox、标记已读，不能创建通知。
- WS 主链路具备 ASR、queue wait、Hermes、persist、delivery 分段耗时和错误计数，日志不包含 token、原始音频或完整隐私文本。
- 原有 WSS、`/sync`、7 字段 HTTP compatibility、后台回复气泡、Inbox 列表/已读体验通过回归。

加分目标：

- MiMo ASR 与 Hermes HTTP 连接复用，减少重复建连。
- server 音频接收不再在 `list[bytes] -> b"".join()` 时瞬时持有两份完整音频。
- ESP32 internal RAM 不增加；扩大的对话文本缓存必须进入 PSRAM，并记录前后基线。

## 范围与非目标

本轮明确要做：

- watch endpoint request/session/conversation 的原子幂等与可恢复状态推进。
- 单设备、单 Hermes conversation 的 server worker 串行执行。
- WS request cancel 与 worker/session 一致化。
- 长任务从 FastAPI 单次请求等待中解耦。
- ESP32 前台等待到本地期限后转为 server pending，而不是伪造 timeout。
- UTF-8 长度契约与安全复制。
- Inbox 内部写入鉴权和公网暴露收口。
- server pytest、ESP32 source/build、Docker smoke、公网 gate、真机回归。

本轮明确不做：

- 不修改 `official_chat` 主线。
- 不实现 MQTT + UDP；继续使用前台 WSS + 后台 `/sync`。
- 不做多设备、多入口、多 agent 调度 UI。
- 不把完整历史、Hermes memory 或任务数据库搬到 ESP32。
- 不让 ESP32 保存 Hermes/MiMo/internal Inbox key。
- 不引入 Redis、Celery、Kafka 或 Kubernetes；当前单用户规模先用 SQLite + 单 worker。
- 不承诺撤销已经由外部工具完成的真实副作用；取消只能停止尚未开始的工作，或 best-effort 中止正在等待的 Hermes 请求并抑制结果投递。
- 不以调 Cloudflare Tunnel transport 代替应用正确性修复；现有证据已经否定 QUIC 是当前延迟主解。

## Owner 与允许修改

Server owner：

- `server/watch_voice_endpoint/app.py`：FastAPI/WS 适配、鉴权、连接注册、兼容 endpoint。
- `server/watch_voice_endpoint/session_repo.py`：request claim、任务状态、取消状态和恢复事实源。
- `server/watch_voice_endpoint/conversation_repo.py`：对话消息幂等落库和 delta 查询。
- 可新增 `server/watch_voice_endpoint/watch_job_runner.py`：单 worker、Hermes 调用生命周期和完成回调；不得变成通用任务框架。
- `server/watch_voice_endpoint/tests/`：行为和竞态测试。

ESP32 owner：

- `memory_watch_service`：前后台生命周期、pending snapshot、FreeRTOS worker/queue/event group。
- `memory_watch_voice_client`：`/sync` 窄协议、UTF-8 边界、HTTP buffer。
- `memory_watch_ws_client`：WS transport 与 turn-level event 映射，不向 UI 扩散原始 frame。
- UI/controller 只读取快照和表达录音/取消/打开页面意图；不得新增 session 状态机。

部署 owner：

- `compose.local.yml`、`compose.cloud.yml`、`deploy/` 只负责非秘密环境变量、镜像和 healthcheck。
- 所有 key/token 继续只存在本机或 `/opt/ai-memory-watch/secrets`，不进入仓库和验证输出。

## 固定协议与兼容边界

保持的公开接口：

```text
GET  /v1/watch/health
WS   /v1/watch/ws
GET  /v1/watch/sync
POST /v1/watch/voice-command                 # compatibility/smoke
POST /v1/watch/text-command                  # compatibility/smoke
POST /v1/watch/request/{request_id}/cancel
GET  /v1/watch/inbox
POST /v1/watch/inbox/{notification_id}/read
```

新增内部接口：

```text
POST /internal/watch/inbox
Authorization: Bearer <WATCH_INTERNAL_API_KEY>
```

固定规则：

- Cloudflare 公网不得路由 `/internal/*`。
- 原公网 `POST /v1/watch/inbox` 在迁移完成后默认关闭；GET 与 mark-read 保留。
- WS 与 HTTP compatibility 必须共享同一个 session claim，不能形成两套幂等域。
- HTTP compatibility 最多同步等待 115 秒；等待超时只表示当前 HTTP caller 未等到结果，不得把仍在运行的 server session 改为 timeout。
- 长任务正式结果仍通过 conversation + `/sync` 返回。
- `/sync` 保持 `schema_version=1`，新增字段只能是 ESP32 可忽略的向后兼容可选字段；如需破坏性变化，必须升 schema version 并先双栈。

## Server Session 状态模型

第一版最小状态保持：

```text
accepted
  -> asr_ready
  -> error / canceled

asr_ready
  -> running
  -> canceled

running
  -> done / error / canceled / interrupted

done / error / canceled / interrupted
  -> terminal，禁止回退
```

公开映射：

```text
accepted / asr_ready / running -> running
done                            -> done
error / interrupted             -> error
canceled                        -> canceled
```

恢复规则：

- `asr_ready`：Hermes 尚未开始，可由 worker 在重启后安全重新 claim。
- `running + assistant message 已存在`：reconcile 为 `done`，不重新调用 Hermes。
- `running + 无 assistant message + 有上游可恢复 response/job id`：按 Hermes 官方能力 reattach/poll。
- `running + 无 assistant message + 无可恢复 id`：标记 `interrupted`，不自动重放可能有副作用的工具任务。
- `accepted` 长时间没有 ASR：标记明确的上传/ASR error，不伪装成 Hermes timeout。

禁止恢复规则：

- 不允许“容器重启后把所有 running 自动重新发给 Hermes”。这会重复创建提醒、重复写记忆或重复执行外部工具。

## 幂等与一致性设计

Session claim：

- `SessionRepo.create_or_get(device_id, request_id)` 必须在单个 SQLite transaction 中完成，并返回 `created`。
- 只有 `created=true` 的 caller 可以进入 ASR。
- `created=false + active` 返回现有状态，不启动第二次 ASR/Hermes。
- `created=false + terminal` 直接 replay session 结果。

Conversation message：

- 新增唯一约束：`UNIQUE(device_id, request_id, role)`。
- 新增 `add_message_once(...)`；冲突时返回已有消息，内容不同则记录 consistency error，不覆盖。
- 上线 unique index 前先查询并记录现有重复项；备份数据库后按明确规则清理，不能让 migration 因历史重复直接失败。

Terminal replay：

- `session.reply_text + last_delivered_message_id` 是 conversation 被保留策略淘汰后的 replay fallback。
- 找不到 conversation message 时只能由 session 重建发送 payload，禁止继续执行 ASR/Hermes。
- replay 不重新插入已被 20 条策略淘汰的历史消息，避免旧消息挤掉当前对话；只响应当前 retry。

完成顺序：

```text
Hermes final reply
  -> add_message_once(assistant)
  -> session transition done(reply_text, message_id)
  -> best-effort WS delivery
```

启动 reconcile：

- 若 assistant message 已落库而 session 仍为 running，补 transition done。
- 若 session done 但 message 不存在，保留 session reply fallback，不重跑任务。

外部工具 exactly-once 边界：

- watch endpoint 保证同一 request 只调用一次 Hermes。
- 向 Hermes input/instructions 附带 request_id 作为审计和工具幂等上下文。
- 外部工具若需要真正 exactly-once，工具适配层仍应使用 request_id/idempotency key；仅靠 HTTP 连接无法证明副作用只执行一次。

## 长任务 Worker 设计

先做 Hermes 0.18.2 能力探针：

- 查安装版本源码和官方文档，确认 `/v1/responses` 是否支持 background、response id、status query、stream、cancel 或 reconnect。
- 用无副作用请求验证，不输出 API key 或完整用户文本。
- 若支持可恢复异步任务，持久化上游 response/job id 并优先 reattach。
- 若不支持，使用以下最小本地 worker 路线。

最小本地路线：

- FastAPI 启动时创建一个 `watch_job_runner` worker。
- worker 从 SQLite claim `asr_ready` session；当前只有 `watch-001`，固定并发度 1，避免同一 Hermes conversation 并发乱序。
- `_ws_finish_audio` 只负责 claim、ASR、user message、`asr_ready` 和 `asr_result/task_started`；不再同步 await Hermes final reply。
- worker 调用 Hermes 后写 conversation/session，并通过当前 authenticated WS connection best-effort 推送。
- WS 不在线时不算失败，ESP32 由 `/sync` 补拉。
- server worker timeout 与 ESP32/HTTP caller timeout 解耦。初始建议 `HERMES_JOB_TIMEOUT_SECONDS=3600`，但必须先用无副作用 fake/stub 验证；不得为了测试真实等待一小时。
- `httpx` timeout 拆分：短 connect/write timeout，长 read timeout；值写入 env.example，不写秘密。

连接注册：

- authenticated WS connection 可按 `device_id` 注册一个当前 delivery sink。
- 新连接替换旧连接，旧连接只停止推送，不影响任务。
- job/session 不保存 WebSocket 对象；连接仅用于 best-effort delivery。
- worker 完成的真相必须先落库，再推 WS。

## 取消语义

取消分三种情况：

```text
尚未被 worker claim：
  session -> canceled，worker 永远不调用 Hermes。

Hermes request 正在本进程等待：
  持久化 cancel_requested/canceled；取消本地 asyncio task；
  即使上游无法撤销，也不再写普通 done reply。

已经 done：
  返回已完成结果，保持当前“完成优先”语义。
```

实现约束：

- HTTP 与 WS 任务必须进入同一个 task registry/session owner。
- `_canceled_requests`、`_inflight_requests` 和 `_ws_background_tasks` 不再分别表达互相矛盾的业务事实。
- 内存 task registry 只用于取消当前进程 task；SQLite session 是跨请求真相源。
- cancel 与 completion 必须测试竞态：同一时刻只有一个 terminal 状态获胜。
- 如果外部工具已经产生副作用，server 只能记录 canceled-after-start 证据，不能声称动作已撤销。

## ESP32 行为修复

保持现有体验：

```text
前台 active turn：WS 实时等 ASR/reply
离页 pending：关闭 WS，/sync 每 5 秒
无 pending：仅 Inbox 低频轮询
重进页面：foreground reconcile + 当前 pending /sync
```

修复点：

- 前台 WS 等待达到本地 `timeout_ms` 时：
  - 已收到 ASR/task accepted：转为 `conversation_pending`，启动 `/sync`，UI 保持“后台处理中”，不能显示 Hermes timeout。
  - 尚未确认 server 接收 request：按 transport/upload error 处理，不能无限 `/sync session=none`。
- server `interrupted/error/canceled` 才能终止 pending；ESP32 不按本地累计时间猜测长任务失败。
- `/sync` 仍是单一后台入口，不恢复 session/conversation/inbox 多 URL 切换。
- UI/controller 不新增 server 状态判断；状态翻译留在 `memory_watch_service`。

FreeRTOS 约束：

- `memory_watch_service` owner task 继续通过 queue 接收命令和 worker 结果。
- WS 等待继续使用 event group 表达 ASR/reply/error/disconnect 组合事件。
- `/sync` worker 继续独立 task + queue，不在 UI poll/getter 中做网络。
- 不新增裸 `volatile` pending/cancel flag；共享状态沿用 critical section、queue、event group 或 task notification。

## UTF-8 与显示契约

问题基线：server 当前按 80 个 Python 字符截断，ESP32 service 主要显示缓冲只有 128 字节；80 个中文可能接近 240 字节。

目标契约：

- server 以 UTF-8 bytes 而不是 Python code point 作为设备输出上限。
- 第一版允许 conversation message 最大 255 bytes，必须以完整 UTF-8 code point 结束。
- `memory_watch_voice_client` 的 256-byte message buffer 保持不变。
- 若 `memory_watch_service` 需要保存完整 255-byte对话文本，扩大后的最近 5 轮缓存必须迁入 PSRAM；不得直接增加 static internal RAM。
- snapshot 可保留更短的 UTF-8-safe preview，但 Hermes 对话列表应读取 PSRAM conversation item。
- server prompt 继续要求短回复；硬截断只作为协议兜底，优先在句号/换行边界收口，不能切断 UTF-8。

验证样例：

- 纯 ASCII 边界。
- 40/80 个中文。
- 中英混排。
- emoji 四字节编码。
- 刚好落在多字节字符中间的长度。
- 超长 ASR 文本不得让整份 `/sync` 永久解析失败。

## Inbox 内部鉴权

目标：Hermes 主动通知生产者与手表消费者使用不同 credential。

迁移步骤：

1. 新增 `WATCH_INTERNAL_API_KEY`，只放 secrets 文件和容器环境。
2. 新增 `POST /internal/watch/inbox`，只接受 internal bearer key。
3. Hermes/内部脚本切到 Docker network 内部地址调用。
4. 保留公网 `GET /v1/watch/inbox` 与 mark-read 的 device token 鉴权。
5. 更新 Cloudflare/public exposure gate，断言 `/internal/watch/inbox` 为 404。
6. 关闭原 `POST /v1/watch/inbox`；迁移窗口如需兼容，必须显式 env 开关且默认 false。

禁止：

- 不把 internal key 写进 ESP32、sdkconfig、仓库、日志或 smoke 输出。
- 不通过前端隐藏按钮代替 server 鉴权。

## 观测与性能优化

先补观测，再做优化：

每个 request 记录非秘密阶段指标：

```text
upload_bytes
upload_ms
asr_ms
queue_wait_ms
hermes_ms
persist_ms
delivery_mode = ws | sync
terminal_state
error_stage / error_code
```

日志规则：

- 可记录 device_id、request_id、字节数、时长、状态和错误类别。
- 不记录 token、Authorization、API key、原始音频、MiMo/Hermes 原始响应体或完整用户文本。
- WS broad exception 必须至少记录 exception type、stage 和 request_id，不能静默吞掉。

性能优化按证据执行：

1. FastAPI lifespan 复用 MiMo/Hermes `httpx.AsyncClient`。
2. 测量后再判断 ffmpeg Ogg->WAV 是否值得替换；不假设 MiMo 支持直接 Ogg。
3. server audio accumulator 改为受控 `bytearray`、spooled file 或流式写入，避免 `b"".join()` 双份峰值。
4. 保持 5 秒 `/sync` interval，不引入已否决的 `poll_after_ms`。
5. 边录边传只作为后续体验优化；它会增加 ESP32 recorder/WS 并发复杂度，不阻塞本轮正确性修复。

## 分阶段执行

### 阶段 0：冻结基线与 Hermes 能力探针

- 备份本地和云端 `session.db/conversation.db/inbox.db`，只记录路径和校验结果，不提交数据库。
- 记录现有 schema、重复 `(device_id, request_id, role)` 数量和 active session 数量。
- 确认 Hermes 0.18.2 async/background/cancel/status 能力。
- 为当前审查问题先补失败测试，不改生产行为。

验收：

- 能明确选择“复用 Hermes async id”或“SQLite 单 worker”路线。
- 并发重复、终态缺 message、running cancel、restart recovery 测试在旧实现上能稳定复现问题。

### 阶段 1：原子幂等与 terminal replay

- 实现 session `create_or_get` 原子 claim。
- 实现 conversation `add_message_once` 和 unique migration。
- terminal session 缺 conversation 时从 session replay，禁止重跑。
- 收敛 HTTP/WS 到同一幂等域。

验收：

- 20 个并发相同 request 最多调用一次 fake Hermes。
- conversation 淘汰后重试相同 request，Hermes 调用计数不增加。
- 内容冲突返回明确 consistency error，不覆盖历史。

### 阶段 2：长任务 worker 与恢复

- 新增窄 `watch_job_runner` 或复用 Hermes async 能力。
- ASR 完成后提交持久 session，WS handler 不再同步持有 Hermes 全生命周期。
- 串行同一 device conversation。
- 实现 startup reconcile 和 `interrupted` 边界。
- HTTP caller timeout 与 server job timeout 解耦。

验收：

- fake Hermes 运行超过 HTTP/ESP wait timeout 后，job 仍能 done 并由 `/sync` 返回。
- WS 断开不影响落库。
- 重启前 `asr_ready` 可继续；未知副作用的 `running` 不自动重放。

### 阶段 3：取消一致性

- cancel endpoint 直接操作 session/job runner。
- 删除或降级互相冲突的内存业务 registry。
- 处理 cancel-before-claim、cancel-running、done-then-cancel 三种情况。

验收：

- cancel-before-claim 的 Hermes 调用计数为 0。
- cancel-running 后不新增 assistant done message。
- done-then-cancel 返回原 done，不篡改历史。
- cancel/completion 并发循环测试无双终态。

### 阶段 4：ESP32 pending 与 UTF-8

- 前台本地等待期限改为 detach/pending，不再误报 server timeout。
- 对话文本使用统一 UTF-8 byte contract。
- 如扩大缓存，迁 PSRAM 并记录 internal/PSRAM/stack 基线。

验收：

- 120 秒边界后仍通过 `/sync` 收到 fake long reply。
- 中文/emoji 不出现非法 UTF-8。
- `printf_esp32_memory_stats()` 与 task stack 日志证明 internal RAM/栈不恶化。

### 阶段 5：Inbox 内部写入收口

- 增加 internal endpoint/key。
- 更新内部脚本、测试和 public exposure gate。
- 关闭公网 device-token Inbox create。

验收：

- 内网 internal token create 成功。
- device token 调用 create 失败。
- 公网 `/internal/watch/inbox` 为 404。
- 手表 GET/list/read 与全局气泡不回退。

### 阶段 6：观测与低风险性能优化

- 补 WS 主链路指标和分段日志。
- 复用 HTTP client。
- 根据峰值证据优化 server 音频 accumulator。
- 不在没有数据时重写录音/ffmpeg/Cloudflare 路线。

验收：

- 能从一次 request 指标区分公网、upload、ASR、queue、Hermes、delivery 耗时。
- health/metrics 能覆盖 WS 主链路，不再只统计 HTTP smoke。
- 并发和大音频测试无未限制内存增长。

### 阶段 7：部署、回退与真机闭环

- 本地 Docker 完成 migration/release gate。
- 云端先备份 SQLite 和当前镜像 digest/tag，再部署新镜像。
- 公网 runtime/private exposure/smoke 通过后再做真机。
- 真机验证前台短任务、离页长任务、取消、重进对账、Inbox 气泡、中文边界。

验收：

- 现有体验无回归。
- 至少一个无副作用 fake/stub 长任务跨过 120 秒边界后成功回表。
- 不用真实工具动作测试并发重复；工具 exactly-once 用 fake counter 证明。

## 测试计划

Server 必增测试：

```text
test_concurrent_duplicate_request_calls_hermes_once
test_terminal_session_without_message_replays_without_execution
test_duplicate_conversation_role_returns_existing_message
test_cancel_before_worker_claim_prevents_hermes_call
test_cancel_running_suppresses_terminal_reply
test_done_wins_late_cancel
test_asr_ready_recovers_after_restart
test_running_without_upstream_id_becomes_interrupted
test_running_with_assistant_message_reconciles_done
test_http_wait_timeout_does_not_timeout_server_job
test_ws_disconnect_still_persists_long_reply
test_internal_inbox_requires_internal_token
test_public_device_token_cannot_create_inbox
test_utf8_reply_limit_preserves_codepoints
```

固定验证命令：

```powershell
uv run --with pytest==8.3.4 --with fastapi==0.115.6 --with httpx==0.28.1 --with python-multipart==0.0.20 --with "uvicorn[standard]==0.34.0" `
  python -m pytest server/watch_voice_endpoint/tests -q

uv run python -m pytest `
  tests/test_memory_watch_service_source.py `
  tests/test_memory_watch_voice_client_source.py `
  tests/test_memory_watch_ws_client_source.py `
  tests/test_memory_watch_ui_source.py -q

.\server\watch_voice_endpoint\release_gate.ps1 -RebuildContainer

. D:\esp-idf\v5.5.3\esp-idf\export.ps1
idf.py build

uv run python scripts/context/validate_context.py --level standard `
  --q "AI Memory Watch Hermes V2.5 conversation reliability" --brief

git diff --check -- . ':!managed_components'
```

公网部署后：

```powershell
.\server\watch_voice_endpoint\runtime_status.ps1 `
  -BaseUrl "https://watch.934000.xyz" `
  -SkipDocker -SkipHermesApi -SkipServiceHealth -AssertPrivateNotExposed

.\server\watch_voice_endpoint\smoke_test.ps1 `
  -BaseUrl "https://watch.934000.xyz" -SkipServiceHealth

.\server\watch_voice_endpoint\websocket_smoke_test.ps1 `
  -BaseUrl "https://watch.934000.xyz"
```

真机日志模式：

```powershell
.\scripts\board\agent_serial_monitor.ps1 `
  -Port COM3 -Action monitor -DurationSeconds 60 `
  -Tag hermes-v25-conversation-reliability `
  -Pattern 'memory_watch|voice-ws|websocket|sync|session|conversation|cancel|reply arrived|bubble|UTF|panic|Guru|stack overflow|NO_MEM'
```

## 部署与回退

数据库迁移：

- 所有 schema 变化必须 additive，并使用明确 schema/user version。
- migration 前复制 `/opt/ai-memory-watch/watch-data` 到带时间戳备份目录。
- unique index 建立前先检测重复；发现重复时停止自动 migration，输出非秘密统计并人工确认清理规则。
- 不在无备份时修改云端 SQLite。

镜像回退：

- 部署前保留当前 watch endpoint image digest 或打不可变 pre-v2.5 tag。
- 新版本先本机，再阿里云；香港中转只转发，不部署业务状态。
- 失败时先回退容器镜像；additive schema 允许旧版忽略新列。
- 如果 migration 改了唯一索引或状态值且旧版不兼容，恢复备份数据库，不用 `git reset --hard` 或直接删除数据。

安全回退：

- internal Inbox 新路径上线后，先确认 Hermes/脚本已切换，再关闭旧 POST。
- 回退时可短期开兼容开关，但不得把 internal key 交给 ESP32。

## 进度

- `[x]` 完成当前代码、线上环境、测试覆盖和资源基线审查。
- `[x]` 确认 owner：server session/conversation/inbox 是云端真相源，ESP32 保持薄客户端。
- `[x]` 阶段 0：冻结基线、备份数据库、完成 Hermes 0.18.2 能力探针并补失败测试。
- `[x]` 阶段 1：修复原子幂等、conversation 唯一约束、terminal replay，并让 HTTP/WS 默认共享 session claim。
- `[x]` 阶段 2：使用 Hermes 原生 `/v1/runs` 实现长任务、delivery 和重启恢复边界。
- `[x]` 阶段 3：统一 WS/HTTP cancel 与 terminal 竞态。
- `[x]` 阶段 4：修复 ESP32 pending timeout 与 UTF-8 byte contract；未扩大缓存，因此无需 PSRAM 迁移。
- `[x]` 阶段 5：迁移 internal Inbox 写入鉴权并关闭公网 create。
- `[x]` 阶段 6：补 WS 分段观测、同设备串行、Hermes HTTP 连接复用和 `bytearray` 音频累计。
- `[x]` 审查修复：补 terminal replay、Hermes run 瞬时重试/未知态、重连动态投递、`request_accepted` pending 门槛、readiness、真实 POST 私有 gate 与容器最小权限。
- `[x]` 云端运行加固：为 Hermes/watch 设置 memory、swap、PID 和日志上限，部署阿里云容器 health timer 与香港 relay health timer，并将阿里云旧 cloudflared 收敛为显式 profile。
- `[x]` Cloudflare 主路切换：`watch.934000.xyz` 已从香港 relay connector 切到阿里云本机 cloudflared connector；云端 Compose 增加 `18787/19119` 本机兼容端口，删除香港 `ct-23` connector 后公网不再返回 `502`。
- `[x]` MiMo 按量付费 API 修复：新的 `sk-` key 与 `https://api.xiaomimimo.com/v1` 已同步到阿里云 Hermes secret、Hermes 数据目录 `.env` 和 watch endpoint ASR secret；Hermes `/v1/responses`、公网 HTTP mock/WSS smoke，以及 HTTP/WSS 真实 Ogg Opus ASR smoke 均通过。
- `[x]` 2026-08-05 Hermes Dashboard 权限修复：香港 Hermes gateway/Dashboard 以 UID/GID `10000` 运行，但持久 `.env` 与 `auth.json` 曾为 `root:root 0600`，导致每次 agent run 在 `load_dotenv()` 失败。已仅修正两文件为 `10000:10000 0600`；新的无工具文本诊断请求返回 `completed` 和非空回复。
- `[ ]` 阶段 7（部分完成）：阿里云部署、公网 gate、HTTP/WSS smoke 已完成；Windows Docker 未启动且 ESP32 COM3 不在线，待补本地容器与真机闭环。

## 决策记录

- 2026-07-14：V2.5 优先修正确性，不先做边录边传、MQTT 或 Cloudflare 路线重构。原因：重复工具执行、假取消和 120 秒上限会直接破坏产品语义，优先级高于几百毫秒优化。
- 2026-07-14：当前单用户/单设备不引入 Redis/Celery，优先 SQLite + 单 worker。原因：现有云端 2C2G 足够，新增分布式组件会扩大部署与恢复面。
- 2026-07-14：同一 device 的 Hermes conversation 第一版串行执行。原因：Hermes 使用固定 conversation id，并发任务可能乱序写历史或重复调用工具。
- 2026-07-14：运行中任务在没有上游可恢复 id 时，重启后标记 interrupted，不自动重放。原因：无法证明外部工具是否已经产生副作用。
- 2026-07-14：保留前台 WSS + 后台统一 `/sync`，不恢复多个后台接口。原因：该链路已经完成真机验证，问题在 server 任务可靠性而非 transport 方向。
- 2026-07-14：不采用 `poll_after_ms`，pending `/sync` 继续每 5 秒。原因：用户已明确否决该字段，且 server 无法准确预测 Hermes 完成时间。
- 2026-07-14：采用 Hermes `0.18.2` 原生 `/v1/runs`，而不是自建 SQLite worker 执行 Hermes。watch session 持久化 run ID；Hermes run 404 映射为 `interrupted`，不自动重放。
- 2026-07-14：HTTP compatibility 默认也接入 session + `/v1/runs`，115 秒只限制 caller 等待；`WATCH_HTTP_ASYNC_RUNS_ENABLED=false` 仅作为旧 `/v1/responses` 紧急回退。
- 2026-07-14：保持 ESP32 现有 128-byte 对话 buffer，不迁 PSRAM；server reply 上限收敛到 120 UTF-8 bytes，避免为了 V2.5 增加 internal RAM。
- 2026-07-14：WS 增加 `request_accepted`，ESP32 只有在 accepted 或 ASR 已确认后才进入后台 pending。原因：TCP/WS 发送成功不等于 server 已创建 session，未确认断线不能无限补拉不存在的 request。
- 2026-07-14：Hermes `0.18.2` `/v1/runs` 未消费 `Idempotency-Key`；启动 POST 仍携带稳定 request_id 作未来兼容，但响应不确定时不自动重试并标记 `interrupted`。原因：未知副作用任务不能自动重放，也不能伪装成普通失败。

## 意外与发现

- V2.4 已删除 ESP32 本地长任务 timeout，但 server 实际仍用 `HERMES_TIMEOUT_SECONDS=120`，因此“server 是长任务真相源”与真实执行上限不一致。
- 当前 terminal replay 在 conversation message 已被 20 条策略淘汰后会继续重新执行 Hermes，幂等只覆盖了消息仍存在的情况。
- HTTP cancel registry 与 WS background task registry 分离，取消状态不会进入 WS session。
- server 按字符截断，ESP32 按字节存储，80 个中文与 128-byte service buffer 不兼容。
- WS 正常路径已通过测试，但 `/health` request metrics 主要统计 HTTP compatibility，无法代表真实手表主链路。
- Hermes run 状态只存在于 Hermes 进程内且 terminal TTL 为 3600 秒；该限制决定了 endpoint 只能持久化 run ID 并在丢失时中断，不能承诺 Hermes 重启后的结果恢复。
- Windows Docker Desktop 当前未运行；为避免同名 cloudflared connector 与云端重复上线，本轮没有启动本地 Compose。
- Windows 当前只枚举到 `COM1`，ESP32 `COM3` 不在线，因此真机阶段不能在本轮自动执行。
- 审查发现 readiness 失败后其他业务入口仍可能继续 lazy 初始化；现已统一 gate，并允许短暂 repository 初始化失败后重试。
- 审查发现 GET 无法证明内部 POST 路由未暴露；private gate 已改为无凭据 POST，405 不再视为安全。
- 阿里云 2 GiB 主机曾在没有明确 OOM kill 的情况下持续进入 memory pressure，导致 Hermes/watch/sshd 一起失去响应；容器无资源边界会把单服务峰值扩大成整机故障。
- autossh 在阿里云重启后会指数退避，即使 connector 和 systemd unit 都显示 active，也可能因 listener 尚未恢复而持续返回 502；必须用业务健康检查驱动重连。
- 2026-07-29 复测确认：公网 `502` 与手表端 TLS/DNS 无关，删除香港 `ct-23` 后 `watch.934000.xyz` 能稳定到达阿里云 watch endpoint。随后发现 MiMo `sk-` key 被错误搭配旧 `token-plan-cn.xiaomimimo.com/v1`；Hermes 持久数据目录 `.env` 与 watch endpoint 的独立 ASR secret 都必须同步改为 `https://api.xiaomimimo.com/v1`，否则文本链路可恢复但真实语音会在 ASR 阶段返回非 2xx。完成三处同步并重建 endpoint 后，真实 Ogg Opus ASR 恢复。
- 2026-08-05 香港 Hermes Dashboard 的固定英文 `unexpected error` 来自 agent run 读取 `/opt/data/.env` 的 `PermissionError`，不是模型、ASR、WSS 或 ESP32 故障。以后人工更新 `/opt/ai-memory-watch/hermes-data/.env` 或 `auth.json` 后，必须保持 UID/GID `10000:10000` 和模式 `0600`；仅 `/health` 通过不足以证明 agent 可执行。

## 验证与验收

截至 2026-07-14 本轮实际结果：

- server pytest：`174 passed`，仅保留 1 条既有 Pydantic `dict()` deprecated warning。
- ESP32 Hermes source tests：`55 passed`。
- ESP-IDF 5.5.3 build：通过；`111.bin` 约 10.77 MiB，最小 app partition 余量约 23%。
- 阿里云：Hermes、watch endpoint、cloudflared 均运行；新 watch endpoint healthy。
- SQLite migration：session 新列与 conversation 唯一索引已生效；active session `0`，duplicate group `0`。
- 公网 private gate：`/health`、`/v1/models`、`/v1/responses`、`/internal/watch/inbox` 均未暴露。
- 公网 HTTP mock Ogg、cancel、invalid token 与 WSS smoke 全部通过；HTTP/WSS 同 request 测试只启动一次 Hermes run。
- 云端一次 WSS 指标：upload `351 ms`、Hermes `16031 ms`、persist `2 ms`、delivery `ws`。
- 审查修复后公网 WSS mock Ogg 再次通过，顺序为 `request_accepted -> asr_result -> task_started -> conversation_message`；云端容器以 UID 10001、read-only rootfs、cap-drop ALL 运行。
- context 证据：`docs/context/runs/2026-07-14-attempt-hermes-v25-conversation-reliability.md`、`docs/context/runs/2026-07-14-attempt-hermes-v25-review-fixes.md`。
- 2026-07-20 云端加固：Hermes/watch 资源限制已由 Docker inspect 确认；阿里云与香港 30 秒 timer active；受控停止 relay 后自动恢复；公网 runtime gate 与 WSS smoke 通过。证据：`docs/context/runs/2026-07-20-attempt-hermes-cloud-memory-relay-hardening.md`。
- 2026-07-29 主路切换与 MiMo API 修复：阿里云 `ai-memory-watch-cloudflared` 已注册；`runtime_status.ps1 -BaseUrl "https://watch.934000.xyz" -SkipDocker -SkipHermesApi -SkipServiceHealth -AssertPrivateNotExposed` 通过，watch health `online`，私有路径均未暴露。MiMo `sk-` key 与 `api.xiaomimimo.com` 配置同步到 Hermes secret 和持久 `.env` 后，Hermes `/v1/responses` 返回 completed 中文回复；公网 `smoke_test.ps1 -BaseUrl "https://watch.934000.xyz" -SkipServiceHealth` 通过，`voice_status=done/action=reminder_created/field_count=7`；公网 `websocket_smoke_test.ps1 -BaseUrl "wss://watch.934000.xyz/v1/watch/ws"` 通过，顺序为 `request_accepted -> asr_result -> task_started -> conversation_message`，`reply_status=done`。
- 2026-07-29 真实语音复验：手表端曾出现 `asr_or_agent_error`，云端日志将请求定位为 `stage=asr` 的 `HTTPStatusError`。根因是 watch endpoint 仍保留旧 Token Plan ASR URL；同步其独立 ASR secret 并重建容器后，公网 `make_tts_sample.ps1 -> smoke_test.ps1 -UseRealAsr` 以及 `websocket_smoke_test.ps1 -AudioPath <real-ogg> -MockAsrText ''` 通过，均返回非空 ASR 文本与 Hermes 回复；WS 顺序为 `request_accepted -> asr_result -> task_started -> conversation_message`。
- 2026-08-05 香港生产 Hermes 容器、watch endpoint、relay 与 OpenResty 均 healthy，`watch` 公网 health 仍为 `ok/hermes_online`，但 Dashboard agent run 失败。修正 `/opt/ai-memory-watch/hermes-data/.env` 与 `auth.json` 的属主后，以新诊断会话调用私有 `/v1/responses` 得到 `HTTP 200`、`status=completed`、非空回复；没有重启 Hermes，因此未中断现有连接。

最终验收必须同时满足：

- 正确性测试全部通过。
- 本地 release gate 与公网 gate 通过。
- 私有 Hermes/internal 路径不公开。
- ESP32 build 通过，RAM/stack 不恶化。
- 真机短任务、离页 pending、取消、重进、气泡和 Inbox 均通过。
- active plan Progress、Validation、Next Step 更新后才能宣布对应阶段完成。

## 幂等与恢复

- 每完成一个可提交小闭环，立即更新本计划“进度/验证与验收/下一步”。
- server schema、worker、ESP32、Inbox security 分阶段提交，不混成一个不可回退大提交。
- 中断后优先从第一个未勾选阶段继续；先读取本计划和对应 run，不重复已证伪路线。
- 涉及 SQLite migration 前必须先备份；涉及 FreeRTOS/RAM/PSRAM 时必须按仓库规则新增 attempt run、更新 CHANGELOG、更新本计划并执行 context standard。
- 不回滚用户现有 `sdkconfig` 或目录重组计划改动，不把它们混入本计划提交。

## 下一步

下一步只补阶段 7 的硬件/本地运行证据，不再扩大 server 范围：

1. ESP32 重新连接并出现 COM3 后执行 `app-flash`，再用 `agent_serial_monitor.ps1` 限时 60 秒采集日志。
2. 真机验证前台短任务、离页长任务、取消、重进对账、回复气泡与 Inbox；重点确认长任务不会回退为本地 timeout。
3. 仅在确认不会启动第二个同名 cloudflared connector 后，再启动 Windows Docker Desktop 并执行本地 `release_gate.ps1 -RebuildContainer`。
4. 真机与本地容器闭环后，将阶段 7 勾选并归档本计划；否则保持 active。
5. 连续观察 24 小时云端 memory snapshot 与容器重启计数；Hermes 若反复触及 768 MiB，优先升级 4 GiB，不放宽到可再次拖死整机。
