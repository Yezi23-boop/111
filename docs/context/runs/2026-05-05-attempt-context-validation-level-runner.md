---
id: attempt-2026-05-05-context-validation-level-runner
tags: context, run, attempt-log
summary: context validation level runner；结果：success。
last_reviewed: 2026-05-05
memory_type: episodic
scope: task
owners: scripts/context/validate_context.py, AGENTS.md, docs/context/README.md, docs/context/procedures/context-garden-policy.md, docs/context/evals/query-golden.yaml
triggers: context validation level runner
evidence_level: observed
record_reasons: framework-constraint, evidence
---

# Attempt Log: context validation level runner

## 背景

- 本次要验证什么：context validation level runner
- 对应任务或计划：未绑定计划
- 结果状态：success
- 长期记录理由：framework-constraint, evidence

## 环境

- 分支/工作区状态：未记录
- 设备/串口/板型：未涉及或未记录
- 关键前置条件：未记录

## 操作

- 修改过的文件或 owner：
- scripts/context/validate_context.py
- AGENTS.md
- docs/context/README.md
- docs/context/procedures/context-garden-policy.md
- docs/context/evals/query-golden.yaml
- 执行的命令或动作：
- 新增 validate_context.py，统一 light / standard / routing / full 四档上下文检索与验证
- light 只执行 query runs、query knowledge 和可选 brief pack；full 才执行 build_index/check/garden/eval_query
- 已尝试但不应直接重复的路径：
- 不要让普通代码任务默认跑完整四件套

## 观测

- 关键日志/证据：
- validate_context.py --level light --brief --dry-run 只打印 query/query/pack
- validate_context.py --level full 通过，eval_query 15/15
- 与预期不一致的点：
- 未记录

## 结论

- 本次可以确认的事实：未形成稳定结论
- 仍然不能确认的事实：
- 未记录

## 未验证风险

- 下一轮仍需补证据的边界：
- 后续上下文验证优先调用 validate_context.py，而不是手写四条命令
