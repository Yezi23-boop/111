---
id: current-cloud-deployment-route
tags: project, deployment, cloud, ai-memory-watch, hermes, hong-kong, production, current-route
summary: 当前 AI Memory Watch / Hermes 云端生产部署路由：watch 与 Hermes 入口运行在香港 1Panel / OpenResty，旧阿里云栈只作为历史迁移背景。
last_reviewed: 2026-08-02
memory_type: semantic
scope: project
status: active
owners: server/watch_voice_endpoint/compose.hk.yml, server/deploy/openresty, docs/context/knowledge/project/current-cloud-deployment-route.md
triggers: 当前服务器, 当前部署, 生产入口, 现网, watch.934000.xyz, hermes.934000.xyz, 香港 1Panel, OpenResty, 阿里云旧栈
evidence_level: observed
---

# 当前云端部署路由

- 当前生产入口：`https://watch.934000.xyz` 和 `https://hermes.934000.xyz`。
- 当前云端主路：香港 1Panel / OpenResty 终止 HTTPS/WSS，`watch.934000.xyz` 代理 watch endpoint，`hermes.934000.xyz` 代理 Hermes。
- 当前服务编排：watch endpoint 与 Gateway Relay 由 `server/watch_voice_endpoint/compose.hk.yml` 管理；Hermes 由 1Panel「AI -> 智能体」管理唯一实例。
- 旧路线：阿里云旧栈和 2026-07-13 香港中转路线只作为历史证据，不再作为当前生产部署事实。
- 证据来源：`docs/context/plans/completed/2026-08-02-hermes-external-mem0-hk-migration-plan.md` 与 `docs/context/CHANGELOG.md` 的 2026-08-02 迁移收口记录。
