---
id: ai-memory-watch-hermes-v2-6-warm-agent-latency-plan
tags: context, plans, ai-memory-watch, hermes, v2.6, latency, warm-agent, api-server, aliyun, paused
summary: 已完成 Hermes 0.19 延迟基线与回滚取证；/v1/runs warm Agent patch 已暂停，后续由 V2.7 Gateway Relay 原生会话路线评估替代。
last_reviewed: 2026-07-29
memory_type: task
scope: task
owners: docs/context/plans/active/2026-07-29-ai-memory-watch-hermes-v2.6-warm-agent-latency-plan.md, server/watch_voice_endpoint/deploy/Dockerfile.hermes.cloud
triggers: Hermes warm Agent, Hermes API Server latency, watch-001 run latency, /v1/runs, Gateway Relay
evidence_level: design
status: active
superseded_by: docs/context/plans/active/2026-07-29-ai-memory-watch-hermes-v2.7-gateway-relay-watch-connector-plan.md
---

# AI Memory Watch / Hermes V2.6 Warm Agent 延迟优化计划（已暂停）

## 目标

- 保留已完成的 Hermes `0.19.0` 延迟基线、镜像回滚点和公网回归证据。
- 暂停对 `APIServerAdapter /v1/runs` 的自定义 warm Agent patch，不向官方 API Server 注入长期维护的缓存逻辑。
- 后续由 V2.7 的 Gateway Relay + Hermes 原生 `RelayAdapter/GatewayRunner` 路线验证固定 `watch-001` 原生会话与 Agent 缓存。
- 保持现有 Direct API 路线可用，直至 V2.7 通过独立 Connector POC 和公网回归。

## 已知基线

- 升级前 Hermes `0.18.2` 实测：真实 WSS request 的 ASR 约 `0.9 s`，Hermes 段约 `19 s`；同一 `/v1/runs` 的两次最小请求总耗时约 `24.1 s` 与 `21.1 s`。
- 升级前日志显示每个 run 都重新初始化 Agent，首个模型调用前约 `15 s`；模型调用本身约 `3.9-6.5 s`。
- 2026-07-29 已升级 Hermes `0.19.0`；官方 release 已包含 agent cold-start 优化，因此必须先重新量测，不可把旧延迟直接当作 warm Agent 的收益。
- 当前云端 `hermes` 内存约 `411 MiB / 768 MiB`，watch endpoint 约 `66 MiB / 256 MiB`，没有排队压力。

## 边界

- 本计划暂停后不修改 `APIServerAdapter`、`official_chat`、ESP32、watch endpoint HTTP/WS 协议或 Hermes Dashboard。
- 不新增 API Server Agent cache、Redis、队列或数据库；V2.7 的 Connector 独立处理其自己的受控投递 spool。
- 不记录或输出 API key、device token、原始音频或完整用户文本。
- 保留现有 `hermes-agent:cloud` 旧镜像标签和 Direct API 路线，作为 V2.7 前的唯一生产回退。

## 进度

- [x] 基线与回滚标签：完成延迟取证，并保留 `hermes-agent:cloud-preupdate-20260729`。
- [x] Hermes `0.19.0` 升级：官方精确 tag 已部署；公网 HTTP/WSS 回归通过。
- [x] 默认镜像来源：云端构建默认通过可达代理拉取 Hermes 基础镜像，构建参数可覆盖。
- [x] 新版延迟基线：固定隔离 session 的连续 `/v1/runs` 首次完成约 `12.6s`、第二次约 `4.6s`；二者均完成。
- [x] Warm Agent 可行性：`0.19.0` 的 `/v1/runs` 仍在每个 run 直接调用 `_create_agent()`，连续热态仍有约 `8s` 差异。
- [x] 路线调整：暂停最小 API Server patch；Gateway Runner 已有原生 session/Agent 缓存，V2.7 先通过 Gateway Relay 接入 `watch-001`，避免维护 API Server 内部补丁。
- [ ] 本计划不再执行阶段 1/2 的 warm Agent patch；仅保留 Direct API 回退、基线和镜像证据。

## 实施

### 阶段 0：冻结基线与回滚点

- [x] 记录真实链路与最小 `/v1/runs` 的分段延迟，确认重复 Agent 初始化是主要可优化项。
- [x] 在阿里云将当前 `hermes-agent:cloud` 加上带日期的回滚标签，确认 compose 可使用该标签恢复。
- [x] 升级官方 Hermes `v2026.7.20` Docker 基础镜像，运行版本为 `0.19.0`。

## 本轮验证

- Docker Hub 在阿里云连接超时，专属 ACR mirror 对该命名空间返回镜像不存在；最终使用可达代理取得与官方 release 一致的 `v2026.7.20` digest。
- Docker daemon 配置首次因命令转义写入非法 JSON 而拒绝启动；修正后 Docker、Hermes、watch endpoint 和 cloudflared 均恢复，旧镜像标签保留可回退。
- `hermes` 重建后保持 `healthy`，`GET /health` 返回 Hermes `0.19.0`。
- 容器内源码确认 `/v1/runs` 对每个 run 直接执行 `_create_agent()`；隔离 benchmark session 的连续 run 为约 `12.6s -> 4.6s`，未输出请求正文或密钥。
- 公网 runtime gate 通过，watch health 为 `online`，`/health`、`/v1/models`、`/v1/responses`、`/internal/watch/inbox` 均未暴露。
- 公网 HTTP mock Ogg smoke 返回 `done`、固定 7 字段完整；WSS smoke 返回 `request_accepted -> asr_result -> task_started -> conversation_message`，最终 reply 为 `done`。

### 阶段 1：最小 warm Agent 补丁（暂停，不执行）

- 不新增 `hermes_warm_agent.patch`，不修改 `APIServerAdapter`。
- 原因：自定义缓存需要自行处理回调换绑、串行、取消、异常失效和官方升级兼容；GatewayRunner 原生会话路径更符合长期手表入口定位。

### 阶段 2：云端部署与对照验证（暂停，不执行）

- 这些验证迁移至 V2.7：先做 Relay Connector POC，再在 Relay 与 Direct 两条路线间做对照验证。

## 失败与回退

- V2.7 未通过前，继续使用当前 Direct API；不自动切换，也不对同一 `request_id` 双发 Direct 和 Relay。
- 若后续 Relay 路线被证伪，保留本计划的基线证据，可重新评估最小 warm Agent patch；届时必须重新审查官方版本与回调/取消语义。

## 验收

- [x] 已保留升级前后 Direct API 延迟基线、镜像回滚点和公网 HTTP/WSS smoke 证据。
- [x] 已明确停止本计划的 API Server patch，避免在官方内部对象上长期维护自定义 Agent 缓存。
- [ ] V2.7 验证 Hermes 原生 Relay 会话确有可接受的延迟和可靠性后，再决定是否永久废弃本 patch 路线。

## 下一步

1. 执行 V2.7：先建立只在 Docker 内网可达的 Watch Relay Connector，验证 Hermes 原生 `RelayAdapter` 握手与单条文本往返。
2. 保持 `watch-001` 的 Direct API 路线为唯一生产路径，直到 Relay 的去重、取消、断线补偿和公网 smoke 完整通过。
