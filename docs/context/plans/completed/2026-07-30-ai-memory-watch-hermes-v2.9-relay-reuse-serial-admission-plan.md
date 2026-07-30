---
id: ai-memory-watch-hermes-v2-9-relay-reuse-serial-admission
tags: context, plans, ai-memory-watch, hermes, v2.9, gateway-relay, websocket, spool, serial
title: AI Memory Watch / Hermes V2.9 Relay 连接复用与单设备串行任务
summary: 巩固 Hermes Relay 服务器侧长连接复用、watch-001 单设备串行门禁、断线 spool 和固定七字段冲突响应。
memory_type: task
scope: repo
owners: server/watch_relay_connector, server/watch_voice_endpoint
evidence_level: implementation
status: archived
last_reviewed: 2026-07-30
triggers: Hermes Relay connection reuse, watch-001 serial turn, relay spool retryable, relay_session_busy
---

# AI Memory Watch / Hermes V2.9 Relay 连接复用与单设备串行任务

## 目标

- Hermes Gateway 与 Watch Relay Connector 复用服务器侧单条 WebSocket，不为每个 turn 重连。
- `watch-001` 同时只允许一个活动 Relay turn；第二个请求立即拒绝，不排队、不自动切 Direct。
- Relay 断线但 Connector 可用时仍持久化 spool，恢复后重放；ESP32 页面生命周期不影响服务器任务。

## 进度

- [x] 保留 Connector 单条 Relay WebSocket 和 endpoint 内部 HTTP turn 提交边界。
- [x] 将 `retryable` 纳入 SQLite active chat partial index 和活动查询，避免未确认取消后并发。
- [x] 为私有 `/internal/relay/status` 增加连接、握手、断开、重放和当前 turn 摘要。
- [x] Relay 冲突映射为固定七字段 `relay_session_busy` watch 错误响应。
- [x] 增加 Connector 连接复用、retryable 门禁、私有 status 和 Endpoint busy 回归测试。
- [x] 完成阿里云生产 Relay 连续 turn、并发拒绝和断线 spool replay canary。

## 验证

- Connector tests: `14 passed`。
- Watch endpoint Relay tests: `4 passed`。
- Watch endpoint 全量 tests: `189 passed, 1 warning`。
- `git diff --check`: 通过；仅有仓库既有 CRLF 提示。
- 阿里云容器已重建并保持 healthy；Relay 私有 status：`connected=true`、`handshake_count=1`、`disconnect_count=0`、`replay_count=0`、`pending_inbounds=0`。
- 云端 canary：连续两个 `watch-001` Relay turn 均完成；并发第二个 turn 返回 `relay_session_busy`，没有进入 Hermes；本地 retryable 门禁测试通过。
- 断线 spool replay 沿用 V2.7 阶段 4 的真实阿里云 live evidence；本轮不停止生产 Hermes 制造新的未确认活动任务，避免破坏当前生产会话。
- 公网 runtime/private exposure gate 通过；HTTP mock smoke 为 `done/reminder_created`、固定七字段；WSS smoke `passed`。
- context standard validation：0 errors / 0 warnings；`git diff --check` 通过。

## 约束

- 不修改 ESP32 固件、`official_chat` 或公网路由。
- 不把 Hermes API key、Relay secret、device token 写入仓库、日志或文档。
- 不为每台设备建立独立 Relay WebSocket，不恢复自定义 warm Agent，不自动 Relay/Direct 双路竞速。

## 下一步

1. 保持 Connector、watch endpoint、Hermes 的健康检查和私有 status 监控。
2. 后续若修改 Relay admission 或 spool，必须重新运行 Connector/endpoint 测试和公网 smoke；不要恢复 Direct 自动回退。
