---
id: attempt-hermes-external-mem0-hk-migration
tags: context, runs, ai-memory-watch, hermes, mem0, 1panel, hong-kong, migration
summary: 记录 Hermes 外置 Mem0 与 watch 云端栈迁移到香港 1Panel 后的公网、私有端口和回退副本验证。
last_reviewed: 2026-08-02
memory_type: run
scope: project
owners: server/watch_voice_endpoint, server/deploy/openresty
triggers: Hermes Mem0 migration, 1Panel compose, direct HTTPS, watch public gate
evidence_level: observed
---

# Hermes 外置 Mem0 香港迁移验证

## 路线

- 香港使用 `1panel/hermes-agent:2026.7.20` 作为基础镜像，派生镜像固定 `mem0ai==2.0.10`。
- Hermes、watch endpoint、Gateway Relay 使用独立数据目录；没有复制旧 Hermes `state.db` 或本地 memories。
- 1Panel 通过“路径选择”导入 `/opt/1panel/docker/compose/ai-memory-watch/docker-compose.yml`，编排列表显示来源 `1Panel`、服务 `3/3` 运行中。
- `hermes.934000.xyz` 和 `watch.934000.xyz` 使用香港 DNS-only A 记录；OpenResty 负责 HTTPS/WSS，watch 只反代 `/v1/watch/*`。

## 验证

- `hermes memory status`：`Provider: mem0`，插件状态 available。
- 派生镜像内 `mem0ai`：`2.0.10`。
- 香港生产 Compose 显式固定 `MEM0_MODE=platform` 与 `MEM0_USER_ID=hermes-user`；这是旧栈未配置该变量时的实际默认身份，避免后续镜像升级改变记忆分桶。
- 香港生产 Hermes 容器内 `mem0_search` 探针返回 2 条既有结果，仅保留数量与摘要哈希 `8379e288b334c50c`，未记录记忆正文。
- 更新环境配置后执行 `docker compose up -d --no-build`，未重新拉取镜像；Hermes 重建后 healthy 且重启计数为 0。
- runtime/private exposure gate：通过；`/health`、`/v1/models`、`/v1/responses`、`/internal/watch/inbox` 公网均被拒绝。
- HTTP mock Ogg smoke：`done`、`reminder_created`、固定 7 字段。
- WSS smoke：收到 `request_accepted`、`asr_result`、`task_started` 和最终 `conversation_message`。
- 宿主机 `8642`、`8787`、`9119` 仅回环监听，公网 TCP 不可连接。
- 阿里云旧 Hermes/watch/Relay/cloudflared 已停止并取消应用容器自动重启；旧数据目录仍保留，作为离线回退材料。
- 用户取消 24 小时观察要求；香港 `hermes-migration-observe.timer` 已禁用，既有日志未删除。
- 曾暴露过的旧 Cloudflare Tunnel token 已轮换，旧 connector 已停止；新 token 未记录。
- 香港最终复测：三容器 healthy，公网 runtime/private exposure、HTTP mock Ogg smoke、WSS smoke 通过；8642、8787、9119 仅回环监听。
- 用户要求必须由 1Panel「AI -> 智能体」管理 Hermes；已将 `Hermes-Agent` 应用 Compose 替换为生产配置，使用派生镜像、生产 Hermes 数据目录和 `ai-memory-watch`/`watch-relay-private` 外部网络。
- watch/Relay Compose 已移除 Hermes 服务，只保留两个服务；1Panel Hermes 以网络别名 `hermes` 提供 API，避免重复 Hermes 实例。
- 切换后 1Panel 容器 `1Panel-hermes-agent-KHau` healthy，旧 `ai-memory-watch-hermes` 容器删除；公网 HTTP/WSS 与私有暴露门禁再次通过。

## 收口结果

- 本轮迁移已完成即时验收；阿里云备份与旧数据目录保留，未删除任何回退材料。

## 1Panel 网站条目复核

- 用户创建了 1Panel 网站条目 `hermes-dashboard` 与 `watch-endpoint`，统一使用 `934000.xyz`/`*.934000.xyz` 证书；Hermes 仍由「AI -> 智能体」管理。
- 新站点生效后发现旧手工 `ai-memory-watch.conf` 与 1Panel vhost 重复，先备份到 `/opt/ai-memory-watch/backups/panel-site-cutover-20260802T000222+0000/`，再将旧文件改为 `.disabled`，未删除。
- 1Panel 默认 watch 代理会把所有路径转发到 8787；已将 `server/deploy/1panel/watch-endpoint/proxy/root.conf` 同步到 `/opt/1panel/www/sites/watch-endpoint/proxy/root.conf`，只转发 `/v1/watch/*`，并对 `/health`、`/internal/*`、`/v1/models`、`/v1/responses` 和 OTA 管理入口返回 404。
- `docker exec 1Panel-openresty-AVm2 nginx -t` 与 reload 通过。
- `runtime_status.ps1 -BaseUrl https://watch.934000.xyz -SkipDocker -SkipHermesApi -SkipServiceHealth -AssertPrivateNotExposed` 通过，私有路径均未暴露，watch health 为 `ok/online`。
- `smoke_test.ps1 -BaseUrl https://watch.934000.xyz -SkipServiceHealth` 通过，mock Ogg 返回 `done/memory_saved`、固定 7 字段。
- `websocket_smoke_test.ps1 -BaseUrl wss://watch.934000.xyz/v1/watch/ws` 通过，收到 `request_accepted`、`asr_result`、`task_started` 和最终 `conversation_message`。
