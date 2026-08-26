---
id: context-garden-policy
tags: context, garden, curator, stale, promotion, archive
summary: 上下文库清理与沉淀流程：只保存高复用规划、决策、试错、项目知识和框架，先标记和降权，再晋升稳定知识，最后归档或删除废弃卡。
last_reviewed: 2026-06-18
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
- 阻止一次性小事、命令流水和无复用价值细节进入长期记忆。
- 清理动作必须可逆、可追溯，不靠一次搜索结果直接删除。

## 记忆分层

- `INDEX.agent.md`：低 token 首读入口，只负责路由和红线。
- `plans/`：Project Plan，记录用户的项目规划、阶段目标和长期路线。
- `runs/`：Trial-and-Error，只记录有复用价值的试错、失败路径、证据和不要重复的动作。
- `knowledge/`：Project Knowledge / Framework / Constraints / Stable Preferences，只放稳定事实、当前 owner、模块边界、方法论、长期约束和稳定偏好。
- `procedures/`：可复用流程和排查套路。
- `handoffs/` 已退场：不再作为当前任务接力层；旧内容只作为 archive 历史证据。
- `archive/`：默认不参与普通检索，只保留历史追溯入口。

## 写入门槛

写入 `runs/attempt` 前至少满足一项：

- 后续 agent 很可能重复同一路径。
- 失败代价高，重新试错会浪费明显时间。
- 出现非平凡错误签名，并且已经形成可复用判断或下一轮排查边界。
- 影响 owner、架构边界、项目框架、长期约束或用户规划。
- 有关键证据，例如真机日志、构建结果、错误码、性能数据或明确复现步骤。
- 当前任务需要跨会话接手。

不满足这些条件时，不写长期上下文，只在本轮答复中说明即可。

## 小闭环收尾写入规则

完成一个可提交的小闭环后，默认先更新对应 `plans/active/` 里的进度、验证状态和下一步。`CHANGELOG.md` 只记录后续有检索价值的摘要，例如关键验证、协议、owner、安全边界、门禁或后续 agent 必须知道的结论；不要把 `CHANGELOG.md` 当作普通命令流水账。

`CHANGELOG.md` 是时间线记录：历史条目引用的已删除/已合并/已重命名路径是当时快照，不作为死链阻断项，也不要求回改历史条目；死链检查只针对 `knowledge/`、`plans/active/`、`procedures/`、`INDEX.agent.md` 等活文档。

## 合并/蒸馏/删除卡的标准流程

- 合并或蒸馏前：先跑引用集扫描（grep 被改卡名），把所有入向引用与卡改动放进同一个提交，禁止留下死链。
- 删除前验证覆盖：声称「镜像卡」或「已被替代」的卡，必须逐张确认替代来源（skill、新卡、git 历史）确实覆盖其事实，再删。
- 改动 golden 锚定卡、或删除被 `query-golden.yaml` 期望的卡：必须在同一提交里更新 golden yaml，并重跑 `uv run python scripts/context/eval_query.py` 全绿后才算完成。
- 卡片之间矛盾时：以固件源码为准裁决，把结论写回卡内；不要凭卡片新旧自行取舍。
- 新卡与被实质修改的卡必须带 `status`，并更新 `last_reviewed` 为当天；`runs/`、`plans/completed/`、`CHANGELOG.md` 是时间线快照，garden 对其豁免园艺警告。

`handoffs/current-task.md` 已退场，不再维护。跨会话接手状态优先写到对应 `plans/active/` 的 `Progress / Decision Log / Validation / Next Step`；失败路线、特殊证据、可复用排查结论写入 `runs/`；稳定事实、长期 owner 或架构边界再进入 `knowledge/`。不得写真实 key/token，也不要把 `CHANGELOG.md` 复述成任务状态。

失败路线、特殊证据、可复用排查结论写入 `runs/`；稳定事实、长期 owner 或架构边界再进入 `knowledge/`。

## 大问题错误与路线选择

错误类沉淀主要面向大问题，仍要避免记录一次性小问题。适用范围包括阻塞构建/链接、panic/Guru、watchdog、断言、`ESP_ERR_*`、`NO_MEM`、串口/板测异常、硬件通信失败、context 检索/校验失败。

