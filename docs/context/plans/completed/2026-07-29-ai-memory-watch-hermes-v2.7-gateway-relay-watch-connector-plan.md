---
id: ai-memory-watch-hermes-v2-7-gateway-relay-watch-connector-plan
tags: context, plans, ai-memory-watch, hermes, v2.7, gateway-relay, relay-adapter, watch-connector, websocket, session, reliability
summary: 以独立 Watch Relay Connector 将 endpoint 接入 Hermes Gateway Relay 的原生 RelayAdapter/GatewayRunner，会话与 Agent 缓存交由 Hermes，设备协议与真相状态继续由 endpoint 持有。
last_reviewed: 2026-07-30
memory_type: task
scope: task
owners: docs/context/plans/active/2026-07-29-ai-memory-watch-hermes-v2.7-gateway-relay-watch-connector-plan.md, server/watch_voice_endpoint, server/watch_relay_connector, server/watch_voice_endpoint/compose.cloud.yml
triggers: AI Memory Watch Gateway Relay, Watch Relay Connector, Hermes RelayAdapter, watch-001 native session
evidence_level: implementation
status: archived
---

# AI Memory Watch / Hermes V2.7 Gateway Relay Watch Connector 执行计划

## 目标

- 将 `watch-001` 从 endpoint 直接调用 Hermes `/v1/runs` 的方式，迁移为 `watch endpoint -> Watch Relay Connector -> Hermes Gateway Relay -> 内置 RelayAdapter/GatewayRunner`。
- 让 Hermes 将固定 `chat_id=watch-001` 作为新的原生长期会话，复用 GatewayRunner 的会话和 Agent 缓存；不迁移切换前的 Hermes 内部历史。
- 保持 ESP32 的录音、设备鉴权、WebSocket、`/sync`、固定 7 字段 JSON、conversation/inbox UI 协议完全不变。
- 保留 Direct API 路线为显式回退。V2.7 未完成验收前，不切换生产手表流量，也不自动双发。

## 现状与决策

- Hermes `0.19.0` 的 `/v1/runs` 仍按每次 run 创建 Agent；Direct API 隔离基线约为首次 `12.6s`、第二次 `4.6s`。
- Hermes 已内置实验性的通用 `RelayAdapter`。配置 `GATEWAY_RELAY_URL` 后，Hermes 主动拨号 Connector 的 `/relay` WebSocket；Connector 提供 `CapabilityDescriptor` 并在同一连接上收发标准 `MessageEvent` 与出站 action。
- 不在 Hermes 内另写或 patch 一份 `WatchAdapter`。本计划中的“手表适配”位于独立的 `Watch Relay Connector`；Hermes 使用官方内置 `RelayAdapter`。
- 官方公开的是 Relay 合同与 Gateway 一侧代码，未发现可直接部署的公开 Connector 成品。V2.7 只实现服务单设备的最小兼容 Connector，不复制官方的多租户、Redis、平台 socket、配对或管理面。
- 切换时 Hermes 原生会话从新消息开始；endpoint SQLite 中已有的手表 UI 历史保留，但不把旧原文或摘要注入新的 Hermes 会话。
- 产品语义固定为“用户对话回复进入 conversation，Hermes 主动提醒进入 inbox”。当前 Hermes 最终 `send` 实测缺失稳定 `reply_to`，因此由 Connector 持久化单设备串行 turn envelope；有且仅有一个未完成 turn 时，缺失 `reply_to` 的最终 `send` 才能按该 envelope 回传 endpoint。没有 active turn 或存在歧义时拒绝归属，绝不自动写入 inbox。

## 架构与 Owner

```text
ESP32
  -> watch endpoint
     - device token、Ogg Opus、MiMo ASR、watch_session、conversation、inbox、WS、/sync
  -> Watch Relay Connector
     - Relay 协议、WS upgrade 鉴权、短期可靠投递 spool、outbound_result、interrupt bridge
  <-> Hermes Gateway Relay
     - 内置 RelayAdapter、GatewayRunner、原生 session/Agent cache、skills、任务执行
```

