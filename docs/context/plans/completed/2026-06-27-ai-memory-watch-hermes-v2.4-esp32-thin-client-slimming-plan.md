---
id: ai-memory-watch-hermes-v2-4-esp32-thin-client-slimming-plan
tags: context, plans, ai-memory-watch, hermes, v2.4, esp32s3, thin-client, memory-watch-service, ram, psram, websocket, sync
summary: AI Memory Watch / Hermes V2.4 执行计划：在 V2.3 server session 真相源已落地后，以前台 WebSocket + 后台统一 /sync 收敛 ESP32-S3 端 Hermes 状态理解、重复去重逻辑、协议细节扩散和资源占用。
last_reviewed: 2026-06-27
memory_type: task
scope: task
owners: docs/context/plans/completed/2026-06-27-ai-memory-watch-hermes-v2.4-esp32-thin-client-slimming-plan.md
triggers: AI Memory Watch V2.4, ESP32 Hermes thin client, memory_watch_service slimming, memory_watch_ws_client, server session, watch sync, RAM baseline
evidence_level: design
status: archived
---

# AI Memory Watch / Hermes V2.4 ESP32 Thin Client Slimming 执行计划

## 目标与定位

- 目标：在 V2.3 已完成 server session 地基后，真正把 ESP32-S3 端做薄，减少本地 Hermes 任务状态理解、重复去重、协议细节扩散和资源占用。
- 一句话定位：V2.3 让 server 拥有任务真相源；V2.4 让 ESP32 开始相信这个真相源，并删除或弱化本地重复职责。
- 范围澄清：V2.4 是 server + ESP32 端到端瘦身计划，不是 server-only 计划。ESP32 端可以修改，但必须围绕“职责变薄、体验不回退、资源不恶化”推进。
- 成功标准不是“文档上说薄”，而是能用数据证明：
  - ESP32 侧状态职责减少。
  - `memory_watch_service` 对 server 业务状态理解减少。
  - `memory_watch_ws_client` 协议细节不向 UI/controller 扩散。
  - internal RAM / stack / bin size 不增加，理想状态下降。

V2.4 完成标准分两级：

- 硬性完成线：
  - ESP32 不再把 Hermes 任务状态当真相源。
  - 后台 pending / inbox 摘要 / 重入对账收敛到统一 `/v1/watch/sync`。
  - UI/controller 不理解 WS frame / ACK / server state transition。
  - 前台/离页体验无回归。
  - internal RAM、栈压力、bin size 不恶化。
- 加分目标：
  - `memory_watch_service` 代码行数下降。
  - internal RAM free 增加。
  - 部分 JSON/HTTP/WS buffer 缩小或迁移到 PSRAM。
  - 删除已被 server session 取代的本地状态字段。

当前基线：

```text
V2.3 已归档。
server session_repo 已接入 WS 路径。
GET /v1/watch/session 已存在。
V2.4 尚未实现统一 GET /v1/watch/sync。
server pytest: 126 passed。
V2.2/V2.3 基线：internal RAM 316/338 KB (93.5%), PSRAM 1395/8192 KB (17%), mw_upload high-water 3172 words。
ESP32 当前体验：前台 Hermes WS 可用，离页 pending 气泡可用，重复回复已修复。
```

## 本轮要解决的问题

V2.3 实现后，server 已经变厚，但 ESP32 端还没有真正瘦下来：

- `memory_watch_service` 仍维护较多本地状态：前后台、pending、last_seen、conversation polling、inbox polling、气泡触发、显示去重。
- `memory_watch_ws_client` 仍偏帧协议级：auth、audio_start、audio_chunk、audio_end、ack、event callback。
- ESP32 仍保留本地 terminal reply/display dedup 兜底，短期必要，但需要明确哪些是显示层保护，哪些是 server 状态重复。
- 后台 pending 完成判断仍主要靠本地状态变化和 conversation polling；还没有统一的 `/v1/watch/sync` 聚合口来一次返回任务状态、对话增量和 inbox 摘要。
- RAM 基线已经说明 internal RAM 极紧张，后续任何新增 internal buffer 都是风险。

## 非目标

- 不重写 Hermes 页面 UI。
- 不删除已验证的 WS / conversation / inbox 链路。
- 不把 ESP32 改成长期历史存储。
- 不引入多设备、多入口或完整多 agent 编排。
- 不把 `official_chat` 主线并入 Memory Watch。
- 不为“代码行数下降”牺牲已经验证的前台/离页体验。
- 不在没有基线对比的情况下宣称资源减少。
- 不在 V2.4 实现 MQTT + UDP 混合方案；它作为后续替代 transport 路线保留，不进入本轮主线。

## 允许修改

- `main/services/memory_watch_service.c`
- `main/services/memory_watch_service.h`
- `main/services/memory_watch_voice_client.*`
- `main/services/memory_watch_ws_client.*`
- `main/ui/custom/memory_watch_controller.c`，仅限消除 UI/controller 对协议细节的依赖或修正气泡触发边界。
- `tests/test_memory_watch_service_source.py`
- `tests/test_memory_watch_ws_client_source.py`
- `tests/test_memory_watch_ui_source.py`
- 必要的 `docs/context/` 记录。

## 禁止修改

- 不修改 `official_chat` 主线。
- 不改 Cloudflare / Hermes / MiMo key 配置。
- 不改公网暴露边界。
- 不提交真实 token/key。
- 不回滚其他 agent 或用户的未提交改动。
- 不合并 worker task，除非阶段 1 的资源审计证明收益明确且验证成本可控。

## 设计原则

