---
id: attempt-memory-watch-foreground-owner-lifecycle-2026-07-14
tags: context, runs, attempt-log, memory-watch, hermes, foreground-session, freertos, resource-gate
summary: 将 Memory Watch 前台意图收回 owner task，并在 Hermes foreground gate 未 READY 时阻断录音与 WS 链路，完成阶段 1 的首个 fail-closed 小闭环。
last_reviewed: 2026-07-14
memory_type: episodic
scope: task
status: completed
owners: main/services/memory_watch/memory_watch_service.c, main/services/memory_watch/memory_watch_service.h, docs/context/plans/active/2026-07-14-watch-foreground-session-lifecycle-plan.md
triggers: memory watch foreground owner, hermes gate fail closed, foreground desired state
evidence_level: observed
record_because: error-signature, evidence
---

# Attempt Log: Memory Watch Foreground Owner Lifecycle

## 目标

完成前台 session 计划阶段 1 的首个小闭环：UI/API 只提交 Hermes 页面前台意图，`memory_watch` owner task 负责申请和释放 foreground gate；gate 未 READY 时不能开始录音或进入后续 WS 上传链路。

## 原问题

- `memory_watch_service_set_foreground()` 在调用线程直接修改 foreground 状态并申请 gate，之后又向 command queue 投递同一状态，真实 owner 不唯一。
- gate acquire 失败只记录日志，`s_foreground_active` 已经为 true，录音和 WS 仍可能继续，属于 fail-open。
- foreground snapshot 没有 desired/READY/error/generation，UI 和测试无法区分“意图已提交”与“资源已可用”。

## 修改

- 新增 `memory_watch_foreground_session_state_t` 和 foreground desired、resource ready、generation、last error 快照字段。
- `memory_watch_service_set_foreground()` 只更新 desired state/generation，并用 `xTaskAbortDelay()` 唤醒 owner task，不再直接 acquire/release gate。
- `memory_watch_service_reconcile_foreground()` 只在 `memory_watch` service task 中执行 gate acquire/release，并用 generation 忽略过期结果。
- gate acquire 失败发布 `MEMORY_WATCH_FOREGROUND_ERROR`，不设置 READY，也不会自动紧循环重试；新的 foreground 意图会生成新 generation 后再尝试。
- `memory_watch_service_begin_recording()` 和 owner 侧 begin handler 双重检查 foreground READY，未就绪返回或记录 `ESP_ERR_INVALID_STATE`。
- 页面离开时先请求 recorder 停止，并等待 upload worker 退出当前 recorder/WS operation 后再 release gate；5 秒超时只发布错误并继续持有 gate，不强删运行中 task。
- 初始化时先创建所有 worker，最后创建 service task；任一 task 创建失败会删除本轮尚未对外发布的 task、重置静态 queue/event bits 并释放 conversation PSRAM staging，使下一次 init 可重试。
- Inbox、pending `/sync`、最近对话缓存、endpoint 配置及长期 worker task 保持原生命周期，本轮不销毁。

## FreeRTOS 选择

- desired state 是“只关心最新值”的控制意图，不继续塞入 command queue，避免重复 enter/leave 占满队列。
- `xTaskAbortDelay()` 只负责唤醒当前阻塞在 queue receive 的 owner task；真实状态仍由 owner task reconcile。
- 录音、发送、取消等必须逐条执行的业务命令继续使用 queue。
- generation 用于区分连续 enter/leave 和重试，避免旧 acquire/release 结果覆盖新意图。

## 验证

- 新增 source-test 先失败，确认旧代码缺少 foreground lifecycle snapshot 和 owner reconcile。
- 聚焦 source tests：`44 passed`。
- 全量 source tests：`422 passed / 7 failed`；失败集合与本轮开始前一致，没有新增失败。
- `idf.py build`：通过；`111.bin=0xac5f30`，最小 app 分区余量 `0x33a0d0`（23%）。
- `git diff --check`：通过，仅工作树 LF/CRLF 提示。
- 本轮未 app-flash，也未做 COM3 真机切换回归。

## 未完成边界

- 当前 stop ACK 使用 upload worker busy 作为 recorder/WS operation 已退出的事实；真机阶段仍需验证快速离页、上传中离页和 5 秒超时日志顺序。
- Safety Monitor quiesced ACK、official_chat 和 BLE 生命周期不属于本次修改。
