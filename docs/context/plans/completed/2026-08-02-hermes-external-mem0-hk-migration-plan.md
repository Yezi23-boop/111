---
id: hermes-external-mem0-hk-migration
tags: plan, ai-memory-watch, hermes, mem0, 1panel, hong-kong, migration
summary: 完成 Hermes 外置 Mem0 记忆与 watch 服务迁移到香港 1Panel，并由 AI 智能体页面管理唯一 Hermes 实例。
last_reviewed: 2026-08-02
memory_type: plan
scope: project
status: completed
owners: server/watch_voice_endpoint/compose.hk.yml, server/watch_voice_endpoint/deploy/Dockerfile.hermes.cloud, server/deploy/openresty
triggers: Hermes Mem0 migration, 1Panel, Hong Kong deployment, Hermes upgrade
evidence_level: observed
---

# Hermes 外置 Mem0 香港迁移计划

## 目标

- 镜像负责 Hermes 程序与固定依赖，挂载目录负责 Hermes 本地状态。
- 香港由 1Panel「AI -> 智能体」管理唯一 Hermes，watch voice endpoint 和 Gateway Relay 由独立 Compose 管理。
- Mem0 使用现有 Platform 账户和稳定手表身份，不复制旧 Hermes `state.db` 或本地 memories。
- 公网只通过 `hermes.934000.xyz`、`watch.934000.xyz` 的 80/443 反向代理访问；`8642`、`8787`、`9119` 仅监听回环地址。

## 已完成

- [x] 香港已有 `1panel/hermes-agent:2026.7.20` 基础镜像。
- [x] 香港构建 `ai-memory-watch-hermes:2026.7.20-mem0` 派生镜像，固定 `mem0ai==2.0.10`。
- [x] 香港生产 Compose 显式固定 `MEM0_MODE=platform` 与 `MEM0_USER_ID=hermes-user`，保持旧栈隐式默认身份不变，避免升级后记忆分桶漂移。
- [x] 隔离实例 `hermes memory status` 显示 `Provider: mem0`、插件可用。
- [x] 隔离实例完成 Mem0 写入、明确 `mem0_search` 检索和容器重启后再次检索。
- [x] 香港生产 Hermes 容器内执行无正文输出的 `mem0_search` 探针，返回 2 条既有结果并记录摘要哈希，证明生产身份可读取外置历史。
- [x] 测试记忆已删除，临时容器与临时数据已清理。
- [x] 新增香港专用 `server/watch_voice_endpoint/compose.hk.yml`，不启用 Cloudflare connector，所有服务端口绑定回环地址。
- [x] 将 watch/Relay Compose 与非敏感源码路径变量同步到香港 `/opt/1panel/docker/compose/ai-memory-watch/`；该编排只管理 watch endpoint 与 Relay。
- [x] 将 `server/deploy/1panel/hermes-agent/docker-compose.yml` 同步到 1Panel AI 应用目录；AI -> 智能体的 `Hermes-Agent` 使用派生镜像、生产数据目录和两个私有网络。
- [x] 1Panel AI 页面管理的 `1Panel-hermes-agent-KHau` healthy；旧 `ai-memory-watch-hermes` 容器已删除，阿里云旧栈已停止并取消自动重启。
- [x] `hermes.934000.xyz`、`watch.934000.xyz` 已切换 DNS-only A 记录到香港公网 IP，并由 1Panel OpenResty 终止 HTTPS/WSS。
- [x] 证书已签发并安装自动续期 deploy hook；watch 公网只代理 `/v1/watch/*`，屏蔽 `/health`、`/internal/*` 和 OTA 管理上传页。
- [x] 公网 runtime/private exposure gate、HTTP mock Ogg smoke、WSS smoke 均通过；8642、8787、9119 从公网不可直连。
- [x] 1Panel 网站条目 `hermes-dashboard`、`watch-endpoint` 使用 `934000.xyz` 通配证书；重复的手工 `ai-memory-watch.conf` 已备份并停用，watch 路径门禁由 `server/deploy/1panel/watch-endpoint/proxy/root.conf` 维护。
- [x] 香港曾启用 `hermes-migration-observe.timer` 记录健康/重启计数和本机探针；用户取消 24 小时观察后已禁用 timer，既有日志保留。

## 收口

- [x] 用户取消 24 小时观察要求，以即时公网与私有端口验收作为本轮切换门禁。
- [x] 曾暴露过的旧 Cloudflare Tunnel token 已轮换，旧 connector 已停止；未记录新 token。
- [x] 阿里云旧 Hermes/watch/Relay 数据目录与香港迁移备份均保留，未删除旧数据。

## 进度 (Progress)