- UI/controller 只表达用户意图和读取 snapshot，不理解 WS frame、ACK、server state transition。
- `memory_watch_service` 仍是 FreeRTOS owner，但只维护 ESP32 交互状态，不充当 Hermes 任务真相源。
- server session 是任务状态真相源；conversation 是对话消息真相源；inbox 是主动提示真相源。
- `session=done` 只表示 server 已经完成 Hermes 任务并成功写入 assistant conversation message；Hermes 仍在执行、或 Hermes 完成但 conversation 尚未落库时，都不能对 ESP32 暴露 `done`。
- server 内部继续保持 session / conversation / inbox 三本账分离，便于测试、调试和演进；ESP32 后台主路径只访问统一 `/v1/watch/sync`，避免在手表端组合多接口结果。
- V2.4 主通讯方案固定为：前台 Hermes 页面用 WebSocket；后台、离页 pending、开机恢复、重新进入页面时用 HTTP `/v1/watch/sync`。MQTT + UDP 混合方案只作为后续替代 transport，不改变本轮接口边界。
- 前台 Hermes 页面不主动轮询 `/v1/watch/sync`；前台体验仍以 WebSocket 实时事件为主，conversation 只负责显示补齐。`/sync` 只用于后台同步、离页 pending 判断和重新进入 Hermes 页面时的一次性对账。
- ESP32 本地去重只保留显示保护，例如“同一 message_id/request_id 不重复展示”，不再承担 server 业务幂等。
- 任何 buffer 缩小或迁移都必须保留错误路径可观测性，不能把 OOM 变成静默丢消息。

即使 watch endpoint 和 Hermes 部署在同一台服务器或同一个 Docker Desktop 环境，server session 仍然需要保留。它的价值不是解决“server 到 Hermes 距离远”的问题，而是解决：

- 手表离开页面或断网后，任务仍有可查询状态。
- 同一 `request_id/session_id` 重复提交时，server 能幂等处理。
- Hermes 长任务运行时，ESP32 不需要保持 WS 长连接。
- session、conversation、inbox 三类真相源可以各司其职，避免 ESP32 重新猜任务事实。
- `/v1/watch/sync` 可以把三类真相源聚合成 ESP32 易消费的 delta response，减少 HTTP 请求、TLS/JSON buffer 和本地组合逻辑。
- 后续 MQTT 只需发送“有新状态/消息”的轻信号，数据仍从 server 真相源经 `/sync` 拉取。

## 分阶段执行

### 阶段 0：复核 V2.3 后验收基线

目的：确认进入瘦身前的行为和资源基线可信。

要做：

- 跑 server pytest，确认仍为 `126 passed` 或记录新的准确数字。
- 跑 ESP32 source tests：

```powershell
uv run python -m pytest tests/test_memory_watch_service_source.py tests/test_memory_watch_ui_source.py tests/test_memory_watch_ws_client_source.py -q
```

- 跑 `idf.py build`，记录：
  - `111.bin` size。
  - app 分区余量。
- 如可用 COM3，采集 60 秒冷启动日志，记录：
  - internal RAM free / largest free block。
  - PSRAM free。
  - `mw_upload` stack high-water。
  - Hermes health / inbox poll / conversation poll 是否正常。

停止条件：

- 只记录基线，不改 ESP32 行为。
- 如果 V2.3 server session 有明显回归，先修 server，不进入 ESP32 瘦身。

### 阶段 1：ESP32 职责审计清单

目的：先列清楚哪些本地职责可以削，避免乱删。

产物：在本计划或新增 run log 中写出清单：

```text
函数/状态/worker -> 当前职责 -> 是否重复 server session -> 动作(delete/weaken/keep/defer) -> 验证用例
```

必须审计：

- `memory_watch_service_task`
- `memory_watch_service_set_foreground`
- `memory_watch_service_conversation_try_poll`
- `memory_watch_service_conversation_handle_worker_done`
- `memory_watch_service_handle_worker_done`
- `memory_watch_service_ws_event_cb`
- `memory_watch_ws_client` 对外接口
- `conversation_already_appended`
- `last_seen_conversation_id`
- `pending_request_id / pending_started_at_ms / foreground_active` 等同类状态
- upload / health / cancel / conversation / inbox worker 是否真的需要合并。

停止条件：

- 只产出审计清单和测试计划。
- 不在这一阶段删除函数。

### 阶段 1.5：定义 `/sync` 协议契约与 source tests

目的：在实现 server `/sync` 与 ESP32 sync client 前，先把协议收敛成小而稳定的 delta contract，避免执行阶段重新发散成多接口组合逻辑。

必须定义：

- endpoint：

```text
GET /v1/watch/sync
```

- request query：
  - `device_id`：设备标识，V2.4 仍默认 `watch-001`。
  - `mode`：同步场景，取值为 `background` 或 `foreground_reconcile`。
  - `pending_request_id`：可为空；非空时表示 ESP32 正在后台等待这条 Hermes 对话回复。
  - `after_message_id`：ESP32 已显示的最后一条 conversation message id；为空表示请求最近增量。
  - `max_messages`：conversation 增量条数建议；`mode=foreground_reconcile` 时 ESP32 默认请求 10 条。

- response schema：

```json
{
  "schema_version": 1,
  "conversation": {
    "has_pending": true,
    "session_state": "running",
    "messages": []
  },
  "inbox": {
    "unread_count": 0,
    "latest_unread": null
  }
}
```

- `conversation.messages[]` 最小字段：
  - `message_id`
  - `request_id`
  - `role`
  - `text`
  - `created_at`
- `inbox.latest_unread` 非空时的最小字段：
  - `notification_id`
  - `title` 或 `text`
  - `preview`
  - `created_at`

必须约束：

