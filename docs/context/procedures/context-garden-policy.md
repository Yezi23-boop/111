---
id: context-garden-policy
tags: context, garden, curator, stale, promotion, archive
summary: 上下文库清理与沉淀流程：先标记和降权，再晋升稳定知识，最后归档或删除废弃卡，避免 agent 被过期记忆误导。
last_reviewed: 2026-05-04
memory_type: procedural
scope: repo
owners: docs/context, scripts/context/garden.py, scripts/context/query.py
triggers: context-garden, stale, promotion, archive, cleanup, curator
evidence_level: design
status: active
---

# Context Garden Policy

## 目标

- 让 agent 默认读到当前有效知识，而不是历史噪声。
- 保留有价值的失败路径和实验记录，避免重复尝试。
- 把多次验证成立的经验从 `runs/` 晋升到稳定层。
- 清理动作必须可逆、可追溯，不靠一次搜索结果直接删除。

## 记忆分层

- `INDEX.agent.md`：低 token 首读入口，只负责路由和红线。
- `runs/`：episodic memory，记录做过什么、失败什么、别重复什么。
- `knowledge/`：semantic memory，只放稳定事实、当前 owner、模块边界。
- `procedures/`：procedural memory，只放可复用流程和排查套路。
- `decisions/`：长期设计取舍，尤其是不可逆或影响范围大的选择。
- `handoffs/`：当前任务压缩摘要，不作为长期事实源。
- `archive/`：默认不参与普通检索，只保留历史追溯入口。

## 生命周期字段

推荐在 frontmatter 使用：

```yaml
status: active | stale | superseded | retired | deprecated | archived
superseded_by: docs/context/knowledge/project/current-card.md
```

- `active`：默认状态，代表当前仍可指导 agent。
- `stale`：可能过期，需要复查；query 默认降权。
- `superseded`：已有替代卡；query 默认强降权。
- `retired/deprecated`：路线已退场；默认只作历史背景。
- `archived`：已归档；普通检索默认排除，查历史时才返回。

## 清理梯子

1. 先标记：补 `status`、`superseded_by`、使用边界，不直接删。
2. 再降权：让 `query.py` 默认不把旧卡排到 top。
3. 再替换引用：修正 `owners`、`knowledge-map`、`INDEX.agent.md` 中旧入口。
4. 再归档：确认替代卡可达后，移动到 `docs/context/archive/` 或标 `archived`。
5. 最后删除：只有确认没有引用、没有复盘价值、没有历史追溯价值时才删。

## 晋升梯子

1. 单次动作、失败、日志和验证先写 `runs/attempt`。
2. 同类问题重复出现，或一次结果已成为稳定 owner/边界，再写 `knowledge/`。
3. 可复用的操作步骤、排障顺序、检查清单，写 `procedures/`。
4. 影响架构路线或长期兼容边界，写 `decisions/ADR-*`。
5. 晋升后保留原 `runs/` 作为证据入口，并从稳定卡链接回关键 attempt。

## Garden Curator 输出

`garden.py` 应输出四类候选，而不是直接修改文件：

- `stale_candidates`：过期、历史、退场、废弃、被替代或 `last_reviewed` 超阈值。
- `promotion_candidates`：成功且有证据的 `runs/`，适合沉淀为知识或流程。
- `archive_candidates`：已有 `superseded_by` 且状态不是 active 的卡。
- `broken_owner_refs`：frontmatter `owners` 指向不存在路径。

## Query 默认策略

- 普通 `mixed/knowledge/procedures` 查询优先返回 active 当前事实。
- `runs/` 只在用户查“做过什么/失败/日志/错误码/重复动作”时自然进入 top。
- `superseded/stale/retired/deprecated` 默认降权。
- `archived` 或 `docs/context/archive/` 默认不返回。
- 用户查询“历史/旧方案/退场/归档/迁移/考古”时，允许历史卡回到结果中。

## 收尾要求

- 每次 context 结构或策略变更都更新 `docs/context/CHANGELOG.md`。
- 变更后运行 `build_index.py`、`check.py`、`garden.py --verbose`、`eval_query.py`。
- 如果 `garden.py` 给出候选，只把它当审查队列，不自动判定为可删。
