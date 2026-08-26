---
id: context-plans-completed-readme
tags: context, plans, archive
summary: 说明 completed plans 目录用于归档已完成复杂任务的执行计划和复盘。
last_reviewed: 2026-05-04
memory_type: task
scope: task
owners: docs/context/plans/completed
triggers: completed-plan, archive, retrospective
evidence_level: design
status: active
---

# Completed Plans 目录说明

该目录用于归档已完成或已被替代、但仍有复盘价值的复杂任务计划。

保留这些计划的目的是：

- 复盘某次复杂任务是如何推进的
- 复用里程碑拆分和验证方式
- 在未来遇到相似问题时快速回放历史路径

归档前建议补齐：

- 最终 `Progress`
- `Decision Log`
- `Validation and Acceptance`
- `Outcomes & Retrospective`
- 关键验证命令和结果摘要

若计划是被替代而非完成，应在 frontmatter 使用 `status: superseded`
和 `superseded_by` 指向当前有效文档，避免未来 agent 误当作待执行任务。