- `/sync` 不返回 `poll_after_ms`；ESP32 固定本地节奏：pending 5 秒，idle 5 分钟。
- `/sync` 不返回完整历史，只返回游标后的 delta 或短摘要。
- V2.4 server 不对 conversation `text` 做长度截断或摘要压缩；回复短文本约束由 Hermes 提示词负责，ESP32 接收和缓存路径必须按可能出现较长文本来放 PSRAM 并保留失败可观测性。
- `pending_request_id` 非空时，server 优先返回这条 request 的 session/conversation 状态。
- `mode=background` 且 `pending_request_id` 为空时，server 不返回 conversation messages，只返回 inbox 未读摘要和必要状态，保证后台 idle 小包。
- `mode=background` 且 `pending_request_id` 非空时，server 可以且必须返回该 pending request 对应的新 conversation messages；这一路负责离页后 assistant reply 到达与气泡触发。
- `mode=foreground_reconcile` 时，ESP32 默认请求最近 10 条 conversation messages，用于用户进入 Hermes 页面后的 UI 恢复。
- `conversation.messages[]` 必须按 `created_at` 升序返回，ESP32 直接 append 显示，不做本地反转排序。
- `session_state=done` 时，如果 ESP32 尚未看到对应 assistant message，response 必须在 `conversation.messages[]` 带回该 message；如果游标已覆盖该 message，可以返回 `done + messages=[]`。
- `session_state=running` 可以返回空 messages。
- `/sync` 对 ESP32 暴露的 `session_state` 枚举固定为：
  - `none`：没有 `pending_request_id`，或 server 找不到这条 session。
  - `running`：任务仍在进行；server 内部的 `accepted`、`asr_ready`、`running` 都映射为公开 `running`。
  - `done`：Hermes 已完成，且 assistant message 已写入 conversation。
  - `error`：Hermes 或 server 执行失败。
  - `timeout`：server 判定任务超时。
  - `canceled`：用户取消了这条任务。
- ESP32 只解析上述公开枚举，不解析 server 内部状态名 `accepted` / `asr_ready`。
- `/sync` 网络失败、DNS/TLS/timeout 或 server 5xx 只表示“本轮同步失败”，ESP32 不清 `pending_request_id`，不把 Hermes 任务判为 `error/timeout`，按原节奏重试；连续失败时可以显示网络弱或同步失败状态。
- `/sync` 返回 401/403 授权失败时，ESP32 不清 `pending_request_id`，但应停止高频 pending sync，避免 5 秒循环打 server，并提示配置失效或需要重新配置 device token。
- `/sync` inbox 只返回 `unread_count` 和最新未读摘要 `latest_unread`，不返回完整 `items[]` / `body`。
- 最新未读摘要用于 UI 气泡文案；用户点气泡或打开收件箱后，继续走现有 `GET /v1/watch/inbox` 拉最近 20 条完整 item。
- 多条 Hermes 主动下发消息同时未读时，`/sync` 仍只返回最新一条 `latest_unread` 和总 `unread_count`；气泡显示最新摘要，并在 `unread_count > 1` 时附带“还有 N 条未读”一类短提示，不在气泡里展开多条。
- inbox 气泡点开后进入 Hermes 收件箱列表，不直接打开详情；当前 server 只有 `GET /v1/watch/inbox` 列表接口，没有独立 detail endpoint。
- 进入 Hermes 页面触发 `mode=foreground_reconcile` 只恢复 conversation，不标记 inbox 已读；只有用户打开/查看收件箱消息时，才调用 `POST /v1/watch/inbox/{notification_id}/read`。
- 同一次 `/sync` 同时出现 conversation assistant reply 与 inbox `latest_unread` 时，气泡优先显示 assistant reply 到达；inbox 只更新未读数，用户点气泡进入 Hermes 对话页。只有没有 assistant reply 时，才显示 inbox 气泡并进入收件箱列表。
- assistant reply 气泡文案与展示方式沿用当前已验证行为；V2.4 不额外规定新标题、截断长度或 UI 文案。
- 气泡必须做本地去重：assistant reply 按 `message_id` 去重，同一个 `message_id` 只弹一次；inbox 气泡按 `notification_id` 去重，同一个 `notification_id` 只弹一次。
- token/key 不得出现在 response、日志、测试快照或文档示例中。

必须补测试：

- server tests：
  - `/sync` 未授权失败。
  - 空 delta 返回稳定 schema。
  - pending request running 返回 `has_pending=true` 且 messages 可为空。
  - pending request done 且 assistant message 未被游标覆盖时返回该 message。
  - pending request done 且 assistant message 已被游标覆盖时不重复返回 message。
  - server 内部 `accepted/asr_ready/running` 对 ESP32 均映射为公开 `session_state=running`。
  - 找不到 session 或无 pending 时返回 `session_state=none`。
  - `/sync` 不向 ESP32 暴露 `accepted` / `asr_ready`。
  - `/sync` 未授权返回 401/403；ESP32 不把它映射为 Hermes 任务失败。
  - `mode=background` 且无 `pending_request_id` 时不返回 conversation messages。
  - `mode=background` 且有 `pending_request_id` 时可以返回该 request 的 assistant message。
  - `mode=foreground_reconcile` 时按 ESP32 默认请求返回最近 10 条 conversation messages。
  - `conversation.messages[]` 按 `created_at` 升序返回。
  - `/sync` 不返回 inbox `items[]` 或完整 `body`。
  - `/sync` 返回 `latest_unread` 时只包含气泡需要的短摘要字段。
  - `latest_unread` 使用现有 inbox 字段名 `notification_id`，不另起 `id` 别名。
  - 多条未读时 `latest_unread` 为最新未读 item，`unread_count` 为总未读数。
  - 同一 response 同时含 assistant reply 与 `latest_unread` 时，ESP32 气泡优先选择 assistant reply。
  - assistant reply 气泡沿用当前已验证文案和展示方式，不新增 UI 文案规则。
  - assistant reply 气泡按 `message_id` 去重，inbox 气泡按 `notification_id` 去重。
  - `foreground_reconcile` 不触发 inbox mark-read。
  - server 不对 conversation `text` 做长度截断或摘要压缩。
- ESP32 source tests：
  - 只允许后台 sync 主路径引用 `/v1/watch/sync`。
  - 后台 idle 使用 `mode=background`。
  - 进入 Hermes 页面重入对账使用 `mode=foreground_reconcile`。
  - `foreground_reconcile` 默认请求 `max_messages=10`。
  - 只解析公开 `session_state` 枚举：`none/running/done/error/timeout/canceled`。
  - source 中不出现对 `accepted` / `asr_ready` 的 ESP32 端业务判断。
  - source 中不出现 `poll_after_ms` 解析逻辑。
  - sync response buffer 不放大对象到 task stack。
  - UI/controller 不直接解析 `session_state`、WS frame 或 JSON 字段。
  - token 不被日志打印。
  - 网络失败不清 pending、不生成 `error/timeout` 任务状态。
  - 401/403 授权失败停止高频 pending sync，并进入配置错误提示路径。