| Owner | 负责 | 不负责 |
| --- | --- | --- |
| watch endpoint | 设备协议、ASR、`watch_session` 终态裁决、conversation/inbox 持久化、WS 与 `/sync` 补偿 | Hermes Agent 生命周期、Relay WS 协议实现 |
| Watch Relay Connector | Hermes Relay handshake、帧验证、临时可靠投递、`inbound_ack`、`outbound_result`、interrupt 转发 | 设备 token、音频、ASR、任务状态、对话归属或历史裁决 |
| Hermes Gateway | 原生 `MessageEvent`、session、Agent 缓存、skills、执行与原生 cancel | ESP32 协议、SQLite UI 缓存、Cloudflare 暴露 |

## 范围与非目标

- 本轮做：单一设备 `watch-001`、单一 Hermes profile `main`、DM 语义、纯文本入站与最终短文本出站、取消、断线重连、去重和公网回归。
- 本轮不做：ESP32 直连 Relay、音频直接进 Hermes、流式 token 到手表、多设备、多 profile、多租户、Redis、媒体上传、原生按钮/approval、MQTT/UDP、Connector 公网暴露。
- 首版 Connector 不记录原始音频，不输出 token、ASR 原文或完整对话正文到日志；为了可靠重投允许在受限 SQLite spool 中短期保存最小 Relay envelope，确认语义经阶段 0 验证后才清理。
- Relay 首版只承接“可证明归属”的 conversation reply。现有主动 inbox 生产者继续使用既有内部 inbox 写入口；未来仅在 Hermes/Connector 提供受信任的显式 `watch_delivery=inbox` 元数据后，才将无 `reply_to` 的 Relay action 接入 inbox。

## 固定契约

### Relay 会话

- `source.platform=relay`、`source.chat_type=dm`、`source.chat_id=watch-001`、`source.user_id=watch-001-owner`、`source.profile=main`。
- 该组合预计在当前 Hermes `0.19.0` 产生确定的 Gateway session key；它是阶段 0 待证明的实现契约，不是稳定公开 API。Connector 不把字符串裸写死，只有在固定镜像 digest 的 source/集成测试与真实 wire trace 一致后，才允许生成或持久化该 key。
- 对同一 `watch-001` 严格串行；Endpoint 的 `(device_id, request_id)` 是端到端幂等键，Relay 不得将同一 ID 的不同文本视为新任务。

### 入站与出站

- endpoint 在 ASR 成功后先创建 `watch_session`、持久化 user conversation，再提交 Relay。用户消息已显示后，不等待 Hermes 最终回复。
- endpoint 将 Relay transport、inbound message ID、投递状态和 session key 来源持久化到自己的真相源；Connector spool 只是可重启的传输副本，不能替代 endpoint recovery。
- Connector 仅在把入站 envelope 写入自身短期 spool 后向 endpoint 确认接收。`inbound_ack` 是否可作为删除条件、live frame 是否携带 ack，以及重启后的重放顺序，全部由阶段 0 的固定镜像 wire trace 决定；未验证前不得声称 exactly-once。
- Hermes `send` action 到达 Connector 后，Connector 以独立内部凭据调用 endpoint 私有 route。endpoint 完成 SQLite 事务并持久化 outbound delivery 去重键后，Connector 才返回 `outbound_result(success=true)`。
- 有 `reply_to` 时，只有它能关联到 endpoint 的持久 message link 才写 conversation；明确给出但不匹配时直接拒绝。仅在缺失 `reply_to` 时，才允许 Connector SQLite 中唯一未完成的 `watch-001` turn envelope 关联。没有 active turn、存在多个 active turn 或 turn 已完成时，未知 action 拒绝写入并返回结构化失败，不能靠“最后一个请求”的易失内存映射猜测归属。
- 阶段 0 要验证 GatewayRunner 对正常最终回复的字段、迟到 action、重复 action 和 delivery ID 语义。当前 native `reply_to` 不足时，已改由阶段 0-c 的持久单设备 envelope 作为受信归属；若 envelope 的唯一 active turn、终态和重启规则无法证明，仍停止 endpoint 集成。
- 初版 descriptor 声明无 draft streaming、无 edit、无 thread；不将模型 token 碎片写入 conversation。手表继续使用已有“思考中”状态，最终只显示一条确定回复。

