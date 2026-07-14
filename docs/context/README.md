# 上下文库说明

该目录是 Codex 使用的项目长期上下文库。

## 目录结构

- `knowledge/`：项目知识、项目框架、稳定约束、偏好和可复用技术事实。
- `procedures/`：稳定标准流程、排查手册和执行套路。
- `runs/`：单次实验、bring-up、联调和验证闭环记录。
- `plans/active/`：进行中的复杂任务执行计划。
- `plans/completed/`：已完成任务的计划归档。
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
memory_type: project_plan | trial_error | project_knowledge | framework | constraints | stable_preferences
scope: repo | component | board | task
owners: main/services/network/network_service.c, components/network_manager
triggers: wifi, provisioning, softap
evidence_level: observed | inferred | design
status: active | stale | superseded | retired | deprecated | archived
superseded_by: docs/context/knowledge/project/current-card.md
record_reasons: repeat-risk, error-signature, route-choice, evidence, owner-architecture
```

`status` 与 `superseded_by` 用于清理和降权：普通查询默认优先 `active` 当前事实，历史、退场、废弃或被替代的卡只在用户明确查历史/归档/迁移时回到前排。
旧文档中的 `semantic / procedural / episodic / task` 仍可保留，不要求批量迁移；新增长期记忆优先使用上面的 7 类。

## 常用命令

分级上下文检索/验证：

```bash
uv run python scripts/context/validate_context.py --level light --q "任务关键词" --brief
uv run python scripts/context/validate_context.py --level standard
uv run python scripts/context/validate_context.py --level routing
uv run python scripts/context/validate_context.py --level full
```

底层维护脚本（普通任务不要默认从这里开始）：

构建索引：

```bash
uv run python scripts/context/build_index.py
```

校验文档：

```bash
uv run python scripts/context/check.py
```

高级调试：直接检索上下文：

```bash
uv run python scripts/context/query.py --q "esp32 s3 lvgl 触摸漂移" --top 5
```

高级调试：按标签过滤检索：

```bash
uv run python scripts/context/query.py --q "待机电流过高" --tag power --tag esp32-s3
```

高级调试：手动打包可复用上下文片段：

```bash
uv run python scripts/context/pack_context.py --q "待机功耗优化" --top 5 --mode brief --max-chars 1800 --print --no-write
```

高级调试：直接检索历史尝试，避免重复动作：

```bash
uv run python scripts/context/query.py --scope runs --q "模块 文件 错误码 症状" --top 8
```

快速记录本轮 agent 尝试：

```bash
uv run python scripts/context/log_attempt.py --title "wifi 管理页二次进入崩溃排查" --status partial --changed main/ui/wifi_management_controller.c --tried "复用前检查 lv_obj_is_valid" --avoid "不要只清空局部按钮指针而保留 screen 缓存" --evidence "monitor: LoadProhibited in lv_style_get_prop" --next "补 LV_EVENT_DELETE 清缓存后复测"
```

现在写入 attempt 需要显式说明长期记录理由，例如：

```bash
uv run python scripts/context/log_attempt.py --title "wifi 管理页二次进入崩溃排查" --record-because repeat-risk --record-because evidence --status partial --changed main/ui/wifi_management_controller.c --tried "复用前检查 lv_obj_is_valid" --avoid "不要只清空局部按钮指针而保留 screen 缓存" --evidence "monitor: LoadProhibited in lv_style_get_prop" --next "补 LV_EVENT_DELETE 清缓存后复测"
```

大问题错误或路线选择记录可使用：

```bash
--record-because error-signature --record-because evidence
--record-because route-choice --record-because evidence
```

园艺检查：

```bash
uv run python scripts/context/garden.py --verbose
```

检索回归评测：

```bash
uv run python scripts/context/eval_query.py
```

经验总结闭环自检：

```bash
uv run python scripts/context/test_memory_flow.py
```

该脚本会验证首读触发入口、`log_attempt.py` 写入门槛、`--force` 旁路可审计、临时 attempt schema、`runs/` 检索、`validate_context light --brief` 不落盘、context index 新鲜度、`garden --summary-json` 与 query golden 回归。

## 低 token 工作流

默认入口只读两份：

- `docs/context/INDEX.agent.md`
- `docs/context/knowledge/project/project-profile.md`

推荐顺序：

1. 普通任务使用分级脚本：
   `uv run python scripts/context/validate_context.py --level light --q "<模块 文件 错误码 症状>" --brief`
2. 最后只打开必要原文：
   只读 brief pack 的 top hit、owner 文件、或需要引用证据的原始 Markdown。

不要默认全量读 `README.md`、`knowledge-map.md`、`repo-overview.md` 或 `knowledge/**`。
如果 brief pack 不够，再显式改用 `--mode standard`，不要反过来默认长上下文。

## 维护规范

- 每个文档聚焦一个主题。
- 长期上下文优先保存用户规划、试错总结、项目知识、项目框架、长期约束和稳定偏好；详见 `docs/context/knowledge/project/context-memory-policy.md`。
- 优先写清单、阈值和验收标准。
- 首读入口控制在 `INDEX.agent.md` 和 `project-profile.md`；新增大段说明前先考虑能否通过 query/brief pack 解决。
- 修改文档时同步更新 `last_reviewed`。
- 长任务优先先落计划，再改代码；推荐在 `plans/active/` 维护 `Progress`、`Decision Log`、`Validation`。
- 会被后续 agent 重复尝试、失败代价高、影响 owner/架构、包含关键证据或需要跨会话接手的内容，才写成 `runs/` attempt；一次性小事和无复用价值细节不要写入长期上下文。
- `handoffs/` 已退场：当前接手状态优先维护在对应 `plans/active/`，失败路线和证据写入 `runs/`，稳定事实写入 `knowledge/`。
- 推荐从模板起步：`plans/active/plan-template.md`、`runs/run-template.md`、`runs/attempt-template.md`。

## 记忆晋升规则

- 跨任务稳定成立的事实、模块边界、经验规则，进入 `knowledge/`。
- 不可逆或需要长期追溯的设计取舍，进入 `knowledge/project/` 的稳定边界卡，并在正文记录取舍原因和替代方案。
- 可复用的操作套路、排查顺序和标准流程，进入 `procedures/`。
- 有复用价值的一次性实验、日志、板测、联调与验证闭环，进入 `runs/`。
- 为了避免后续 agent 重复同一动作而记录的“改过什么、试过什么、哪里失败、下一步怎么接”，也进入 `runs/`，但必须满足 `record_reasons` 门槛，并在开工前用 `--scope runs` 先检索。
- 正在推进且需要多轮维护的复杂任务，进入 `plans/active/`；结束后再归档到 `plans/completed/`。
- 为了跨会话续跑或压缩当前状态而写的摘要，优先进入对应 `plans/active/`；没有 active plan 但具备复用价值的接手状态，写入 `runs/` 并使用 `handoff` 或 `evidence` 记录理由。

## 清理与沉淀

- 详细流程见 `docs/context/procedures/context-garden-policy.md`。
- 清理旧知识按“标记 -> 降权 -> 替换引用 -> 归档 -> 删除”推进，不直接删除仍有复盘价值的历史卡。
- `garden.py` 只输出候选队列：`stale_candidates`、`promotion_candidates`、`archive_candidates`、`broken_owner_refs`。
- `runs/` 中成功且有证据的记录，只有能提炼为项目知识、试错总结、流程、决策、约束或稳定偏好时，才应晋升到 `knowledge/` 或 `procedures/`；普通成功小事不自动晋升。
- `query.py` 默认压低 `stale/superseded/retired/deprecated`，并排除 `archived`；用户查询历史、退场、迁移、归档或考古时才把旧卡放回候选。

## 验证分级

- `light`：普通代码任务，只做 runs/knowledge 检索和可选 brief pack，不重建索引。
- `standard`：只改 `docs/context` 普通知识卡、runs、plans 时使用，运行 `build_index.py + check.py`。
- `routing`：改 `INDEX.agent.md`、`project-profile.md`、`knowledge-map.md`、`query-golden.yaml` 或重要路由时使用，额外运行 `eval_query.py`。
- `full`：改 `scripts/context`、query 评分、garden 规则、pack 逻辑、记忆政策、生命周期字段、归档/晋升机制时使用，运行完整机制验证。