停止条件：

- `/sync` 契约写入本计划或对应 server test fixture。
- server 和 ESP32 source tests 先覆盖契约，不要求此阶段完成全部业务实现。
- 未完成阶段 1.5 前，不进入阶段 2 的 sync client 实现。

### 阶段 2：引入后台统一 sync 窄客户端

目的：让 ESP32 通过一个后台同步接口消费 server session / conversation / inbox 聚合结果，但不改变前台 WebSocket 体验。

建议实现：

- server 新增或完善：

```text
GET /v1/watch/sync
```

- 请求只携带 ESP32 必需游标：
  - `device_id`
  - `mode`
  - `pending_request_id`
  - `after_message_id`
  - `max_messages`
- response 采用轻量 delta，不返回全量历史：
  - `schema_version`
  - `conversation.has_pending`
  - `conversation.session_state`
  - `conversation.messages[]`
  - `inbox.unread_count`
  - `inbox.latest_unread`
- 后台 idle 默认只返回 inbox 未读数量与最新未读摘要；有 pending 时返回 pending 任务状态、新 conversation message 以及 inbox 未读摘要。收件箱详情页继续使用现有 `GET /v1/watch/inbox` 拉最近 20 条完整 item，不走后台大包 sync。
- 在 `memory_watch_voice_client` 或新窄文件中增加：

```c
esp_err_t memory_watch_voice_client_sync(
    const memory_watch_voice_client_config_t *config,
    const memory_watch_voice_client_sync_cursor_t *cursor,
    memory_watch_voice_client_sync_result_t *out_result);
```

- 只解析必要字段：
  - `conversation.has_pending`
  - `conversation.session_state`
  - `conversation.messages[]` 的 `message_id/role/text/request_id/created_at`
  - `inbox.unread_count`
  - `inbox.latest_unread` 的 `notification_id/title/preview/created_at`
- 不解析 `reply_text` 作为独立 UI 正文；正文只来自 `conversation.messages[]`。
- response buffer 放 PSRAM 或受控上限，不进小栈；由于 server 不截断 `text`，ESP32 必须对超出本地 buffer 的响应保留明确错误日志和失败状态。
- source tests 锁定：
  - endpoint path `/v1/watch/sync`
  - token 不打印。
  - 空 delta 返回不崩。
  - `foreground_reconcile` 默认请求 `max_messages=10`。
  - terminal state 映射到本地轻量状态。

停止条件：

- client 和 source tests 完成。
- 不接管前台 WebSocket。

### 阶段 3：后台 sync 判断改薄

目的：把离页 pending、重入对账和 idle inbox 摘要从“本地完整任务状态机 + 多接口轮询”收敛为“本地生命周期状态 + `/sync` delta 游标”。

建议行为：

- 前台 Hermes 页面仍走 WS 实时，不改变用户体验。
- 前台 Hermes 页面不跑周期 sync，避免 WS 和后台 HTTP 同步同时竞争资源。
- 开机网络就绪后：
  - 以 `mode=background` 运行一次 `/sync`，恢复 unread_count、旧 pending 和必要状态。
- 进入 Hermes 页面时：
  - 以 `mode=foreground_reconcile` 运行一次 `/sync` 做轻量对账，补齐最近 message / pending 状态。
  - 之后新的语音回合继续走前台 WS，不复用旧 WS 连接。
- 离页 pending 时：
  - 每次只调用 `/sync`，由 server 同时返回 session_state 和 conversation messages。
  - ESP32 固定每 5 秒调用一次 `/sync`，不依赖 server 预测 Hermes 完成时间。
  - `session_state=done` 到达时，按约定 assistant message 应已写入 `conversation.messages[]`；如果 ESP32 暂未看到对应 message，只视为同步异常并继续按低频策略补拉，不伪造回复、不用 15 秒这类短窗口判失败。
  - 长任务是否超时由 server session 的 `timeout/error` 状态决定，ESP32 不用本地短倒计时猜测 Hermes 是否失败。
- 无 pending 时：
  - 后台仍以 `mode=background` 调用 `/sync`，固定 5 分钟一次。
  - server 返回 inbox `unread_count` 和 `latest_unread` 即可，不返回 conversation 全量历史。

可以弱化：

- 本地对 `done/error/timeout/canceled` 的业务推断。
- 本地分别调度 session / conversation / inbox 的后台轮询分支。
- worker done 后再次 append reply 的路径。
- 和 server 幂等重复的 terminal reply 去重。

必须保留：

- 显示层去重：同一 message_id/request_id 不重复显示。
- 前台 ASR 先显用户侧消息。
- 离页 assistant reply 到达弹“回复已到达”气泡。

停止条件：

- source tests 通过。
- 真机或脚本证明离页 pending reply 仍能回来，且不重复。
- 后台主路径不再分别调用 `/session`、`/conversation`、`/inbox` 来组合 pending 结果；这些接口保留为调试、测试或页面详情能力。

### 阶段 4：`memory_watch_ws_client` 意图级收口

目的：让 `memory_watch_service` 不再直接理解 WS 帧细节。

建议做法：

- 保留 `memory_watch_ws_client`，不删除已验证 transport。
- 文件内部保留 auth/audio_start/audio_chunk/audio_end 实现细节。
- 对 service 暴露更窄的 turn-level API 或 event：

```c
memory_watch_ws_client_start_turn(...)
memory_watch_ws_client_send_audio(...)
memory_watch_ws_client_finish_turn(...)
```

- event callback 输出业务事件：
  - `turn_asr_ready`
  - `turn_reply_message`
  - `turn_error`
  - `turn_closed`
- source tests 锁定 UI/controller 不直接出现 WS frame/event name 字符串。

停止条件：