### 取消与终态

- 手表仍调用 `POST /v1/watch/request/{request_id}/cancel`；endpoint 继续是取消与完成的唯一终态裁决者。
- Relay cancel 需持久化 `none -> requested -> dispatching -> dispatched/retryable -> terminal` 的 transport 子状态；连接断开、Connector 重启或 gateway 无响应时保持可恢复的 `requested/retryable`，不得伪造 `canceled`。
- 未送入 Gateway 的入站消息：在 endpoint 事务确认 Connector 尚未投递后，删除/失效该 outbox 项并裁决 `canceled`。已投递的消息：Connector 仅在阶段 0 已证明 session key 与 interrupt 确认语义后发送 `interrupt_inbound`；否则 Relay 不具备上线条件。
- `done` 与 `canceled` 竞态沿用 V2.5：先被 endpoint SQLite 成功持久化的终态胜出；迟到的出站 reply 只做幂等检查，不能覆盖既有终态。不得把 WS 断开、Relay 重连或 timeout 直接翻译成 `canceled`。
- `cancel-before-claim` 不是 Relay 特例：endpoint 未成功创建 session 前不确认取消；创建后的一切 cancel request、dispatch 和 reconcile 都必须持久化，避免仅靠进程内记忆。

### 内网与安全

- 新增独立 `watch-relay-private` Docker network，设置 `internal: true`；只有 Hermes、watch endpoint 与 `watch-relay-connector` 加入。Connector 不加入现有共享 `ai-memory-watch` external network，不映射宿主机端口、不配置 Cloudflare hostname、不暴露 Dashboard、Hermes API 或 Relay 管理面。
- Hermes 使用 `GATEWAY_RELAY_URL`、`GATEWAY_RELAY_ID`、`GATEWAY_RELAY_SECRET` 连接 Connector；endpoint 与 Connector 使用独立内部服务凭据。所有值只存在云端 secrets/env，不进入仓库、日志、计划或固件。
- Connector 只允许预期 gateway ID、`watch-001` 和支持的 frame type；未知 frame、未知 chat 或无效认证必须拒绝，不将细节回显给设备。

## 进度

- [x] 架构决策：采用 Gateway Relay + 独立 Watch Relay Connector；不 patch Hermes API Server warm Agent。
- [x] 会话决策：Relay 切换后创建干净的 Hermes 原生会话，不迁移旧历史；手表 UI 历史仍保留 endpoint SQLite。
- [x] 回复产品语义：用户对话回复进入 conversation，Hermes 主动提醒进入 inbox；由于 Hermes 当前最终 `send` 不稳定携带 `reply_to`，归属改由 Connector 持久 turn envelope + 单设备串行约束证明。
- [x] 边界审查：确认 endpoint SQLite 继续是 request/conversation/inbox 的真相源，WS 断线由 `/sync` 补偿，Relay 不裁决终态。
- [x] 审查修订：将 transport/recovery、reply 归属、cancel、网络隔离和 Direct/Relay 切换列为阻断契约，不允许以易失 Connector 状态代替 endpoint SQLite。
- [x] 阶段 0-a：从固定 Hermes `0.19.0` 复核 Relay 配置、HMAC upgrade、newline-delimited JSON handshake，并完成隔离 Connector harness 测试。
- [x] 阶段 0-b：在阿里云临时 Docker network 中用无端口、临时 `/opt/data` 的 Hermes canary 完成 `hello -> descriptor -> inbound -> typing/send -> outbound_result`；生产 Direct 容器未重启。
- [x] 阶段 0-c（归属方案已落地）：Hermes 原生 `reply_to` 缺失时，由 Connector 持久化单设备串行 turn envelope，以 endpoint `request_id`/inbound message link 作为受信归属；无 active turn、多个 active turn 或已完成 turn 的主动 `send` 拒绝自动归入 conversation。
- [x] 阶段 1：实现 Connector SQLite spool、turn/delivery 关联、内部 ingress/egress、重启后未完成入站重放和 endpoint Relay transport adapter；Direct 默认路线保持不变。
- [x] 阶段 2：接入 endpoint 的 Relay transport adapter，Relay session 持久化 transport 与 relay 状态，保持 Direct 路线可选。
- [x] 阶段 3：完成真实 Hermes `0.19.0` cancel/session key/重连/长任务归属 canary；本地 cancel、outbound conversation 落库和重复回调回归已通过，live 证据见 `docs/context/runs/2026-07-30-attempt-hermes-relay-stage3-cloud-canary.md`。
- [x] 阶段 4：Docker 内网部署、显式灰度、HTTP/WSS/ESP32 回归与回退演练完成；云端 Relay、HTTP/WSS、Direct 回退和 ESP32 真机按键/离页补偿均有证据，保留一次可恢复的本地录音 `ESP_ERR_INVALID_STATE` 边界告警。

