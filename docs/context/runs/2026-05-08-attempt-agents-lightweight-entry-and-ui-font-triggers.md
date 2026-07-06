---
id: attempt-2026-05-08-agents-lightweight-entry-and-ui-font-triggers
tags: context, run, attempt-log
summary: agents lightweight entry and ui font triggers；结果：success。
last_reviewed: 2026-05-08
garden_status: keep-evidence
garden_reviewed: 2026-05-16
memory_type: episodic
scope: task
owners: AGENTS.md, scripts/context/validate_context.py, scripts/context/pack_context.py, docs/context/INDEX.agent.md, components/official_chat/AGENTS.md, components/traffic_inference/AGENTS.md, docs/context/README.md, docs/context/archive/handoffs/current-repo-state.md, docs/context/CHANGELOG.md, docs/context/knowledge/project/project-profile.md, docs/context/knowledge/project/embedded-c-cpp-engineering-rules.md, docs/context/evals/query-golden.yaml
triggers: agents lightweight entry and ui font triggers
evidence_level: observed
record_reasons: framework-constraint, evidence
---

# Attempt Log: agents lightweight entry and ui font triggers

## 背景

- 本次要验证什么：agents lightweight entry and ui font triggers
- 对应任务或计划：未绑定计划
- 结果状态：success
- 长期记录理由：framework-constraint, evidence

## 环境

- 分支/工作区状态：未记录
- 设备/串口/板型：未涉及或未记录
- 关键前置条件：未记录

## 操作

- 修改过的文件或 owner：
- AGENTS.md
- scripts/context/validate_context.py
- scripts/context/pack_context.py
- docs/context/INDEX.agent.md
- components/official_chat/AGENTS.md
- components/traffic_inference/AGENTS.md
- docs/context/README.md
- docs/context/archive/handoffs/current-repo-state.md
- docs/context/CHANGELOG.md
- docs/context/knowledge/project/project-profile.md
- docs/context/knowledge/project/embedded-c-cpp-engineering-rules.md
- docs/context/evals/query-golden.yaml
- 执行的命令或动作：
- AGENTS 当前仓库规则收敛为 validate_context.py --level light --brief 单入口，移除 query.py/pack_context.py 双入口暴露
- AGENTS 注释规范压缩为根红线，详细注释规则继续指向 embedded-c-cpp-engineering-rules.md
- 专项触发词补 main/ui/custom、ai_chat_view、ui_runtime_fonts、ui_chinese_fonts、fonts
- CLAUDE 最高优先级显式放在仓库冲突裁决之前，避免被“最小可运行改动”裁决语句削弱
- 根 AGENTS 中 IDF/monitor/hardware 细则压缩为红线索引，详细流程继续由 agent-operational-rules.md 承接
- INDEX.agent.md 改为 validate_context.py --level light --brief 单入口，query.py/pack_context.py 只作为实现细节或高级调试路径
- official_chat / traffic_inference 局部 AGENTS 改为先 light 检索，再按命中打开模块原文卡
- embedded-c-cpp-engineering-rules.md 同步收窄 Doxygen 默认范围为公开接口、跨模块接口或非显然函数
- 宽泛 fonts 触发词收窄为 main/ui/custom/fonts、ui_chinese_fonts、ui_runtime_fonts、lv_font_
- pack_context.py 增加 --no-write，validate_context.py light --brief 默认只打印 brief，不再写 context/pack/context-pack.md
- README.md 与 current-repo-state.md 的默认维护入口同步为 validate_context 分级流程，query/pack 只作为高级调试或实现细节
- 旧 BLE notify flush / NimBLE host task 诊断卡标记为 superseded，并指向当前 network_provisioning_adapter 主线
- project-profile.md owners 改回自身，避免 curator 误判 repo-overview 是维护入口
- 已尝试但不应直接重复的路径：
- 不要在 AGENTS 同时维护 query/pack 和 validate_context 两套普通任务入口
- 不要要求“验证分级”查询必须 top3 命中制度卡；具体 attempt 命中靠前是有价值的防重复信号
- 不要在局部 AGENTS 的 Read First 中默认列 repo-overview.md 和多张原文卡；应先 light 检索再按命中打开
- 不要让普通 light brief 写入 context/pack/context-pack.md；首读检索应保持只读

## 观测

- 关键日志/证据：
- validate_context.py --level routing 通过，eval_query 15/15
- 修复 review findings 后再次运行 validate_context.py --level routing 通过，eval_query 15/15
- 修复 scripts/context 只读 brief 与 BLE 旧卡后，需要使用 validate_context.py --level full 验证
- 与预期不一致的点：
- 新增 attempt 记录会把 `context-garden-policy.md` 从 top3 挤到 top5，因此 golden 断言改为 expected_top5

## 结论

- 本次可以确认的事实：AGENTS 普通任务入口应只暴露 `validate_context.py --level light --brief`，详细 query/pack 细节留给脚本实现；入口文件只保留优先级、红线、低 token 路由和专项触发词，避免重复规则漂移；普通 light brief 应只读输出，不应制造 context/pack 工作区噪声。
- 仍然不能确认的事实：
- 未验证板端代码影响，本次只改 agent/context 规则。

## 未验证风险

- 下一轮仍需补证据的边界：
- 后续 UI 字体或 AI Chat 任务应触发项目专项规则，并优先读取对应 context 卡
