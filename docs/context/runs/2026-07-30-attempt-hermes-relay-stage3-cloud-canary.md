---
id: attempt-hermes-relay-stage3-cloud-canary
tags: context, runs, ai-memory-watch, hermes, gateway-relay, cloud-canary, reconnect, cancel
summary: 阿里云私有 Docker 网络完成 Hermes 0.19.0 Relay Connector 阶段 3 canary，验证最终回复归属、长任务、cancel、断线重连和原生 session key。
last_reviewed: 2026-07-30
memory_type: attempt
scope: task
owners: server/watch_relay_connector, server/watch_voice_endpoint, docs/context/plans/active/2026-07-29-ai-memory-watch-hermes-v2.7-gateway-relay-watch-connector-plan.md
triggers: Hermes Relay cloud canary, watch relay reconnect, relay cancel, session key
evidence_level: live
status: completed
---

# Hermes Relay 阶段 3 云端 Canary

## 环境边界

- 云主机：阿里云生产主机；canary 使用独立 Docker network `internal`，不加入生产 `ai-memory-watch` network。
- 临时服务：Hermes `0.19.0`、watch endpoint、Watch Relay Connector；Hermes 使用独立 `/tmp` 数据目录。
- 临时镜像：Hermes image id `sha256:7183e44212d25cc431b5afbec6cbafd409f34afa653fbcfc5b53ee292c2d8e1e`；Connector image id `sha256:16e1be889ab7e152bec6817036d0228db8e3df58bbc5e8bdbe8afd3128442091`。
- 生产 `hermes`、生产 watch endpoint 和 cloudflared 未重启、未改环境、未切换 `WATCH_HERMES_TRANSPORT`。
- 所有 canary 凭据均为临时值，未写入仓库；日志和文档不记录生产 token/key。

## 结果

1. **普通最终回复归属通过**：`canary-relay-003` 经 endpoint WebSocket 提交，Connector 落 spool，Hermes Relay 返回，endpoint 写入一条 user 和一条 assistant conversation；session=`done`、relay=`completed`，Connector delivery=`delivered`，无 inbox 写入。
2. **固定原生 session key 通过**：Hermes `sessions.json` 出现唯一 `agent:main:relay:dm:watch-001`，平台为 `relay`、类型为 `dm`，并保留稳定 `session_id=20260729_175533_99eb2cfd`。多个 canary turn 均进入该 session。
3. **cancel 通过**：`canary-relay-cancel-001` 使用真实 `/v1/watch/request/{request_id}/cancel`，endpoint 返回 `status=canceled`；endpoint 与 Connector 均记录 `canceled`，Hermes 日志出现 `Interrupt detected during retry wait, aborting`，没有 assistant reply。
4. **断线重连/spool replay 通过**：停止临时 Hermes 后提交 `canary-relay-reconnect-001`，Connector 和 endpoint 均保持 `queued`；恢复 Hermes 后 `gateway_connected=true`，spool replay，session=`done`、relay=`completed`、delivery=`delivered`，assistant conversation 成功落库。
5. **长任务归属通过**：`canary-relay-long-001` 使用无副作用 `sleep 15` 提示，约 52 秒后返回；即使处理延迟，endpoint 仍按原 request_id 写入 assistant，Connector turn/inbound/delivery 均完成，没有串到其他 turn。

## 发现并修复

- Connector 的 bind mount 目录若由 root 创建，非 root UID `10002` 无法打开 SQLite；云端部署必须先 `install -d -o 10002 -g 10002`，已沉淀到 `server/watch_relay_connector/README.md`。
- endpoint 的 bind mount 目录同样必须由 UID `10001` 持有；生产已有 `/opt/ai-memory-watch/watch-data`，新增部署应在启动前检查 owner 和可写性。
- 临时 Hermes 早于 Connector 可解析/可用时会记录 DNS/连接失败并退避；正式 Compose 应先让 Connector healthy，再启动带 `GATEWAY_RELAY_URL` 的 Hermes。
- 首次 Docker canary 构建发现旧版 Docker builder 要求多源 `COPY` 目标显式为 `./`，已修复 Connector Dockerfile。

## 结论

服务器侧 live canary gate 已通过，但还没有生产灰度。下一步是补齐正式 secrets/目录初始化与 Compose 启动顺序，然后在停 Direct 新请求、保留回退的前提下做显式 Relay 灰度；ESP32 和 Cloudflare 主路暂不需要修改。
