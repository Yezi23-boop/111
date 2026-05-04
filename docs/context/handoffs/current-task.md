---
id: context-current-task
tags: context, handoff, current-task
summary: 当前任务交接模板，用于跨会话续跑时压缩记录目标、进展、风险和下一步。
last_reviewed: 2026-05-04
memory_type: task
scope: task
owners: docs/context/handoffs
triggers: handoff, current-task, next-step
evidence_level: design
---

# 当前任务交接模板

## 目标

- 在这里写当前任务的用户目标和验收口径。

## 当前状态

- 在这里用 2-5 行压缩说明当前处于哪一步、为什么还没结束。

## Progress

- 在这里写已经落地的关键动作和已验证结果。

## Decision Log

- 在这里记录本轮已经做出的关键取舍，避免下轮会话重复讨论。

## 已验证

- 在这里写已经跑过的命令、日志、板测或查询结果。

## 当前风险

- 在这里写仍未确认的边界、假设或阻塞点。

## 下一步

- 在这里写最小下一步动作，优先写可执行命令或应先读的文档。

## 证据入口

- 相关计划：`docs/context/plans/active/...`
- 相关运行记录：`docs/context/runs/...`
- 相关长期知识：`docs/context/knowledge/...`