## 实施

### 阶段 0：协议冻结与可行性 POC

- 精读固定 Hermes `0.19.0` 的 `gateway/relay/`，以官方 `relay-connector-contract.md` 的 `contract_version=1` 为唯一 wire 依据；记录当前镜像 digest。
- 实现只记录结构与 ID、绝不记录用户正文的 Connector test harness，抓取并断言 authenticated handshake、`hello`、descriptor、`inbound`、ack、`outbound`、`outbound_result` 与 `interrupt_inbound` 的固定镜像 wire trace。
- 验证 `GATEWAY_RELAY_URL` 启动后 Hermes 会注册 `Platform.RELAY` 并主动连接内部 Connector；Connector 返回 descriptor 后才接收入站。
- 用无副作用文本探针验证 stable source 实际落入预期 Gateway session，验证 session key 的来源和可重复构造性，并验证当前版本的 `interrupt_inbound(session_key, chat_id)` 能只取消该会话。
- 验证普通最终 `send` 是否携带触发消息的 `reply_to`、多条 send 的 delivery ID 语义、live/buffered inbound 的 ack 行为。这些是 conversation/inbox 分类、spool 清理和 cancel 上线前的阻断 gate。
- 未通过前不写 endpoint 集成代码；必要时停止并继续保留 Direct API。

### 阶段 0 当前验证结论

- 本地 `server/watch_relay_connector/tests`：当前 `11 passed`；覆盖 upgrade 鉴权、descriptor、合成 inbound/outbound、trace 脱敏、内部 ingress 鉴权/幂等、取消 retryable、显式错误 reply link 拒绝和未知 outbound 拒绝。
- 远端 POC 镜像已用阿里镜像构建，镜像 digest 为 `sha256:44fc6d08062a2cceef9d2b11bef55c635c0db8804844be0d3f81d1aa0798bf6d`；临时 Connector 以 64 MiB、只读、无特权运行，峰值约 37 MiB。
- Hermes canary 以 512 MiB 上限运行，峰值约 343 MiB；真实 trace 证明 newline framing 修复后 Relay handshake 和一次无副作用文本的出站 action 可工作。
- 当前阻断证据已转化为设计约束：最终 `op=send` trace 未出现 `reply_to`，因此不能依赖 Hermes 原生字段；Connector 现在必须先持久化 inbound envelope，并只在唯一 active turn 内做缺省归属。没有该约束时，任何“写 conversation/inbox”实现都会有错投风险。
- 源码复查确认这不是 harness 丢字段：Hermes `RelayAdapter.send()` 的 `reply_to` 是可选参数；当前 Gateway 对普通 `platform=relay` 会话没有稳定把触发消息 ID 注入最终 `send`，该字段主要出现在少数平台的线程/进度路径中。
- 生产 `hermes`、watch endpoint、cloudflared 在 canary 结束后均保持 healthy；POC 容器和临时网络已清理。详细证据见 `docs/context/runs/2026-07-30-attempt-hermes-relay-stage0-wire-trace.md`。
- 当前实现增量：`server/watch_relay_connector/relay_spool.py` 持久化 turn、inbound frame 和 delivery；`server/watch_voice_endpoint/relay_transport.py` 提供私有 endpoint adapter；内部 outbound 回调完成 endpoint conversation 幂等落库并推送当前 WS；Connector 完成 delivery 后不再重放入站，网关断线取消保持 `retryable`，不伪造 `canceled`。
- 阶段 1-3 验证：watch endpoint 全量 `179 passed`，Relay Connector 全量 `11 passed`，相关 Python `py_compile` 与 `git diff --check` 通过；阶段 4 追加 HTTP waiter 修复后 endpoint 全量为 `180 passed`。云端 compose 私有网络、Connector volume、healthcheck 和 endpoint 镜像已部署，生产 `watch-001` 已完成显式 Relay 灰度；阶段 4 的 ESP32 真机证据仍待 COM3 恢复。

