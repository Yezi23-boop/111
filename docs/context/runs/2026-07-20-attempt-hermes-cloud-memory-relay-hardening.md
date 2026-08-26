---
id: attempt-hermes-cloud-memory-relay-hardening
tags: context, runs, ai-memory-watch, hermes, aliyun, memory-pressure, autossh, cloudflare, watchdog
summary: 记录阿里云 2C2G 主机内存压力导致 Hermes/watch/SSH 同时失去响应，以及容器资源隔离和香港 relay 自愈防护的部署验证。
last_reviewed: 2026-07-29
memory_type: run
scope: project
owners: server/watch_voice_endpoint, docs/context/runs
triggers: Hermes memory pressure, SSH banner timeout, watch 502, autossh backoff, relay watchdog
evidence_level: observed
---

# Hermes 云端内存与 Relay 自愈加固

## 故障签名

- `watch.934000.xyz` 与 `hermes.934000.xyz` 同时超时或返回 `502`，而香港 cloudflared 仍为 active。
- 香港 `watch-relay-autossh` 进程可能仍显示 running，但 `127.0.0.1:18787/19119` 不再传输数据，或在阿里云重启后因 autossh 退避数分钟才重连。
- 阿里云端 TCP 22 可以建立连接，但 SSH 停在 `Connection timed out during banner exchange`，无法进入主机执行 Docker 命令。
- 上一启动周期从 2026-07-19 16:56 到重启前持续出现 `systemd-journald: Under memory pressure, flushing caches`；没有磁盘满、kernel panic 或明确 OOM kill 证据。

## 结论

- watch endpoint 没有单独崩溃；2 GiB 规格主机进入持续内存压力后，Hermes、watch、sshd 和 SSH 转发一起失去响应。
- 两个业务容器原先都没有 Docker memory/PID 上限。故障前最后一次 sysstat 样本可用内存约 390 MiB，随后日志与服务响应明显延迟。
- 历史数据没有进程级内存采样，因此不能把瞬时峰值百分之百归因于某个进程；Hermes 是当前最大业务进程，watch endpoint 常态占用明显更低。
- 阿里云恢复后 autossh 的内部退避仍可造成数分钟公网 `502`，需要独立健康守护主动重建转发。

## 已部署防护

- Hermes：`mem_limit=768m`、`memswap_limit=1024m`、`pids_limit=256`。
- watch endpoint：`mem_limit=256m`、`memswap_limit=384m`、`pids_limit=128`。
- 两个容器都限制 json-file 日志为 `10m x 3`；Hermes 新增 `/health` Docker healthcheck，watch 启动等待 Hermes healthy。
- 阿里云旧 cloudflared Compose 服务改为显式 `aliyun-connector` profile，默认部署不会与香港 connector 重复上线。
- 阿里云 `watch-cloud-healthcheck.timer` 每 30 秒采集主机可用内存和两容器内存；单容器连续 3 次健康失败后只重启该容器。
- 香港 autossh 增加 10 秒连接超时、单次连接、15 秒 keepalive 和两个本地转发。
- 香港 `watch-relay-healthcheck.timer` 每 30 秒检查 listener 与两个 upstream；两个 upstream 同时失败或 listener 缺失时立即重启 relay。

## 验证

- 云端 Compose 真实解析通过，Hermes/watch 容器重建后均为 healthy；inspect 显示 memory、swap、PID 限制已生效。
- 阿里云内存快照定时器持续触发，样本显示 Hermes 约 236-327 MiB、watch endpoint 约 37-41 MiB、主机可用内存约 812-891 MiB。
- 受控停止香港 relay 后，healthcheck 成功自动重启；`18787/19119` 恢复监听，upstream 返回 `200/302`。
- 公网 runtime gate 通过：watch health online，Hermes/internal 私有路径保持未暴露。
- 公网 WSS smoke 通过：`request_accepted -> asr_result -> task_started -> conversation_message`。
- contract tests：`14 passed`。server 全量首次为 `175 passed / 1` 个异步时序抖动；失败用例隔离重跑通过，不涉及本轮 Compose/守护脚本。

## 后续观察

- 连续观察至少 24 小时的 `watch-cloud-healthcheck` 内存快照和容器重启计数。
- 如果 Hermes 经常触及 768 MiB 并被容器 OOM，优先升级到 4 GiB，而不是继续放宽限制把风险转回整机。
- 当前守护只做恢复与记录，不自动重启阿里云实例，也不重放可能有外部副作用的 Hermes 任务。

## 2026-07-29 追加：主路切到阿里云 connector

- 现象：手表和公网脚本访问 `watch.934000.xyz` 返回 `502`，但阿里云 `hermes` 与 `ai-memory-watch-voice-endpoint` 均为 healthy，`127.0.0.1:8787/8642` 本机健康。
- 证据：香港 connector `ct-23` 仍连接，Cloudflare ingress 指向香港 relay 兼容端口 `127.0.0.1:18787/19119`；香港 SSH `22022` 在握手阶段关闭连接，无法远程停止 relay。阿里云保存的旧 Tunnel token 首次启动时报 `Unauthorized: Invalid tunnel secret`，换新 connector token 后注册成功。
- 修复：阿里云启用 `aliyun-connector` profile，并为当前 Cloudflare ingress 增加本机兼容端口 `127.0.0.1:18787 -> watch endpoint:8787`、`127.0.0.1:19119 -> Hermes Dashboard:9119`；用户在 Cloudflare 删除香港 `ct-23` connector 后，公网不再分流到坏 relay。
- 验证：公网 `runtime_status.ps1 -BaseUrl "https://watch.934000.xyz" -SkipDocker -SkipHermesApi -SkipServiceHealth -AssertPrivateNotExposed` 通过，watch health online，私有 `/health`、`/v1/models`、`/v1/responses`、`/internal/watch/inbox` 均未暴露。裸 `/v1/watch/health?device_id=watch-001` 返回 `401` 是缺 device token 的预期鉴权结果。
- 后续修复：用户提供的新 MiMo `sk-` 接入密钥属于按量付费 API，不能继续搭配旧 Token Plan 地址。仅更新 `/opt/ai-memory-watch/secrets/hermes.env` 后，容器环境已显示 `XIAOMI_BASE_URL=https://api.xiaomimimo.com/v1`，但 Hermes 日志仍调用 `https://token-plan-cn.xiaomimimo.com/v1`；定位到 `/opt/ai-memory-watch/hermes-data/.env` 仍持久保存旧 `tp-` key 与旧 base URL，并会影响 Hermes 实际 provider 调用。同步更新 Hermes secret 与数据目录 `.env`、重启 Hermes 后，`/v1/responses` 返回 completed 中文回复。
- 最终验证：公网 `runtime_status.ps1 -BaseUrl "https://watch.934000.xyz" -SkipDocker -SkipHermesApi -SkipServiceHealth -AssertPrivateNotExposed` 通过；公网 `smoke_test.ps1 -BaseUrl "https://watch.934000.xyz" -SkipServiceHealth` 返回 `voice_status=done/action=reminder_created/field_count=7`；公网 `websocket_smoke_test.ps1 -BaseUrl "wss://watch.934000.xyz/v1/watch/ws"` 通过，`reply_status=done`。第一次 WSS 复验曾读到旧 replay 帧导致脚本期望顺序失败，重跑后通过，未再出现 MiMo 401。
