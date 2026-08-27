---
id: context-plans-active-readme
tags: context, plans, task-memory
summary: 说明 active plans 目录用于维护进行中的复杂任务执行计划和活文档进度。
last_reviewed: 2026-08-07
memory_type: task
scope: task
owners: docs/context/plans/active
triggers: plan, execplan, milestone, long-task
evidence_level: design
status: active
---

# Active Plans 目录说明

该目录用于保存正在推进的复杂任务计划。适合：

- 新增模块或跨文件重构
- 需要多轮验证的联调任务
- 预计跨多轮会话推进的任务

根据最新强制约束（`check.py`），活跃计划书**必须**精确包含以下二级标题（中文或旧版英文均可），否则将触发验证阻断：

- `进度` (旧版：`Progress`)

此外，强烈建议包含以下部分以补充完整上下文：

- `验证与验收` (旧版：`Validation and Acceptance`)
- `下一步` (旧版：`Next Step`)
- `目标与全局` (Purpose / Big Picture)
- `范围与非目标` (Scope / Non-Goals)
- `决策记录` (Decision Log)
- `意外与发现` (Surprises & Discoveries)
- `幂等与恢复` (Idempotence and Recovery)

> [!WARNING]
> 当 `Progress` 进度内的待办全部打勾完成后，脚本将报错拦截。此时您必须将本计划转移到 `../completed/` 归档，并将 Frontmatter 状态改为 `archived`。

推荐直接从 `plan-template.md` 复制开始，避免每次手写结构。
