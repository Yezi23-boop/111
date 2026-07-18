---
id: attempt-official-chat-foreground-owner-lifecycle-2026-07-14
tags: context, runs, attempt-log, official-chat, foreground-session, resource-gate, audio
summary: 将 official_chat gate 申请改为 fail-closed，并把 gate 与 Safety audio blocker 的释放延后到 callback、transport、audio 和 instance 全部销毁之后。
last_reviewed: 2026-07-14
memory_type: episodic
scope: task
status: completed
owners: main/services/official_chat_service.c, docs/context/plans/active/2026-07-14-watch-foreground-session-lifecycle-plan.md
triggers: official chat lifecycle, gate fail closed, destroy before release
evidence_level: observed
record_because: error-signature, evidence
---

# Attempt Log: official_chat Foreground Owner Lifecycle

## 目标

修复 official_chat 的两个资源窗口缺口：gate acquire 失败后仍创建底层 instance，以及离页一开始就 release gate、但 transport/audio/instance 尚未销毁。

## 修改

- `official_chat_service_set_foreground_runtime_active()` 改为返回 `esp_err_t`；release 失败时不提前清除本地 held 状态。
- enter 命令先申请 gate；失败时发布 ERROR、保持 foreground=false，不启动 audio blocker，也不允许 service task 创建 `s_chat_handle`。
- begin shutdown 只记录 desired stop，不再提前清除 audio blocker 或 release gate。
- shutdown 顺序固定为 prepare/stop、等待 idle、transport quiet period、解绑 callback、`official_chat_destroy()`、清除 audio blocker、最后 release gate。
- gate release 失败时保持 shutdown pending 并在 owner task 重试，不伪装 STOPPED。
- `official_chat_start_internal()` 失败后进入统一 shutdown 路径，确保已经持有的 gate 会释放。

## 验证

- official_chat/foreground 聚焦 source tests：`28 passed`。
- ESP-IDF build：通过；`111.bin=0xac6040`，最小 app 分区余量 `0x339fc0`（23%）。
- 未 app-flash；快速进出页面、连接中离页和 gate 冲突留待阶段 7 真机组合回归。

## 边界

- 没有修改 `components/official_chat` 的 Application、transport 或 audio adapter。
- Safety Monitor 真正 quiesced ACK 尚未实现；当前只保证 official_chat 自身的 release 顺序正确。