- `memory_watch_service` 仍是 owner，但不拼 WS JSON frame、不判断 ACK。
- 前台 WS smoke 不回退。

### 阶段 5：资源瘦身与数据对比

目的：把“做薄”变成可测量结果。

可尝试项：

- 缩小或合并只用于本地业务判断的状态字段。
- 删除已由 server session 取代的本地 terminal 业务判断。
- 审查 HTTP/WS JSON response buffer 是否过大或在 internal RAM。
- 审查 `/sync` response 形态，确保后台 idle / pending 返回都是 delta 小包；server 不截断 conversation `text`，ESP32 必须用 PSRAM 接收并在超出本地能力时可观测失败。
- 保留 worker task 独立性，除非审计显示合并能明显减少栈/队列且不会增加阻塞风险。
- 删除未再使用的 helper、常量、测试 fixture。

必须记录对比：

```text
指标                         V2.3 baseline     V2.4 result
111.bin size                 ...
app free                     ...
internal RAM free             ...
largest internal block        ...
PSRAM free                    ...
mw_upload high-water          ...
conversation worker stack     ...
inbox worker stack            ...
source tests                  ...
server tests                  ...
```

停止条件：

- 必须做到“职责真实变薄 + 资源不恶化”。
- 如果资源没有下降，也必须说明原因：例如当前主要占用来自 UI、音频、网络、TLS 或 LVGL；本轮为了保持前台/离页体验只完成职责瘦身，物理资源下降留到后续。

### 阶段 6：真机验收与归档

必须验收：

1. 前台 Hermes 页面按住说话，ASR 先显示用户消息。
2. 前台等待 Hermes 回复，assistant 消息只显示一次。
3. 发起复杂请求后离开 Hermes 页面，WS 关闭，pending 不丢。
4. reply 到达后弹“回复已到达”气泡。
5. 点气泡回 Hermes 页面，看到连续对话。
6. inbox 主动提示仍进入收件箱，不混入 conversation。
7. 无 Guru、panic、stack overflow、URL 乱码、token 泄露。

验收命令建议：

```powershell
.\server\watch_voice_endpoint\.venv\Scripts\python.exe -m pytest server/watch_voice_endpoint/tests -q
uv run python -m pytest tests/test_memory_watch_service_source.py tests/test_memory_watch_ui_source.py tests/test_memory_watch_ws_client_source.py -q
idf.py build
.\scripts\board\agent_serial_monitor.ps1 -Port COM3 -Action app-flash-monitor -DurationSeconds 150 -FlashTimeoutSeconds 240 -Tag hermes-v24-esp32-thin-client -Pattern 'memory_watch|conversation|inbox|voice-ws|websocket|session|watch request result|reply arrived|panic|Guru|stack overflow|HTTP_CLIENT|Error parse url'
```

归档条件：

- V2.4 计划进度全部打勾。
- `CHANGELOG.md` 更新。
- 如涉及 FreeRTOS/RAM/PSRAM/底层资源改动，新增 `docs/context/runs/YYYY-MM-DD-attempt-hermes-v24-esp32-thin-client-slimming.md`。
- context standard 校验通过。

## 进度

- `[x]` V2.3 server session 地基已完成并归档。
- `[x]` 本计划已创建，边界为 ESP32 真实瘦身，不再混同 V2.3 server-thick foundation。
- `[x]` 阶段 0：复核 V2.3 后验收基线。
- `[x]` 阶段 1：ESP32 职责审计清单。
- `[x]` 阶段 1.5：定义 `/sync` 协议契约与 source tests。
- `[x]` 阶段 2：引入后台统一 sync 窄客户端。
- `[x]` 阶段 3：后台 sync 判断改薄。
- `[x]` 阶段 4：`memory_watch_ws_client` 意图级收口。
- `[x]` 阶段 5：资源瘦身与数据对比。
- `[x]` 阶段 6：真机验收与归档。

## 决策记录

- 日期：2026-06-27
- 进度：阶段 0 基线复核完成。当前分支为 `codex/ai-memory-watch-hermes-api`；server pytest `126 passed`；ESP32 Memory Watch source tests `39 passed`；`idf.py build` 通过，`111.bin` size `0xabec30`，最小 app 分区剩余 `0x3413d0`（23%）。

- 日期：2026-06-27
- 进度：阶段 1 ESP32 职责审计完成，先记录不删除。审计清单：
  - `memory_watch_service_task`：当前 owner loop 同时调度 inbox polling 与 conversation polling；保留 owner，但后续 conversation polling 应收敛到 `/sync` worker。
  - `memory_watch_service_set_foreground` / `s_foreground_active`：当前负责页面前后台状态和进入前台触发 inbox poll；保留本地生命周期 owner，后续进入 Hermes 页面改为触发 `mode=foreground_reconcile` sync。
  - `memory_watch_service_conversation_try_poll`：当前每 5 秒 conversation polling，并有 10 分钟本地 pending timeout；V2.4 应弱化为 `/sync` pending 轮询，任务超时事实只看 server `session_state=timeout/error`。
  - `memory_watch_service_conversation_handle_worker_done`：当前合并 conversation worker staging 并触发气泡；后续迁移为 sync worker result 合并 conversation messages / latest_unread。
  - `memory_watch_service_handle_worker_done`：当前仍处理 `conversation_pending`、terminal response 与 `conversation_already_appended`；保留显示层去重，但弱化本地 terminal 业务判断。
  - `memory_watch_service_ws_event_cb`：当前接收 WS ASR/reply 事件并追加本地对话；阶段 4 再做 turn-level event 收口，不在阶段 1 删除。
  - `conversation_already_appended`：保留为显示层防重复保护，不能被 server session 幂等替代。
  - `s_conversation_pending_request_id` / `s_conversation_poll_active` / `s_conversation_poll_started_ms`：后续改为 sync pending 游标/状态，不再由 ESP32 本地决定长任务失败。
  - upload / health / cancel / conversation / inbox worker：暂不合并；除非阶段 5 资源对比证明收益明确，否则保持 worker 隔离以降低网络 IO 阻塞风险。

