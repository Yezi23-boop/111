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
- `docs/context/knowledge/` 负责长期稳定知识、架构边界和项目取舍。
- `docs/context/procedures/` 负责标准做法。
- `docs/context/runs/` 负责单次验证和实验记录。
- `docs/context/plans/` 负责复杂任务计划。
- `docs/context/handoffs/` 负责当前状态压缩与交接。

## 维护脚本

- 普通任务低 token 检索：`uv run python scripts/context/validate_context.py --level light --q "<关键词>" --brief`
- 只改 context 文档：`uv run python scripts/context/validate_context.py --level standard`
- 改入口或检索基准：`uv run python scripts/context/validate_context.py --level routing`
- 改 `scripts/context` 或记忆机制：`uv run python scripts/context/validate_context.py --level full`
- 底层脚本 `build_index.py`、`check.py`、`query.py`、`pack_context.py`、`garden.py` 只作为高级调试或 `validate_context.py` 的实现细节使用。

## 当前维护原则

- 先查命中最高的上下文，不全量阅读文档库。
- 长任务优先落 `plans/active/`，任务结束后再归档。
- 单次验证证据优先进入 `runs/`，避免长期知识库被一次性结论污染。

## 首读入口

- agent 首读入口：`docs/context/INDEX.agent.md`
- 仓库级首读画像：`docs/context/knowledge/project/project-profile.md`
- 总体模块地图：`docs/context/knowledge/project/repo-overview.md`，仅在 brief 命中或确实需要全局骨架时打开
- 目录/owner 映射：`docs/context/knowledge/project/main-directory-map.md`
