---
id: attempt-hermes-v25-conversation-reliability
tags: context, runs, ai-memory-watch, hermes, v2.5, websocket, session, sqlite, utf8, inbox-auth, aliyun
summary: 记录 AI Memory Watch V2.5 对话可靠性实现、Hermes 原生异步 run、SQLite 幂等恢复、ESP32 pending/UTF-8 修复和阿里云公网验证证据。
last_reviewed: 2026-07-14
memory_type: run
scope: project
owners: server/watch_voice_endpoint, main/services/memory_watch_service.c, docs/context/plans/active/2026-07-14-ai-memory-watch-hermes-v2.5-conversation-reliability-plan.md
triggers: Hermes V2.5, conversation reliability, hermes run recovery, watch session interrupted, internal inbox auth, websocket metrics
evidence_level: observed
---

# AI Memory Watch / Hermes V2.5 对话可靠性执行记录

## 目标与边界

- 正式手表对话继续使用前台 WSS + 后台 `/sync`，不修改 `official_chat`，不引入 MQTT/UDP、Redis 或 Celery。
- watch endpoint 持有 session/conversation/inbox 真相；ESP32 只负责输入、短缓存、状态展示和补拉。
- 所有密钥只写入仓库外环境文件；验证输出不包含 Hermes、MiMo、Cloudflare、device 或 internal token。

## Hermes 能力与恢复边界

- Hermes `0.18.2` 已验证原生 `POST /v1/runs`、`GET /v1/runs/{run_id}`、events 与 stop API。
- 无副作用探针实际返回 `202`，10 次轮询后进入 `completed` 并提供 output。
- Hermes run 状态只在 Hermes 进程内保存，terminal TTL 为 3600 秒；Hermes 重启后 run 可能 404。
- watch session 持久化 `hermes_run_id`。重启后有 run ID 时重新挂接；没有可恢复 ID 时标记 `interrupted`，禁止自动重放可能已经执行过工具副作用的任务。

## 数据与并发修复

- 云端上线前使用 SQLite online backup 保存到：
  - `/opt/ai-memory-watch/watch-data/backups/20260714-v25-stage0`
  - `/opt/ai-memory-watch/watch-data/backups/20260714-v25-predeploy-012048`
- `watch_session` additive migration 增加 `hermes_run_id` 与 `error_code`。
- conversation 增加 `(device_id, request_id, role)` 唯一索引与 `add_message_once()`；云端迁移后重复组为 0。
- `create_or_get()` 原子 claim、terminal replay、cancel/completion 单终态和同一 device Hermes run 串行均有 pytest 覆盖。
- `/v1/runs` 轮询复用进程级 `httpx.AsyncClient`，避免每秒重新建立连接。

## ESP32 修复

- 前台 WS 已收到 ASR 后达到本地等待期限时，转为 `conversation_pending` 并由后台 `/sync` 继续补拉，不再伪造 `watch_timeout`。
- 固定保留现有 128-byte service/UI buffer；server reply 限制为 120 UTF-8 bytes，ASR device text 限制为 255 UTF-8 bytes。
- `memory_watch_service_copy_text()` 在 UTF-8 codepoint 边界截断，未扩大 internal RAM，也未增加新的 task/queue/event group。
- ESP-IDF 5.5.3 `idf.py build` 通过；`111.bin` 为 `0xac5db0`，最小 app partition 余量 `0x33a250`，约 23%。

## Inbox 与公网边界

- 新增 `POST /internal/watch/inbox`，只接受独立 `WATCH_INTERNAL_API_KEY`。
- 公网 device token 默认不能创建 Inbox；原 `POST /v1/watch/inbox` 默认 404，GET/list/read 保留。
- Cloudflare 公网 `/health`、`/v1/models`、`/v1/responses`、`/internal/watch/inbox` 均为 404；private exposure gate 通过。

## 云端部署与观测证据

- 部署前镜像回退标签：`ai-memory-watch-voice-endpoint:pre-v2.5-20260714`。
- 新镜像部署到阿里云后容器 healthy，Hermes 与 cloudflared 保持运行。
- Dockerfile 的 Debian APT 与 Python pip 均切到阿里镜像；默认 PyPI 构建曾等待近 4 分钟，切换后依赖安装约 19 秒。
- 公网 HTTP mock Ogg smoke：`status=done`、固定 7 字段。
- 公网 WSS smoke：ASR、task_started、assistant conversation message 全链路通过。
- HTTP compatibility 默认也使用同一 SQLite session + `/v1/runs`；云端 smoke 后最新 session 为 `done` 且 run ID 存在。HTTP 与 WS 同 request 的并发测试证明只启动一次 Hermes run。
- 一次云端 WS 指标：upload `351 ms`、ASR `0 ms`（mock）、queue `0 ms`、Hermes `16031 ms`、persist `2 ms`、delivery `ws`。
- 云端 migration 后 `active_sessions=0`、conversation duplicate groups `0`，最近部署日志无 ERROR/Traceback/Exception。

## 失败路线与证伪

- PowerShell `export.ps1` 在非交互 shell 的父 PID 识别失败；改用同一 ESP-IDF 目录的 `export.bat` 在单个 `cmd` 子进程激活后构建成功。该失败不属于固件代码。
- 远端 Bash 命令直接嵌入 PowerShell 双引号会被本地提前解析；后续多行远端命令统一通过 stdin 传给 `bash -s`。
- watch endpoint 默认 PyPI 下载过慢；取消的只是 BuildKit 构建任务，旧容器始终 healthy，没有公网中断。

## 尚未闭环

- Windows Docker Desktop 当前未运行，为避免本地同名 cloudflared connector 与云端同时上线，本轮没有启动本地 Compose；本地以完整 pytest 代替容器 release gate。
- HTTP `/v1/watch/voice-command` 与 `/text-command` 已默认加入与 WSS 相同的 session claim；`WATCH_HTTP_ASYNC_RUNS_ENABLED=false` 只保留为紧急回退到旧 `/v1/responses` 的开关。
- 本轮无法执行 app-flash 和真机长任务/取消/重进页面回归：Windows 当前只枚举到系统 `COM1`，ESP32 的 `COM3` 不在线。设备重新连接后应使用仓库串口脚本限时验证。