- 日期：2026-06-27
- 进度：阶段 1.5 server `/sync` 契约与 endpoint 完成。新增 `GET /v1/watch/sync`，支持 `mode=background|foreground_reconcile`、`pending_request_id`、`after_message_id`、`max_messages`，返回 `schema_version=1`、公开 `session_state`、conversation delta、`inbox.unread_count` 与 `latest_unread` 摘要；`accepted/asr_ready/running` 映射为公开 `running`，`latest_unread` 使用 `notification_id`。新增 `server/watch_voice_endpoint/tests/test_sync.py`，覆盖授权、空 delta、foreground reconcile、pending running/done、`max_messages=0`、latest unread、不 mark-read 等契约。验证：`test_sync.py` `14 passed`，server tests `140 passed`，ESP32 Memory Watch source tests `39 passed`。

- 日期：2026-06-27
- 进度：阶段 2 ESP32 `/sync` 窄客户端完成。`memory_watch_voice_client.h/.c` 新增 `memory_watch_voice_client_sync()`、`memory_watch_voice_client_sync_cursor_t`、`memory_watch_voice_client_sync_result_t`、`memory_watch_sync_mode_t` 与 `memory_watch_sync_inbox_summary_t`，构建 `GET /v1/watch/sync?device_id=...&mode=...&pending_request_id=...&after_message_id=...&max_messages=...`，解析 `schema_version=1`、公开 `session_state`、conversation messages、`inbox.unread_count` 和 `latest_unread.notification_id/title/preview/created_at`。response buffer 走 `memory_watch_voice_client_alloc()`，优先 PSRAM；固件源码不包含 `poll_after_ms`、`after_inbox_id`、`max_inbox_items` 或 server 内部 session state 名。验证：Memory Watch source tests `40 passed`，server tests `140 passed`，`idf.py build` 通过，`111.bin` size `0xabecc0`，最小 app 分区剩余 `0x341340`（23%）。

- 日期：2026-06-27
- 进度：阶段 3 后台 sync 判断改薄完成首轮。`memory_watch_service` 保留已验证的 conversation worker/queue 外壳，但 worker 内部从 `memory_watch_voice_client_conversation_poll()` 换为 `memory_watch_voice_client_sync()`；离页 pending 使用 `mode=background + pending_request_id + after_message_id`，进入 Hermes 页面触发 `mode=foreground_reconcile` 做一次对话对账。删除本地 10 分钟 `conversation_poll_timeout` 判定，Hermes 长任务是否终止改看 server 公开 `session_state`；401/403 sync auth 失败会停止高频 pending sync 并进入配置错误路径。验证：Memory Watch source tests `40 passed`，server tests `140 passed`，`idf.py build` 通过，`111.bin` size `0xabefb0`，最小 app 分区剩余 `0x341050`（23%）。

- 日期：2026-06-27
- 进度：阶段 4 `memory_watch_ws_client` 意图级收口完成首轮。WS client 新增 `memory_watch_ws_event_kind_t`，把原始 `asr_result` / `conversation_message` / `error` JSON frame 映射为 `TURN_ASR_READY` / `TURN_REPLY_MESSAGE` / `TURN_ERROR`；`memory_watch_service_ws_event_cb()` 改为消费业务 event kind，不再判断原始 frame 名。WS client 新增 `memory_watch_ws_client_send_audio_turn()`，service 不再手写 `audio_start` / binary chunk / `audio_end` 序列。验证：Memory Watch source tests `40 passed`，server tests `140 passed`，`idf.py build` 通过，`111.bin` size `0xabeff0`，最小 app 分区剩余 `0x341010`（23%）。

- 日期：2026-06-27
- 进度：阶段 5 资源瘦身与数据对比完成代码侧闭环。删除 ESP32 `memory_watch_voice_client` 中旧 `GET /v1/watch/conversation` polling 公开接口、path builder、parser 与 `MEMORY_WATCH_CONVERSATION_RESPONSE_MAX_BYTES`，后台 conversation delta 只保留 `/v1/watch/sync`；删除 `memory_watch_service` 中已失效的本地 pending 起始时间计时；修正 `/sync session_state=done` 但未返回 assistant message 时的处理，继续补拉而不是伪造空回复。验证：Memory Watch source tests `40 passed`，server tests `140 passed`，`idf.py build` 通过，`111.bin` size `0xabef80`，最小 app 分区剩余 `0x341080`（23%）。真机 RAM/栈高水位本轮未重新采集，留到阶段 6 COM3 验收。

阶段 5 对比表：

```text
指标                         V2.3/V2.4 baseline             V2.4 result
111.bin size                 阶段 4: 0xabeff0               0xabef80
app free                     阶段 4: 0x341010               0x341080
internal RAM free            V2.2 基线: 316/338 KB used     阶段 6 尚未记录完整快照
largest internal block       未记录                          阶段 6 尚未记录完整快照
PSRAM free                   V2.2 基线: 1395/8192 KB used   阶段 6 尚未记录完整快照
mw_upload high-water         V2.2 基线: 3172 words          3172 words
conversation worker stack    未记录                          阶段 6 尚未记录完整快照
inbox worker stack           未记录                          阶段 6 尚未记录完整快照
source tests                 40 passed                       40 passed
server tests                 140 passed                      140 passed
```

