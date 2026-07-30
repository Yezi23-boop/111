---
id: attempt-hermes-relay-stage4-production-canary
tags: context, runs, ai-memory-watch, hermes, gateway-relay, production-canary, rollback
summary: 阿里云正式 Compose 完成 Hermes Relay Connector 部署、watch-001 显式灰度和 Direct 回退演练；云端 canary 通过，ESP32 真机回归因 COM3 不存在待补。
last_reviewed: 2026-07-30
memory_type: attempt
scope: task
owners: server/watch_relay_connector, server/watch_voice_endpoint, docs/context/plans/active/2026-07-29-ai-memory-watch-hermes-v2.7-gateway-relay-watch-connector-plan.md
triggers: Hermes Relay production canary, watch relay rollback, HTTP relay compatibility, COM3 unavailable
evidence_level: live
status: completed
---

# Hermes Relay 阶段 4 正式云端 Canary

## 环境与安全边界

- 目标主机为阿里云生产主机；Relay Connector、watch endpoint、Hermes 通过独立 `watch-relay-private` Docker network 通讯，网络 `internal=true`。
- Connector 没有宿主机端口映射，仅在容器内监听 `9080/tcp`；Relay spool 使用持久 `/opt/ai-memory-watch/relay-data`，云端 secrets 未写入仓库、日志或本文档。
- `watch.934000.xyz`、Cloudflare Tunnel、Hermes `8642/9119` 公网路线未被 Relay Connector 接管；公网仍只进入 watch endpoint。

## 正式 canary 结果

1. **部署健康**：Connector health 为 `gateway_connected=true`、`spool_configured=true`；Hermes、endpoint、Connector 均 healthy。
2. **session key 与最终归属**：正式 Relay request 的 endpoint session 固化 `transport=relay`、`relay_session_key=agent:main:relay:dm:watch-001`、`relay_state=completed`；Connector turn 为 `completed`、delivery 为 `delivered`，conversation 每个 canary request 各有 1 条 user 与 1 条 assistant。
3. **cancel**：正式 Relay request `watch-001-relay-cancel-1976110574` 经公网 cancel API 返回 `status=canceled/action=no_action`；endpoint 与 Connector 都是 `canceled`，该 request 没有 assistant 消息。
4. **重连与 spool replay**：停止 Hermes 后提交 `watch-001-relay-reconnect-991116394`，Connector health 变为 `gateway_connected=false`，turn 保持 `queued`；恢复 Hermes 后自动 replay，endpoint/Connector 最终完成并仍使用同一 session key。
5. **HTTP/WSS**：Relay 模式下公网 HTTP mock Ogg 与 WSS smoke 均通过，固定 7 字段和 assistant conversation message 均正常；私有 `/health`、`/v1/models`、`/v1/responses`、`/internal/watch/inbox` 仍未暴露。
6. **Direct 回退**：停止新请求后切 Direct，HTTP/WSS/runtime/private gate 均通过；随后恢复 `WATCH_HERMES_TRANSPORT=relay`，endpoint、Hermes、Connector health 与 Relay smoke 再次通过。Direct/Relay 镜像与 secrets 备份保留。
7. **同设备并发边界**：并发启动两个 `watch-001` WSS smoke 时，一个 request 正常完成，另一个被 Connector 以 `409 relay_turn_conflict` 拒绝；这是计划中单设备严格串行约束的预期结果，不作为多设备并发能力验收。串行 WSS smoke 随后通过。

## 本轮发现与修复

- Relay task 提交给 Connector 后会自然结束，但最终回复由 Connector outbound callback 异步写入 endpoint SQLite。旧 HTTP compatibility waiter 把 task 完成误当成 session 完成，导致 Relay HTTP 过早返回 `server_timeout`。
- `server/watch_voice_endpoint/app.py` 现在对已持久 Relay inbound 改为等待 SQLite terminal state，不重新提交入站、不创建第二个 Hermes task；新增测试后 endpoint 聚焦测试为 `20 passed`，语法检查和 diff check 通过。

## ESP32 证据边界

- `COM3` 不存在，改用当前枚举出的 `COM7` 运行 `agent_serial_monitor.ps1 -Action monitor -DurationSeconds 60`；没有刷写或修改 ESP32 固件。
- COM7 日志证明当前固件成功启动、加载 `watch-001` Kconfig endpoint，`memory_watch`、`mw_upload`、`mw_health`、`mw_cancel`、`mw_conv`、`mw_inbox` 任务均存在，60 秒内 `panic_log_seen=0`、无 Guru/stack overflow。
- 后续 COM7 回归已联网成功：出现 `WIFI_READY -> SERVICE_READY`、`watch endpoint health result: hermes_online=1`、`inbox poll ok items=0 unread=0`；证据为 `board_logs/2026-07-30-02-46-43-hermes-relay-final-esp32-com7.log`。
- 随后 COM7 真实交互证据通过：request `watch-001-aa8d8d84-0001` 前台完成；request `...-0002` 离页后出现 `conversation: sync ok messages=1 session=running`，随后 `session=done terminal=1`、`watch request result status=done` 和 `reply bubble shown`；request `...-0004/0005` 也完成。云端对应 Relay turn 均为 `completed`，无 active session。
- 同一窗口中 request `...-0003` 在本地录音 cleanup 报 `ESP_ERR_INVALID_STATE`，`discard=1` 且没有建立服务器请求；下一次录音成功，未出现 panic/Guru/stack overflow。该问题属于 ESP32 本地录音边界，单独记录为后续优化项，不改变本轮云端 Relay 结论。交互证据：`board_logs/2026-07-30-02-49-23-hermes-relay-final-esp32-interaction.log`。

## 结论

云端私有网络的真实 Hermes Relay session key、cancel、断线重连、回复归属、HTTP/WSS 回归和 Direct 回退已通过，`watch-001` 已显式灰度到 Relay；ESP32 联网启动、health、inbox、真实 WSS 上传、离页 `/sync` 和气泡也已通过。阶段 4 完成；剩余 `ESP_ERR_INVALID_STATE` 是不阻断 Relay 的本地录音边界优化项。
