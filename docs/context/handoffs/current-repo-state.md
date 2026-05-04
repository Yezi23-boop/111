---
id: context-current-repo-state
tags: context, handoff, repo-state
summary: 压缩记录当前仓库上下文系统的结构、入口和维护方式，便于新会话快速恢复骨架。
last_reviewed: 2026-05-04
memory_type: task
scope: repo
owners: AGENTS.md, docs/context
triggers: handoff, repo-state, current-context
evidence_level: observed
---

# 当前仓库状态摘要

## 上下文系统骨架

- 根入口是 `AGENTS.md`，负责规则优先级、上下文使用流程和专项触发边界。
- `docs/context/knowledge/` 负责长期稳定知识。
- `docs/context/decisions/` 负责 ADR。
- `docs/context/procedures/` 负责标准做法。
- `docs/context/runs/` 负责单次验证和实验记录。
- `docs/context/plans/` 负责复杂任务计划。
- `docs/context/handoffs/` 负责当前状态压缩与交接。

## 维护脚本

- `uv run python scripts/context/build_index.py`
- `uv run python scripts/context/check.py`
- `uv run python scripts/context/query.py --q "<关键词>" --top 5`
- `uv run python scripts/context/pack_context.py --q "<关键词>" --top 5`
- `uv run python scripts/context/garden.py`

## 当前维护原则

- 先查命中最高的上下文，不全量阅读文档库。
- 长任务优先落 `plans/active/`，任务结束后再归档。
- 单次验证证据优先进入 `runs/`，避免长期知识库被一次性结论污染。

## 首读入口

- 仓库级首读画像：`docs/context/knowledge/project/project-profile.md`
- 总体模块地图：`docs/context/knowledge/project/repo-overview.md`
- 目录/owner 映射：`docs/context/knowledge/project/main-directory-map.md`