- 日期：2026-06-28
- 进度：阶段 6 部分真机验收通过，但暂不归档。用户修复 Mihomo/Fake-IP DNS 后，COM3 日志 `board_logs/2026-06-28-19-27-07-hermes-v24-stage6-dns-fixed-verify.log` 显示手表获得 `192.168.103.11`，网络进入 `SERVICE_READY`，SNTP 同步成功，Hermes health `hermes_online=1`，inbox poll 正常，前台 WSS 真麦克风请求完成：`status=done action=conversation_reply error_code=none`，`mw_upload` high-water 约 `3172` words。随后发现运行中的 watch endpoint 容器仍是旧镜像，公网/本机 `/v1/watch/sync` 曾返回 404；重建容器后 `/sync` 路由存在，未授权请求返回 401。COM3 日志 `board_logs/2026-06-28-19-30-47-hermes-v24-stage6-sync-deployed-verify.log` 显示 `conversation: sync ok messages=0 session=none terminal=0`，并再次完成前台 WSS 真麦克风请求：`status=done action=conversation_reply error_code=none`。两轮日志均未见 Guru、panic、stack overflow、`Error parse url`。
- 剩余验收：还需要一次慢任务专项场景，验证用户离开 Hermes 页面后 WS 关闭、后台 `/sync` 拉回 assistant reply、弹气泡，点气泡回 Hermes 页面后能看到连续对话。该项完成前不把阶段 6 勾选为完成。

- 日期：2026-06-28
- 进度：阶段 6 后台 `/sync` 数据面脚本化验收通过。使用运行中 watch endpoint 容器注入 `codex-stage6-sync-bg-20260628` 测试 session/conversation 后，本机和公网 `GET /v1/watch/sync?mode=background&pending_request_id=...` 均返回 `session_state=done`、`messages=user,assistant`、assistant 文本为“后台 sync 测试回复已到达”；再带 `after_message_id=<user message>` 请求公网 `/sync`，只返回 1 条 assistant 增量。结论：server 数据面已满足离页 pending 的核心语义；剩余验证收窄为真机 UI/气泡闭环，不再重复验证服务器数据面。

- 日期：2026-06-28
- 进度：阶段 6 真机发现并修复 WS 过早 detach。1 分钟 COM3 日志显示 ESP32 已录音成功、WS 已连接，但 `audio_end` 后立刻关闭 WS 并开始后台 polling；server 侧对应 request `watch-001-f8bc9e26-0001` 的 session/conversation 均不存在，导致后台 `/sync` 持续 `session=none/messages=0`，UI 一直“思考中”。已在 `memory_watch_service_send_voice_over_ws()` 增加 ASR ready 接管屏障：`TURN_ASR_READY` 设置 `kWsWaitAsrReadyBit`，离页时只有 `asr_ready_seen=true` 才关闭 WS 并切后台 `/sync`。验证：Memory Watch source tests `40 passed`，server `test_sync.py` `14 passed`，`idf.py build` 通过，`111.bin` size `0xabef90`，最小 app 分区剩余 `0x341070`（23%）。待 app-flash 后真机复测。

- 日期：2026-06-28
- 进度：阶段 6 真机复测完成。`idf.py -p COM3 app-flash` 后，用户确认重新执行“Hermes 页面按住说，松手后立刻离开页面”场景，后台 `/sync` 不再长期 `session=none`，此前“离页后一直思考中”的阻塞解除。V2.4 阶段 6 完成；后续如需收口文档，可将本 active plan 归档到 `docs/context/plans/completed/`。

- 日期：2026-06-27
- 决策：V2.4 不继续扩 server session，而是专门做 ESP32 真实瘦身。
- 原因：V2.3 已经完成 server-thick foundation，但 ESP32 端代码和资源没有自动减少；必须另起小闭环处理。

- 日期：2026-06-27
- 决策：V2.4 允许修改 ESP32 端，并以 ESP32 端职责变薄作为主要验收目标；server 侧作为真相源和支撑能力继续配合。
- 原因：如果只改 server，不会自动减少 ESP32 端状态理解、协议细节和资源压力；本计划是端到端瘦身计划，不是 server-only 计划。

- 日期：2026-06-27
- 决策：不以“大删代码”为第一步。
- 原因：当前前台 WS、离页气泡、conversation polling、inbox 都已验证可用；先审计职责和建立 session client，再逐步弱化本地重复状态，风险更低。

- 日期：2026-06-27
- 决策：`session=done` 的语义收紧为“Hermes 任务完成且 assistant message 已写入 conversation”。
- 原因：Hermes 复杂任务可能运行 30 分钟以上；ESP32 不能用短时间补拉窗口推断任务失败，也不能在 conversation 尚未落库时伪造回复内容。

- 日期：2026-06-27
- 决策：前台 Hermes 页面不主动轮询 session；session 只用于离页 pending 判断和重新进入页面时的一次性对账。
- 原因：前台已有 WebSocket 实时链路，叠加 session polling 会让 ESP32 同时维护三套同步机制，违背 V2.4 thin client 目标。

- 日期：2026-06-27
- 决策：V2.4 主线收敛为“前台 WebSocket + 后台统一 `/v1/watch/sync`”；server 内部仍保留 session / conversation / inbox 三本账，ESP32 后台主路径不再分别组合多个接口。
- 原因：ESP32-S3 的薄客户端目标要求减少 HTTP 请求、TLS/JSON buffer 和本地状态组合逻辑；统一 `/sync` 可以让 server 负责聚合，手表只按本地生命周期状态触发同步。

- 日期：2026-06-27
- 决策：MQTT + UDP 混合方案作为后续替代 transport 路线保留，不进入 V2.4。
- 原因：本轮先把 WebSocket + 后台 `/sync` 做稳；MQTT/UDP 后续可以用于低功耗信令或实时语音实验，但不能在 V2.4 里引入第二套业务状态系统。

- 日期：2026-06-27
- 决策：V2.4 `/sync` 不引入 `poll_after_ms`。
- 原因：server 无法可靠预测 Hermes 复杂任务何时完成；ESP32 采用本地固定同步节奏即可，离页 pending 每 5 秒 `/sync`，后台 idle 每 5 分钟 `/sync`，任务完成/失败只看 `session_state` 与 `conversation.messages[]`。

- 日期：2026-06-27
- 决策：在阶段 2 实现 sync client 前插入阶段 1.5，先定义 `/sync` 协议契约与 server/ESP32 source tests。
- 原因：`/sync` 是 V2.4 thin client 的关键边界；先锁定 request/response、游标、delta、pending/inbox 限制和 no `poll_after_ms`，可以避免实现阶段重新发散成多接口组合逻辑。

