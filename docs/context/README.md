# 上下文库说明

该目录是 Codex 使用的项目长期上下文库。

## 目录结构

- `knowledge/`：可复用技术知识、排障手册、实践清单。
- `decisions/`：架构决策记录（ADR）。
- `procedures/`：稳定标准流程、排查手册和执行套路。
- `runs/`：单次实验、bring-up、联调和验证闭环记录。
- `plans/active/`：进行中的复杂任务执行计划。
- `plans/completed/`：已完成任务的计划归档。
- `handoffs/`：当前任务压缩摘要、仓库现状摘要等交接文档。
- `evals/`：检索基准、查询样例和上下文系统评测输入。
- `INDEX.agent.md`：agent 首读的低 token 路由入口。
- `CHANGELOG.md`：上下文库的变更流水。
- `knowledge-map.md`：知识导航入口。

## Frontmatter 约定

内容型上下文文档统一包含以下基础头部：

```yaml
---
id: 唯一标识
tags: 标签1, 标签2, 标签3
summary: 一句话摘要
last_reviewed: YYYY-MM-DD
---
```

推荐补充以下扩展字段，用于后续检索、筛选与园艺脚本：

```yaml
memory_type: semantic | procedural | episodic | task
scope: repo | component | board | task
owners: main/services/network_service.c, components/network_manager
triggers: wifi, provisioning, softap
evidence_level: observed | inferred | design
status: active | stale | superseded | retired | deprecated | archived
superseded_by: docs/context/knowledge/project/current-card.md
```

`status` 与 `superseded_by` 用于清理和降权：普通查询默认优先 `active` 当前事实，历史、退场、废弃或被替代的卡只在用户明确查历史/归档/迁移时回到前排。

## 常用命令

构建索引：

```bash
uv run python scripts/context/build_index.py
```

校验文档：

```bash
uv run python scripts/context/check.py
```

检索上下文：

```bash
uv run python scripts/context/query.py --q "esp32 s3 lvgl 触摸漂移" --top 5
```

按标签过滤检索：

```bash
uv run python scripts/context/query.py --q "待机电流过高" --tag power --tag esp32-s3
```

打包可复用上下文片段：

```bash
uv run python scripts/context/pack_context.py --q "待机功耗优化" --top 5 --mode brief --max-chars 1800
```

检索历史尝试，避免重复动作：

```bash
uv run python scripts/context/query.py --scope runs --q "模块 文件 错误码 症状" --top 8
```

快速记录本轮 agent 尝试：

```bash
uv run python scripts/context/log_attempt.py --title "wifi 管理页二次进入崩溃排查" --status partial --changed main/ui/wifi_management_controller.c --tried "复用前检查 lv_obj_is_valid" --avoid "不要只清空局部按钮指针而保留 screen 缓存" --evidence "monitor: LoadProhibited in lv_style_get_prop" --next "补 LV_EVENT_DELETE 清缓存后复测"
```

园艺检查：

```bash
uv run python scripts/context/garden.py --verbose
```

检索回归评测：

```bash
uv run python scripts/context/eval_query.py
```

## 低 token 工作流

默认入口只读两份：

- `docs/context/INDEX.agent.md`
- `docs/context/knowledge/project/project-profile.md`

推荐顺序：

1. 先查历史 attempt，避免重复动作：
   `uv run python scripts/context/query.py --scope runs --q "<模块 文件 错误码 症状>" --top 8`
2. 再查稳定知识：
   `uv run python scripts/context/query.py --q "<任务关键词>" --top 5`
3. 再打 brief 上下文包：
   `uv run python scripts/context/pack_context.py --q "<任务关键词>" --top 5 --mode brief --max-chars 1800 --print`
4. 最后只打开必要原文：
   只读 brief pack 的 top hit、owner 文件、或需要引用证据的原始 Markdown。

不要默认全量读 `README.md`、`knowledge-map.md`、`repo-overview.md` 或 `knowledge/**`。
如果 brief pack 不够，再显式改用 `--mode standard`，不要反过来默认长上下文。

## 维护规范

- 每个文档聚焦一个主题。
- 优先写清单、阈值和验收标准。
- 首读入口控制在 `INDEX.agent.md` 和 `project-profile.md`；新增大段说明前先考虑能否通过 query/brief pack 解决。
- 修改文档时同步更新 `last_reviewed`。
- 长任务优先先落计划，再改代码；推荐在 `plans/active/` 维护 `Progress`、`Decision Log`、`Validation`。
- 会被后续 agent 重复尝试的修改、失败路径、验证命令和关键日志，优先写成 `runs/` attempt 记录；推荐字段是“修改过的文件或 owner / 执行过的动作 / 不应直接重复的路径 / 证据 / 下一步”。
- 交接文档优先保持“少而稳”：`current-task.md` 用于当前任务压缩，`current-repo-state.md` 用于仓库骨架压缩。
- 推荐从模板起步：`plans/active/plan-template.md`、`runs/run-template.md`、`runs/attempt-template.md`、`handoffs/handoff-template.md`。

## 记忆晋升规则

- 跨任务稳定成立的事实、模块边界、经验规则，进入 `knowledge/`。
- 不可逆或需要长期追溯的设计取舍，进入 `decisions/`。
- 可复用的操作套路、排查顺序和标准流程，进入 `procedures/`。
- 一次性的实验、日志、板测、联调与验证闭环，进入 `runs/`。
- 为了避免后续 agent 重复同一动作而记录的“改过什么、试过什么、哪里失败、下一步怎么接”，也进入 `runs/`，并在开工前用 `--scope runs` 先检索。
- 正在推进且需要多轮维护的复杂任务，进入 `plans/active/`；结束后再归档到 `plans/completed/`。
- 为了跨会话续跑或压缩当前状态而写的摘要，进入 `handoffs/`。

## 清理与沉淀

- 详细流程见 `docs/context/procedures/context-garden-policy.md`。
- 清理旧知识按“标记 -> 降权 -> 替换引用 -> 归档 -> 删除”推进，不直接删除仍有复盘价值的历史卡。
- `garden.py` 只输出候选队列：`stale_candidates`、`promotion_candidates`、`archive_candidates`、`broken_owner_refs`。
- `runs/` 中成功且有证据的记录，如果变成稳定事实或可复用流程，应晋升到 `knowledge/` 或 `procedures/`，原 attempt 保留为证据入口。
- `query.py` 默认压低 `stale/superseded/retired/deprecated`，并排除 `archived`；用户查询历史、退场、迁移、归档或考古时才把旧卡放回候选。