### 阶段 0.5：Relay 持久化与切换契约

| 持久化项 | 真相 owner | 创建时机 | 重启恢复规则 |
| --- | --- | --- | --- |
| `transport=direct|relay` | endpoint `watch_session` | session claim 同一事务 | 不可随全局配置反向改写；只按该值分派恢复 |
| `relay_inbound_id`、`relay_state` | endpoint transport/outbox 表 | Relay session 创建后 | 以同一 request ID 向 Connector 查询/重投；绝不调用 `/v1/runs` |
| `relay_session_key` 与来源版本 | endpoint transport 表 | 阶段 0 证明后 | 仅用于同版本 interrupt/reconcile；版本不匹配时阻断切流 |
| Connector spool frame/ack | Connector 私有 SQLite | Connector 接收内网提交时 | 仅作为传输副本；按 endpoint ID 重放，确认语义由阶段 0 trace 固化 |
| `relay_delivery_id`、message link | endpoint outbound/link 表 | Connector 请求出站时 | 事务去重；无可信 link 的 action 拒绝写入 |
| `relay_cancel_state` | endpoint transport 表 | cancel request 到达时 | 继续 dispatch/reconcile，连接故障不自动变终态 |

- endpoint recovery 必须按 session 中固化的 `transport` 分派：仅 Direct session 可复用当前 `hermes_run_id` 与 `/v1/runs` 恢复；Relay session 只能通过 Connector/Relay reconcile。
- `relay_state`、delivery 与 cancel 的实际枚举值以阶段 0 trace 为准；阶段 1 前把该表落实为 schema migration、repository API 与 source tests，不得只放在内存结构中。
- 切换和回退按 session，而不是只读进程级 `WATCH_HERMES_TRANSPORT`：配置只决定尚未 claim 的新请求，既有 session 永远使用其创建时的 transport。

### 阶段 1：最小 Watch Relay Connector

- 新增 `server/watch_relay_connector/`，使用与现有 endpoint 一致的 Python 服务栈；包含 protocol、WS auth、短期 SQLite spool、internal ingress/egress client、health 与测试。
- 实现并严格测试 `hello`、`descriptor`、`inbound`、`inbound_ack`、`outbound`、`outbound_result`、`interrupt_inbound`；不实现未用的 media、prompt、thread、管理面或多租户。
- descriptor 以 `platform=relay`、`label=AI Memory Watch`、DM/plain-text 能力启动；最终消息可通过 `send` 返回。Connector 对不支持的 action 返回结构化失败，不伪造成功。
- outbound `send` 必须保留 `chat_id`、`content`、`reply_to` 和稳定 delivery ID；同一 delivery 重投时由 endpoint 幂等落库。
- Connector spool 的清理、ack、重投顺序以阶段 0 trace 为准；endpoint transport/outbox 表保留跨 Connector 重启所需的持久关联。日志仅记录 request/delivery ID、状态和耗时，不记录正文。

### 阶段 2：endpoint Relay transport adapter