满足以下任一条件时，可考虑写 `runs/attempt`：

- 错误有稳定签名、错误码、日志片段或复现命令。
- 本轮已经证伪过一条路线，后续 agent 容易重复。
- 错误跨 owner、跨任务、跨硬件状态，或定位成本明显高于普通小修。
- 当前只得到 partial 结论，需要下一轮接手继续补证据。
- 本轮做过路线选择：选择继续、放弃、回退或换 owner，并且这个取舍后续可能被重新讨论。

相关 attempt 至少记录：错误原文或路线问题、触发条件、相关 owner/文件、已尝试动作、不要重复的路径、当前可信判断和下一步证据。

## 生命周期字段

推荐在 frontmatter 使用：

```yaml
status: active | stale | superseded | retired | deprecated | archived
superseded_by: docs/context/knowledge/project/current-card.md
garden_status: archived | covered | keep-evidence | keep-history | no-action
garden_reviewed: YYYY-MM-DD
```

- `active`：默认状态，代表当前仍可指导 agent。
- `stale`：可能过期，需要复查；query 默认降权。
- `superseded`：已有替代卡；query 默认强降权。
- `retired/deprecated`：路线已退场；默认只作历史背景。
- `archived`：已归档；普通检索默认排除，查历史时才返回。
- `garden_status/garden_reviewed`：表示人工已经审查过某个 garden 候选；在 `garden_reviewed` 未超过 freshness 阈值时，`garden.py` 不再把它反复列入 stale/archive/promotion/low-value 候选，但仍会继续检查 frontmatter、owner 路径和推荐章节等硬性质量问题。

## 清理梯子

1. 先标记：补 `status`、`superseded_by`、使用边界，不直接删。
2. 再降权：让 `query.py` 默认不把旧卡排到 top。
3. 再替换引用：修正 `owners`、`knowledge-map`、`INDEX.agent.md` 中旧入口。
4. 再归档：确认替代卡可达后，移动到 `docs/context/archive/` 或标 `archived`。
5. 最后删除：只有确认没有引用、没有复盘价值、没有历史追溯价值时才删。

## 晋升梯子

1. 有复用价值的动作、失败、大问题错误签名、路线选择、日志和验证先写 `runs/attempt`，并用 `record_reasons` 说明长期记录理由。
2. 同类问题重复出现，或一次结果已成为稳定 owner/边界，再写 `knowledge/`。
3. 可复用的操作步骤、排障顺序、检查清单，写 `procedures/`。
4. 影响架构路线或长期兼容边界，写 `knowledge/project/` 稳定边界卡，并在正文记录取舍原因和替代方案。
5. 晋升后保留原 `runs/` 作为证据入口，并从稳定卡链接回关键 attempt。

## Garden Curator 输出

`garden.py` 应输出四类候选，而不是直接修改文件：

- `stale_candidates`：过期、历史、退场、废弃、被替代或 `last_reviewed` 超阈值。
- `promotion_candidates`：成功、有证据且具备长期记录理由或晋升提示的 `runs/`。
- `low_value_run_candidates`：成功但缺少长期记录理由的 `runs/`，用于后续审查是否归档或删除。
- `archive_candidates`：已有 `superseded_by` 且状态不是 active 的卡。
- `broken_owner_refs`：frontmatter `owners` 指向不存在路径。

## Query 默认策略

- 普通 `mixed/knowledge/procedures` 查询优先返回 active 当前事实。
- `runs/` 只在用户查“做过什么/失败/日志/错误码/重复动作”时自然进入 top。
- `superseded/stale/retired/deprecated` 默认降权。
- `archived` 或 `docs/context/archive/` 默认不返回。
- 用户查询“历史/旧方案/退场/归档/迁移/考古”时，允许历史卡回到结果中。

## 收尾要求

- context 结构或策略变更只有在会影响后续检索、路由、写入门槛或 agent 行为时，才更新 `docs/context/CHANGELOG.md`，并只写摘要。
- 验证按影响范围分级运行 `scripts/context/validate_context.py`：只改文档用 `--level standard`，改入口或检索基准用 `--level routing`，改脚本或记忆/晋升/归档机制才用 `--level full`。
- 如果 `garden.py` 给出候选，只把它当审查队列，不自动判定为可删。