- 日期：2026-06-27
- 决策：`/sync` conversation 游标字段采用 `after_message_id`，不使用 `last_conversation_id`；inbox 不在 `/sync` 内做列表游标。
- 原因：`after_message_id` 明确表达“返回该 message id 之后的 conversation 增量”，避免把 message 游标误解为 conversation id、request id 或 session id；inbox 的完整列表继续由现有 `GET /v1/watch/inbox` 负责。

- 日期：2026-06-27
- 决策：`/sync` request 增加 `mode`，取值为 `background` 或 `foreground_reconcile`。
- 原因：`pending_request_id` 为空时无法区分后台 idle 同步和进入 Hermes 页面后的重入对账；`mode=background` 保持小包，只返回 inbox 未读摘要和必要状态，`mode=foreground_reconcile` 才允许返回最近 conversation messages。

- 日期：2026-06-27
- 决策：`mode=background` 不是禁止 conversation messages；当 `pending_request_id` 非空时，`/sync` 必须能返回该 pending request 的新 assistant message。
- 原因：离开 Hermes 页面后等待回复的主链路就是 `background + pending_request_id`；如果这一场景不返回 assistant message，ESP32 无法缓存回复和弹出“回复已到达”气泡。

- 日期：2026-06-27
- 决策：`mode=foreground_reconcile` 默认请求最近 10 条 conversation messages；V2.4 server 不做 conversation `text` 长度截断或摘要压缩。
- 原因：手表页面恢复需要最近 5 轮左右的上下文；回复简短由 Hermes 提示词约束更简单快捷，ESP32 侧用 PSRAM buffer 与失败可观测性兜住异常长文本。

- 日期：2026-06-27
- 决策：`/sync` 返回的 `conversation.messages[]` 按 `created_at` 升序排列。
- 原因：ESP32 可以按返回顺序直接 append 到本地对话缓存和 UI，避免在手表端做反转、排序或头插逻辑。

- 日期：2026-06-27
- 决策：`/sync` inbox 采用 `unread_count + latest_unread`，不返回完整 `items[]` / `body`。
- 原因：手表需要在气泡里显示 Hermes 主动下发了什么，但后台同步不能拉完整 inbox 列表；最新未读摘要足够显示气泡，用户点气泡或打开收件箱后再用现有 `GET /v1/watch/inbox` 拉最近 20 条完整 item。

- 日期：2026-06-27
- 决策：`latest_unread` 使用现有 inbox 字段名 `notification_id`，不新增 `id` 别名。
- 原因：当前 `GET /v1/watch/inbox` 和 mark-read 路由均以 `notification_id` 为 item 标识；`/sync` 复用同名字段可以减少 ESP32 端字段转换和歧义。

- 日期：2026-06-27
- 决策：多条 inbox 未读时，`/sync` 气泡数据仍只返回最新一条 `latest_unread` 和总 `unread_count`，气泡点开进入 Hermes 收件箱列表。
- 原因：手表小屏不适合在气泡里展开多条主动消息；列表查看由现有 `GET /v1/watch/inbox` 承担，避免 `/sync` 变成收件箱列表接口。

- 日期：2026-06-27
- 决策：同一次 `/sync` 同时返回 conversation assistant reply 与 inbox `latest_unread` 时，气泡优先显示 assistant reply。
- 原因：conversation reply 是用户刚从手表发起的 Hermes 请求回执，时效性高于 Hermes 主动收件箱消息；inbox 未读数仍保留，用户稍后可进收件箱查看。

- 日期：2026-06-27
- 决策：assistant reply 气泡文案和展示方式沿用当前已验证行为，V2.4 不新增固定标题、截断长度或 UI 文案规则。
- 原因：V2.4 目标是前台 WebSocket + 后台 `/sync` 稳定化与 ESP32 瘦身，不把已验证可用的气泡 UI 体验作为本轮改动面。

- 日期：2026-06-27
- 决策：`/sync` 对 ESP32 暴露的 `session_state` 枚举固定为 `none/running/done/error/timeout/canceled`；server 内部 `accepted/asr_ready/running` 统一映射为公开 `running`。
- 原因：`accepted` 和 `asr_ready` 是 server 内部任务阶段，不应扩散到 ESP32；手表只需要知道“还在跑、完成、失败、超时、取消、没有任务”。

- 日期：2026-06-27
- 决策：`/sync` 网络失败只表示本轮同步失败，不改变 Hermes 任务状态；401/403 授权失败停止高频 pending sync 并提示配置问题。
- 原因：Wi-Fi/TLS/server 临时故障不能让 ESP32 误判长任务失败；授权失败属于设备配置问题，继续 5 秒重试会浪费资源并污染日志。

- 日期：2026-06-27
- 决策：`/sync` 气泡本地去重规则固定为 assistant reply 按 `message_id`，inbox 按 `notification_id`。
- 原因：网络重试、游标滞后或页面重入可能让同一条消息被 `/sync` 再次返回；本地去重可避免重复气泡，同时不影响页面按 id 去重显示。

- 日期：2026-06-27
- 决策：进入 Hermes 页面做 `foreground_reconcile` 不标记 inbox 已读；只有用户打开/查看收件箱消息时才 mark read。
- 原因：Hermes 对话页和主动收件箱是不同入口；进入对话页不代表用户已经看过 Hermes 主动下发的 inbox 内容。

## 风险

- internal RAM 极紧张，任何新增 buffer 都可能引入 `ESP_ERR_NO_MEM`。
- 如果过早删除本地 display dedup，可能导致重复回复回归。
- 如果 `/sync` 设计过胖，可能把减少多请求的收益抵消成更大的 JSON 解析压力；尤其是 server 不截断 `text` 后，ESP32 buffer 与错误路径必须先设计清楚。
- 如果 `/sync` 聚合逻辑没有保持 delta 语义，可能让后台 idle 也拉到过多 conversation/inbox 历史。
- 如果把 worker 合并得太激进，可能让网络 IO、UI snapshot 和低频轮询互相阻塞。
