---
id: attempt-hermes-relay-v29-connection-reuse
tags: context, runs, ai-memory-watch, hermes, gateway-relay, connection-reuse, serial-admission, cloud-canary
summary: V2.9 云端验证 Relay 单连接复用、watch-001 串行拒绝、固定七字段冲突响应和公网回归；期间修复云端镜像漏打包 run_events.py 与 session_repo.py 版本漂移。
last_reviewed: 2026-07-30
memory_type: attempt
scope: task
owners: server/watch_relay_connector, server/watch_voice_endpoint, docs/context/plans/active/2026-07-30-ai-memory-watch-hermes-v2.9-relay-reuse-serial-admission-plan.md
triggers: Relay connection reuse, relay_session_busy, retryable active turn, cloud deploy packaging
evidence_level: live
status: completed
---

# Hermes Relay V2.9 云端 Canary

## 变更范围

- Connector 保持一条 Hermes Relay WebSocket；新增私有连接/握手/断开/replay/活动 turn 指标。
- SQLite active chat 门禁包含 `queued`、`sent`、`awaiting_reply`、`retryable`；`retryable` 不释放 `watch-001`。
- Connector 的 409 冲突由 watch endpoint 转换为固定七字段 `relay_session_busy` 错误响应；不自动切 Direct。
- 未修改 ESP32、`official_chat`、公网路由或任何 secret 文件。

## 云端证据

- 阿里云 `hermes`、`ai-memory-watch-relay-connector`、`ai-memory-watch-voice-endpoint` 和 `cloudflared` 均 healthy。
- Connector 私有 status 在 canary 后为：`connected=true`、`handshake_count=1`、`disconnect_count=0`、`pending_inbounds=0`、无活动 turn；连续 turn 没有产生第二次握手。
- 两个连续 `watch-001` 文本请求均完成；并发第二请求返回固定 `relay_session_busy`，日志显示 Connector 返回 409，未创建第二个 Hermes inbound。
- 本地 retryable 状态门禁测试确认未确认状态不会接受新 turn；不为生产演练停止 Hermes，避免留下不可自动收敛的活动任务。
- 公网 runtime/private exposure gate 通过；HTTP mock smoke 为 `done/reminder_created`、7 字段；WSS smoke `passed`。

## 部署问题与修复

- 首次云端重建暴露两个版本漂移问题：watch endpoint 镜像未复制 `run_events.py`，云端 `session_repo.py` 也缺少当前代码需要的 `set_progress`/`progress_phase`。
- 已将 `Dockerfile` 纳入 `run_events.py`，并同步当前 `session_repo.py` 后重建 watch endpoint；Hermes 未重启，Relay 单连接保持稳定。
- 修复前产生的 `hermes_run_error` 只存在于旧 canary 记录；修复后的新 request 均按预期完成或得到串行拒绝。

## 结论

V2.9 连接复用与单设备串行 admission 已通过云端和公网门禁，可以保持 Relay 作为生产主路。后续只需在变更 Relay/spool 时重复现有测试与 smoke；不需要 ESP32 固件改动。