- 在 `server/watch_voice_endpoint` 新增可选 transport owner，使用私密配置 `WATCH_HERMES_TRANSPORT=direct|relay`；默认仍为 `direct`，且配置只决定新 session 的持久 `transport` 值。
- Relay 模式下，ASR 后仍先走现有 session claim 与 user conversation 持久化，再提交 Connector；即时 WS 返回 accepted/思考状态，最终回复不再由同一 `/v1/runs` coroutine 等待。
- 新增 endpoint 内部 Relay ingress/egress route，使用独立 service credential。它们不得匹配 Cloudflare 的 `/v1/watch/*` 公网路径，且必须拒绝缺失内部凭据的请求。
- 为 Relay 入站/outbound/cancel 建立 endpoint transport/outbox/link/去重记录，保证“SQLite 落库成功”与“Connector 返回 outbound success”一致；不共享 Connector SQLite，也不让 Connector 直接读写 endpoint 数据库。
- 修改 endpoint 重启恢复：Direct 路线的 HTTP/WSS 行为、`hermes_run_id` 处理和现有 `/v1/runs` cancel 路径保持原样；Relay session 不伪装成 Direct run ID，也不得被 Direct worker 自动接管。

### 阶段 3：可靠性与取消

- 将 endpoint cancel 分为未投递、已投递待 interrupt、interrupt retryable、已完成四种持久 transport 状态；同一 `request_id` 的 HTTP/WSS 重投、Relay 重投都命中既有 `watch_session`。
- 为 Relay session key 的构造加入固定 Hermes 版本 source test 与 live integration test；升级 Hermes 时，先跑该 gate 再更换生产镜像。
- 覆盖：Connector 重启、Hermes 重启、Relay WS 断开、入站重复、outbound_result 丢失、WS 离页、`/sync` 重入、cancel-before-delivery、cancel-vs-done、迟到 reply。
- 保持 V2.5 的规则：WS 只是实时投递；断线不取消任务，终态/对话由 `/sync`、重连 snapshot 或 session replay 补偿。

### 阶段 4：部署与显式灰度

- 在 `compose.cloud.yml` 增加 Connector 服务、私有 volume、healthcheck、最小资源限制和 `watch-relay-private: internal: true` 网络；Connector 不映射端口到宿主机，也不加入共享 external network。
- 将 Relay ID、Relay secret、endpoint-connector 内部凭据写入云端 secrets，不提交 `.env`。
- 部署后先运行纯文本 Connector POC；再运行本机/公网 runtime gate、HTTP mock Ogg smoke、WSS smoke 与 private exposure gate。
- 只有全部通过后，先停止新请求 admission，等待既有 Direct session 全部终态，再手动让后续 `watch-001` session 使用 `relay`；Direct API 镜像、配置和验证脚本保留。
- 不做自动 fallback：回退时先停止新请求 admission，等待/人工处置在途 Relay session 后才让后续 session 使用 `direct`。既有 session 的 `transport` 不可改写，避免同一语音任务在两条 Hermes 路线重复执行。

#### 阶段 4 当前执行结果（2026-07-30）

