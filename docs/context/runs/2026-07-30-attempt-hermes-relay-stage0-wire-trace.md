---
id: attempt-hermes-relay-stage0-wire-trace
tags: context, runs, ai-memory-watch, hermes, gateway-relay, watch-relay-connector, wire-trace
summary: 记录 Hermes 0.19.0 Gateway Relay 隔离 canary 的握手、出站和回复归属验证；握手通过但最终 send 缺少可信 reply_to，阻断 endpoint 接入。
last_reviewed: 2026-07-30
memory_type: run
scope: project
owners: server/watch_relay_connector, docs/context/plans/active/2026-07-29-ai-memory-watch-hermes-v2.7-gateway-relay-watch-connector-plan.md
triggers: Hermes Relay handshake timeout, reply_to missing, Gateway Relay wire trace, Watch Relay Connector POC
evidence_level: observed
---

# Hermes Gateway Relay 阶段 0 Wire Trace

## 范围

- 使用当前 Hermes `0.19.0` 镜像，在阿里云临时 Docker internal network 中启动隔离 canary。
- canary 使用临时 `/opt/data`，不映射宿主机端口，不接入 watch endpoint、ESP32、Cloudflare 或生产 Hermes 数据。
- Relay Connector 以 64 MiB、只读、丢弃 capabilities 运行；临时 Hermes 以 512 MiB 上限运行。

## 证据

- 本地 Connector tests：`5 passed`。
- Connector 镜像构建成功，镜像 digest 为 `sha256:44fc6d08062a2cceef9d2b11bef55c635c0db8804844be0d3f81d1aa0798bf6d`。
- 第一次 canary 暴露 harness 使用 `send_json()` 而 Hermes RelayTransport 要求 newline-delimited JSON；改为 `JSON + "\\n"` 后，真实 handshake 稳定通过。
- 最终 trace 包含：`hello`、`descriptor`、`inbound`、多次 `typing`、`send` 和对应 `outbound_result`；没有再次出现 30 秒 relay connect timeout。
- canary 峰值约为 Connector `37 MiB`、Hermes `343 MiB`；生产三个容器在结束后仍 healthy，临时容器和网络已清理。
- 源码复查：`RelayAdapter.send()` 接收可选 `reply_to`，但当前 Gateway 对普通 `platform=relay` 的最终回复没有稳定注入触发消息 ID；`reply_to` 只在少数平台线程/进度路径中显式传递。因此缺失字段不是 Connector JSON 序列化问题。

## 阻断结论

- 当前最终 `op=send` 没有携带 `reply_to`，也没有可直接映射到 endpoint `request_id` 的可信 delivery link。
- 因此不能把 Relay 出站消息安全写入 `watch_conversation` 或 inbox；不能用进程内 map、最后一个 pending request 或文本内容猜测归属。
- V2.7 暂停在阶段 0-c。Direct API 仍是生产主路，未修改生产 Hermes 配置。

## 下一步

- 固定 Hermes 版本继续查明最终 reply 的可信关联字段，以及 session key、interrupt、buffered inbound ack 的实际语义。
- 若原生 Relay 不提供 `reply_to`，先定义受信的 Connector envelope 或显式 `watch_delivery` 元数据，再进入 endpoint transport adapter；不以当前 trace 直接上线。
