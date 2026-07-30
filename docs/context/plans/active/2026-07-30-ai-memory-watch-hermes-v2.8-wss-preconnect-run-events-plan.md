---
id: ai-memory-watch-hermes-v2-8-wss-preconnect-run-events-plan
tags: context, plans, ai-memory-watch, hermes, v2.8, websocket, sse, run-events, progress
title: AI Memory Watch / Hermes V2.8 WSS 预连接与 Run Events 进度反馈
summary: 让 Hermes 页面提前建立前台 WSS，并把 Direct Hermes Run Events 归一化为安全的手表进度阶段。
memory_type: task
scope: task
owners: docs/context/plans/active/2026-07-30-ai-memory-watch-hermes-v2.8-wss-preconnect-run-events-plan.md, server/watch_voice_endpoint/app.py, server/watch_voice_endpoint/run_events.py, server/watch_voice_endpoint/session_repo.py, main/services/memory_watch/memory_watch_service.c, main/services/memory_watch/memory_watch_ws_client.cc, main/ui/custom/memory_watch_controller.c
evidence_level: design
status: active
last_reviewed: 2026-07-30
triggers: ai-memory-watch, hermes, wss, run-events, sse, task-progress
---

# V2.8 WSS 预连接与 Run Events 进度反馈

## 目标

进入 Hermes 页面后由 `memory_watch_service` owner task 预连接并认证 `/v1/watch/ws`，页面停留期间复用连接上传录音；离开页面只关闭前台传输，不取消已经被服务器接收的 session。WSS 失败时，只有在尚未发送音频的明确边界内回退现有 HTTP multipart 路径。

服务器 Direct 路径可选订阅 Hermes `/v1/runs/{run_id}/events` SSE，将官方事件压缩为 `recognized`、`searching`、`executing`、`composing` 四种阶段。手表只接收 `request_id` 与 `phase`，不接收原始 SSE、tool 名称或 token delta。Relay 没有可验证进度帧时只保持 `task_started`/`executing` 与最终回复，不伪造搜索阶段。

## 约束

- `WATCH_RUN_EVENTS_ENABLED=false` 默认关闭，关闭时仍使用现有 Hermes run 状态轮询。
- `/sync` 继续是离页后的最终状态真相源；不返回原始 SSE。
- Hermes API key 只在服务器内部使用，不进入 WSS、固件、日志或文档。
- 不修改 `official_chat`，不新增 Hermes API 公网暴露，不改变 V1 HTTP 固定七字段接口。
- `task_progress` 可以被合并或丢弃；ASR、最终回复和错误事件必须通过 service-owned queue 交付，不能引用请求栈上的长期 callback context。

## 进度

- [x] 新增 Direct SSE 行解析与事件归一化模块；解析 keepalive、`message.delta`、`tool.start`、`tool.progress`、终态事件。
- [x] `watch_session` 增加 `progress_phase` / `progress_updated_at`，WSS 重连补发活动 session 的最近阶段。
- [x] Direct run 启动后建立一次 SSE best-effort 订阅；SSE 失败不影响现有状态轮询，终态仍由 session/conversation 提交逻辑负责。
- [x] ESP32 WSS callback 改为 service-owned FreeRTOS queue；进度事件不拼接 token，UI 映射四个固定中文阶段。
- [x] Hermes 页面前台 ready 后预连接 WSS，录音上传复用连接；离页由同一 service owner 关闭 WSS。
- [x] WSS 未预连接时增加明确的 HTTP fallback；已开始发送音频的 WSS 不做无条件重复上传，避免副作用重复执行。
- [x] 保留 Relay 的保守进度策略：当前无已验证 progress frame，不发送伪造的搜索/工具阶段。
- [x] ESP-IDF build 通过：`111.bin=0xac0c20`，最小 app 分区剩余约 23%。
- [ ] Direct 云端 canary 与真实 ESP32 页面进出/录音验证完成后，再决定是否开启 Run Events 生产开关。

## 验证

- ESP32 source tests：`46 passed`。
- Server SSE/session/WSS 聚焦测试：`67 passed`；server 全量测试：`188 passed, 1 warning`（既有 Pydantic deprecation warning）。
- 已覆盖：SSE keepalive/data/终态解析、阶段映射、session 重启后阶段恢复、终态后拒绝更新、WSS 预连接复用、`task_progress` 顺序、SSE 失败回退轮询、旧 WSS 回复兼容。
- 待执行：app-flash 后进入/离开页面和真实录音串口验证；Direct 云端 canary 通过后再考虑开启 `WATCH_RUN_EVENTS_ENABLED`。

## 下一步

1. [x] 用确认的 ESP-IDF 环境执行 `idf.py build`，记录 build 产物 `111.bin=0xac0c20` 与 app 分区余量；板端 RAM/栈高水位留待真机运行记录。
2. [ ] 在真实 Direct 云端以非生产开关验证 SSE 事件、长任务、取消、重连和重复终态；Relay 继续保持现有生产行为。
3. [ ] 通过板端验证页面进入先出现 WSS connected、录音完成不再首次 connect、离页后 WSS close 且 session 仍可由 `/sync` 收敛。
4. [ ] 验证通过后再将 `WATCH_RUN_EVENTS_ENABLED=true` 作为 Direct 路径配置变更；Relay 只有拿到真实进度帧证据后才扩展阶段映射。