- [x] 阿里云正式 Compose 已启动 `watch-relay-connector`；`watch-relay-private` 为 `internal=true`，Connector 仅暴露容器内 `9080/tcp`，无宿主机端口；spool 使用持久 `/opt/ai-memory-watch/relay-data`，UID `10002` 可写。
- [x] 正式 endpoint 完成 Relay schema migration，初始保持 Direct；通过 runtime/private exposure、HTTP mock Ogg 与 WSS smoke 后，才将新 `watch-001` session 显式切为 `relay`。
- [x] 正式 Relay canary 通过：session key、普通 HTTP/WSS 最终回复、真实 cancel、Hermes 停止期间 queued spool、恢复后的 replay、单 request 的 user/assistant 归属均有云端 SQLite 证据。
- [x] 发现并修复 Relay HTTP 兼容路径过早返回 `server_timeout`：Relay task 提交后不代表终态，HTTP 现在等待 endpoint SQLite terminal state；聚焦 endpoint 测试 `20 passed`。
- [x] 执行 Direct -> Relay 回退演练：Direct 期间 HTTP/WSS/runtime/private gate 通过，随后恢复 Relay；最终生产配置为 `WATCH_HERMES_TRANSPORT=relay`，Hermes/endpoint/Connector 均 healthy。
- [x] ESP32 联网启动回归：COM3 不存在，改用 COM7 采集 60 秒证据；当前固件加载 `watch-001` Kconfig endpoint，出现 `WIFI_READY -> SERVICE_READY`、`watch endpoint health result: hermes_online=1`、`inbox poll ok items=0 unread=0`，无 panic/Guru/栈溢出。证据：`board_logs/2026-07-30-02-46-43-hermes-relay-final-esp32-com7.log`。
- [x] ESP32 交互回归：COM7 真实按键录音至少 3 次上传并通过 Relay；前台 request `...-0001` 完成，离页 request `...-0002` 先通过 `/sync` 观察 `running`，随后观察 `done` 并显示 `reply bubble shown`，后续 `...-0004/0005` 也完成；全程无 panic/Guru/stack overflow。一次 `...-0003` 在本地录音清理阶段报 `ESP_ERR_INVALID_STATE`、未发服务器请求，下一次录音恢复成功，已记录为待后续独立收敛的录音边界问题。

## 验证与验收

- Connector 协议测试：固定镜像 digest 的 auth failure、descriptor、完整 wire frame、inbound/ack、outbound/result、interrupt、重连队列、去重、日志脱敏全部通过。
- endpoint pytest：Direct 路线不回退；Relay 路线的持久 transport/recovery、session/conversation/inbox/cancel `/sync` 契约通过。
- Hermes integration：单条无副作用中文文本由 Relay 往返成功；`watch-001` 后续消息复用同一 Gateway session；cancel 只影响该 session。
- 公网门禁：
  ```powershell
  .\server\watch_voice_endpoint\runtime_status.ps1 -BaseUrl "https://watch.934000.xyz" -SkipDocker -SkipHermesApi -SkipServiceHealth -AssertPrivateNotExposed
  .\server\watch_voice_endpoint\smoke_test.ps1 -BaseUrl "https://watch.934000.xyz" -SkipServiceHealth
  .\server\watch_voice_endpoint\websocket_smoke_test.ps1 -BaseUrl "wss://watch.934000.xyz/v1/watch/ws"
  ```
- 真机：按住说后用户文本立即显示；复杂任务中可离开页面；最终回复只显示一次；离页由气泡和 `/sync` 补回；主动 Hermes 消息只进入 inbox。
- 网络隔离：Connector 公网和 Cloudflare hostname 均返回不可达；宿主机无 Connector 监听；不在 `watch-relay-private` allowlist 中的容器无法连接 Connector route。
- 回退演练：Direct -> Relay 和 Relay -> Direct 两个方向均验证在途 session 不双发、不误恢复；未终态的旧 transport session 必须 drain 或由人工明确处置。
- `idf.py build` 仅在 ESP32 代码实际变更时运行；本计划服务器阶段不以不必要的板端重编译代替服务验证。

## 失败与回退

- Relay handshake、session key、cancel、reply 归属、ack 语义或去重任一项无法证明正确：停止在 POC，不切换 `watch-001`，继续 Direct API。
- Relay 运行时出现重复执行、回复归类错误、终态覆盖、公共暴露或 Connector 内存异常：将 `WATCH_HERMES_TRANSPORT` 显式切回 `direct`，保留 endpoint SQLite 数据，不删除 Connector 证据。
- Hermes Relay 合同或 Docker 镜像升级：固定当前版本，先在非生产 Connector POC 验证 `contract_version`、session key、cancel 与 smoke，再升级生产。

## 下一步

1. 后续独立处理本地录音 `ESP_ERR_INVALID_STATE` 边界；它不阻断本轮已经完成的 Relay/离页补偿验收。
2. 保持 Direct 镜像、secrets 备份和回退命令可用；后续新 session 继续按显式 transport 灰度，既有 session 按创建时 transport 恢复。
