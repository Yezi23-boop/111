---
id: attempt-hermes-v25-review-fixes
tags: context, runs, ai-memory-watch, hermes, v2.5, websocket, readiness, sqlite, docker-hardening, aliyun
summary: 记录 V2.5 审查后对回复 replay、Hermes run 重试、WS 重连、ESP32 accepted 确认、私有路由门禁、readiness 和容器硬化的修复与公网验证。
last_reviewed: 2026-07-14
memory_type: run
scope: project
owners: server/watch_voice_endpoint, main/services/memory_watch_service.c, main/services/memory_watch_ws_client.cc, docs/context/plans/active/2026-07-14-ai-memory-watch-hermes-v2.5-conversation-reliability-plan.md
triggers: Hermes V2.5 review fixes, request_accepted, session replay, runtime readiness, private exposure POST, non-root watch endpoint
evidence_level: observed
status: active
---

# AI Memory Watch / Hermes V2.5 审查修复记录

## 问题签名

- terminal session 的 conversation message 被淘汰时，`/sync` 可能只返回终态而没有回复文本。
- Hermes run poll 的瞬时网络错误会被终态化；run 启动响应丢失时存在“上游可能已接收、server 未拿到 run_id”的未知态。
- 后台任务持有旧 WS connection，设备重连后完成消息可能继续投向旧 socket。
- ESP32 在 server 尚未确认接收时断线，可能错误进入长期 pending 并持续得到 `session=none`。
- 公网私有路径门禁用 GET 探测内部 POST 路由，`405` 可能被误判为安全。
- runtime readiness 未统一覆盖 migration、配置校验、active run 恢复和业务入口。

## 修复与边界

- `/sync` 对 terminal session 缺失 assistant message 时使用 session 的 `reply_text/message_id` replay；客户端已确认该 message_id 后不重复发送。
- Hermes run poll 对 request error、408、429 和 5xx 在原 deadline 内退避重试；`run_started_at` 持久化，容器重启不重置一小时预算。
- Hermes `0.18.2` 源码探针确认 `/v1/runs` 尚未消费 `Idempotency-Key`。watch endpoint 仍发送稳定 `request_id` 头作未来兼容，但启动响应一旦不确定就不自动重试，session 进入 `interrupted`；只有无副作用的 GET poll 才退避重试。
- 当前 V2.5 强制 `session_id == request_id`，避免同一 request 创建第二条 session。
- server 按 device 动态选择当前 authenticated WS connection；旧连接关闭时不会移除较新的 connection。
- WS 在 session claim 成功后先发送 `request_accepted`。ESP32 使用已有 FreeRTOS EventGroup 增加 accepted bit，只有看到 accepted 或 ASR 后，离页、断线或本地期限结束才进入后台 pending；accepted-only deadline 也不得伪造本地 timeout。
- `clarification_id` 与 `hermes_run_id/run_started_at` 一并持久化，重启恢复保持追问语义。
- 私有 gate 对 `/internal/watch/inbox` 使用无凭据 POST，只允许 404/410；401/403/405 和网络不可验证均失败。
- runtime 初始化失败可重试；HTTP watch/internal 路由统一返回 503，WS 使用 1013 关闭。
- 公网 Inbox create 兼容入口删除；device token 只保留 list/read 权限。

## 部署与回退证据

- 云端部署前对 `session.db/conversation.db/inbox.db` 执行 SQLite online backup 与 integrity check；备份位于 `/opt/ai-memory-watch/watch-data/backups/20260714-025142-v25-review-fixes`。
- 旧镜像回退标签：`ai-memory-watch-voice-endpoint:pre-review-fixes-20260714`。
- watch endpoint 基础镜像固定 digest，pip 恢复 TLS 校验；运行用户为 `10001:10001`，root filesystem read-only，capabilities 全部删除并启用 no-new-privileges。
- 数据目录已调整为 UID/GID 10001；回退旧 root 镜像仍可读取。若回退到不兼容 schema，必须停服务后恢复上述数据库备份并重新执行 integrity check。

## 验证

- server pytest：`174 passed`；仅有既有 Pydantic `dict()` deprecated warning。
- ESP32 Hermes source tests：`55 passed`。
- ESP-IDF 5.5.3 build：通过；`111.bin` 为 `0xac5e70` bytes，最小 app partition 余量 `0x33a190` bytes（23%）。
- 云端容器：healthy，user `10001:10001`，read-only rootfs，`CapDrop=[ALL]`。
- 云端 SQLite：`user_version=2`、duplicate request group `0`、active session `0`、integrity `ok`。
- 公网 runtime gate：watch health online；`/health`、`/v1/models`、`/v1/responses` 和 POST `/internal/watch/inbox` 均未暴露。
- 公网 WSS mock Ogg：通过，顺序为 `request_accepted -> asr_result -> task_started -> conversation_message`，总脚本耗时约 9.5 秒。
- Windows Docker Desktop 未运行；本地容器回归仍待后续补证据。ESP32 真机本轮未刷写，保留原阶段 7 验收项。
