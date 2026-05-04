---
id: context-plans-active-readme
tags: context, plans, task-memory
summary: 说明 active plans 目录用于维护进行中的复杂任务执行计划和活文档进度。
last_reviewed: 2026-05-04
memory_type: task
scope: task
owners: docs/context/plans/active
triggers: plan, execplan, milestone, long-task
evidence_level: design
---

# Active Plans 目录说明

该目录用于保存正在推进的复杂任务计划。适合：

- 新增模块或跨文件重构
- 需要多轮验证的联调任务
- 预计跨多轮会话推进的任务

建议计划至少包含以下部分：

- `Purpose / Big Picture`
- `Scope / Non-Goals`
- `Progress`
- `Decision Log`
- `Surprises & Discoveries`
- `Validation and Acceptance`
- `Idempotence and Recovery`
- `Next Step`

任务完成后，应将最终计划转移到 `../completed/`，并补齐结果总结。

推荐直接从 `plan-template.md` 复制开始，避免每次手写结构。
