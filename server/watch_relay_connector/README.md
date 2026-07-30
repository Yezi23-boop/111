# Watch Relay Connector

这是 AI Memory Watch 与 Hermes Gateway Relay 之间的私有 Connector。它只加入
`watch-relay-private` Docker network，不映射宿主机端口，也不接受 Cloudflare
公网请求。

## 云端数据目录

Connector 以 UID/GID `10002` 的非 root 用户运行。使用 bind mount 前必须先在云主机
创建并授权 spool 目录，否则 SQLite 无法创建 `/data/relay.db`：

```bash
install -d -o 10002 -g 10002 -m 0750 /opt/ai-memory-watch/relay-data
```

`GATEWAY_RELAY_ID`、`GATEWAY_RELAY_SECRET`、`WATCH_RELAY_INTERNAL_TOKEN` 和
`WATCH_RELAY_ENDPOINT_TOKEN` 只写入云主机私有 env 文件，不进入仓库、日志或固件。

## 启动顺序

先启动 Connector 并确认 `/health` 的 `gateway_connected` 可以工作，再启动或重启
配置了 `GATEWAY_RELAY_URL` 的 Hermes。Hermes 会主动拨号 Connector；Connector
重连后会从 SQLite spool 重放未完成 inbound。生产切换前必须完成 session key、cancel、
重连和长任务 canary，`WATCH_HERMES_TRANSPORT` 默认保持 `direct`。

## 连接复用与串行门禁

- Hermes Gateway 与 Connector 之间复用服务器侧单条 Relay WebSocket；每个 turn
  只提交一帧 inbound，不重新建立 Hermes 连接。
- `watch-001` 同时只允许一个活动 turn。`queued`、`sent`、`awaiting_reply` 和
  `retryable` 都会占用活动槽位；第二个 turn 返回 `relay_turn_conflict`，不排队、
  不自动切 Direct。
- Relay WebSocket 断开时，Connector 仍可把 turn 写入 SQLite spool；Hermes 恢复
  连接后重放。`retryable` 是尚未确认的状态，不会被错误地当成空闲会话。
- 连接计数、最近帧时间、重连次数和当前 turn 只通过带内部 token 的
  `/internal/relay/status` 查看；不通过 Cloudflare 公网暴露。