2026-08-02：香港迁移已完成首次公网闭环。新 Hermes 使用 `ai-memory-watch-hermes:2026.7.20-mem0`，容器内 `mem0ai==2.0.10`，`hermes memory status` 显示 `Provider: mem0` 且插件可用。1Panel 编排目录中的 Compose 已实际执行 `up -d --no-build`，三个服务保持 healthy。DNS 已改为香港直连，OpenResty 通过 Let's Encrypt 证书提供 Dashboard HTTPS 与 watch HTTPS/WSS；watch 网关只转发 `/v1/watch/*`，不会把容器 `/health` 或 `/internal/*` 暴露出去。香港观察 timer 已启用，首次成功执行时间为 `2026-08-01T21:28:57Z`。
2026-08-02：香港生产 Compose 进一步显式固定 `MEM0_MODE=platform` 与 `MEM0_USER_ID=hermes-user`，重建 Hermes 容器未重新拉取镜像，重建后仍 `healthy`、重启计数为 0。容器内生产 Mem0 检索探针返回 2 条既有结果，仅输出数量与摘要哈希，不输出记忆正文。
2026-08-02：用户取消 24 小时观察要求，完成即时收口。最终香港 Hermes、watch endpoint、Relay 均 healthy；公网 runtime/private exposure、HTTP mock Ogg smoke、WSS smoke 通过，8642/8787/9119 仍仅回环监听。旧 Cloudflare Tunnel token 已轮换，阿里云旧应用与 connector 已停止并取消自动重启；旧数据目录与迁移备份保留，香港观察 timer 已禁用。
2026-08-02：按用户要求将 Hermes 唯一管理入口切换为 1Panel「AI -> 智能体」。AI 应用使用 `ai-memory-watch-hermes:2026.7.20-mem0`、生产 `/opt/data` 与 `ai-memory-watch`/`watch-relay-private` 网络；watch/Relay Compose 移除 Hermes 服务，避免重复实例。切换后 Hermes health、Mem0、HTTP/WSS 与私有暴露门禁再次通过。
2026-08-02：用户创建 1Panel 网站条目后完成站点切换复核。停用重复手工 vhost 并备份原配置，补回 1Panel `watch-endpoint` 的 `/v1/watch/*` 专用代理与私有路径 404 门禁；OpenResty 配置检查、公网 runtime/private exposure、HTTP mock Ogg smoke、WSS smoke 均通过。

## 验证 (Validation)

- 1Panel 容器 -> 编排：`ai-memory-watch` 只管理 watch endpoint 与 Relay，显示 `2/2` 运行中；Hermes 由 AI -> 智能体单独管理。
- 1Panel AI -> 智能体：`Hermes-Agent` 对应容器 `1Panel-hermes-agent-KHau`，镜像为 `ai-memory-watch-hermes:2026.7.20-mem0`，标签 `createdBy=Apps`，状态 healthy。
- watch/Relay Compose：只包含两个服务，均 healthy；Hermes 通过两个 external Docker network 以别名 `hermes` 提供给它们。
- `hermes memory status`：provider `mem0`，插件 available；镜像内 `mem0ai` 版本 `2.0.10`。
- 香港 Hermes 生产容器环境含显式 `MEM0_MODE`、`MEM0_USER_ID`（只验证存在，不输出值）；生产 `mem0_search` 探针返回 `result_count=2`，摘要哈希 `8379e288b334c50c`。
- 1Panel AI Compose 重建 Hermes 未重新下载镜像；`1Panel-hermes-agent-KHau` `RestartCount=0`，8642/9119 仍只回环监听。
- `runtime_status.ps1 -BaseUrl https://watch.934000.xyz -SkipDocker -SkipHermesApi -SkipServiceHealth -AssertPrivateNotExposed`：通过。
- `smoke_test.ps1 -BaseUrl https://watch.934000.xyz -SkipServiceHealth`：mock Ogg `done/reminder_created`，固定 7 字段。
- `websocket_smoke_test.ps1 -BaseUrl wss://watch.934000.xyz/v1/watch/ws`：通过，收到 ASR 与最终 `conversation_message`。
- 公网 TCP `8642`、`8787`、`9119`：不可连接；OpenResty 80/443 正常。
- 旧阿里云：Hermes/watch/Relay/cloudflared 容器已退出，旧数据目录仍存在；旧 connector 与应用容器的自动重启已关闭。
- 阿里云备份 `/opt/ai-memory-watch/backups/pre-migration-20260801T195451Z` 仍存在，包含 Compose、环境配置备份和 Hermes 数据归档；未读取或记录密钥内容。
- `hermes-migration-observe.timer`：已禁用且 inactive，既有观察日志保留，不作为完成门禁。
- 最终复测：`runtime_status.ps1` 通过；HTTP mock Ogg smoke 返回 `done`、固定 7 字段；WSS smoke 收到 ASR 与最终 `conversation_message`；`hermes.934000.xyz` 返回 Dashboard 登录重定向 `302`。
- 1Panel 网站切换复测：`/health`、`/internal/watch/inbox`、`/v1/models`、`/v1/responses` 均返回 404；`watch.934000.xyz/v1/watch/health` 认证后返回 `status=ok`、`hermes_status=online`；HTTP mock Ogg 与 WSS 均通过。

## 下一步 (Next Step)

迁移已收口。后续 Hermes 升级从 1Panel「AI -> 智能体」的配置/Compose 入口替换固定版本派生镜像，并复用 `/opt/ai-memory-watch/hermes-data`；网站规则通过 1Panel 网站条目和 `server/deploy/1panel/watch-endpoint/proxy/root.conf` 维护。如需回退，使用保留的迁移备份，不重新启用旧公网入口。

## 迁移红线

- 不提交或输出任何 API key、device token、Relay secret、Cloudflare token。
- 不让两个 Hermes 实例同时写同一个 `/opt/data`。
- 不使用容器内临时安装作为正式升级方式；升级只替换派生镜像并复用挂载数据目录。
- 不修改 `official_chat` 主线。
