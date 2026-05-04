---
id: attempt-2026-05-04-context-attempt-log-anti-repeat
tags: context, attempt-log, anti-repeat
summary: context-attempt-log-anti-repeat；结果：success。
last_reviewed: 2026-05-04
memory_type: episodic
scope: task
owners: AGENTS.md, scripts/context/log_attempt.py, docs/context/runs/attempt-template.md, docs/context/runs/README.md, docs/context/README.md, docs/context/evals/query-golden.yaml, scripts/context/eval_query.py
triggers: agent 做过什么 修改 尝试 避免重复 context attempt
evidence_level: observed
---

# Attempt Log: context-attempt-log-anti-repeat

## 背景

- 本次要验证什么：记录本轮上下文系统防重复机制，避免后续继续只扩展知识卡而没有记录 agent 做过的尝试。
- 对应任务或计划：context attempt log anti-repeat system
- 结果状态：success

## 环境

- 分支/工作区状态：未记录
- 设备/串口/板型：未涉及或未记录
- 关键前置条件：未记录

## 操作

- 修改过的文件或 owner：
- AGENTS.md
- scripts/context/log_attempt.py
- docs/context/runs/attempt-template.md
- docs/context/runs/README.md
- docs/context/README.md
- docs/context/evals/query-golden.yaml
- scripts/context/eval_query.py
- 执行的命令或动作：
- 新增 log_attempt.py 快速生成 runs attempt 记录
- AGENTS.md 非简单任务流程改为先 query.py --scope runs 查历史尝试
- eval_query.py 支持单条 query 的 scope 覆盖，用 runs 层评测 attempt 入口
- 已尝试但不应直接重复的路径：
- 不要只依赖 CHANGELOG 追踪 agent 尝试，CHANGELOG 太长且不表达不要重复什么
- 不要把失败尝试写进稳定 knowledge 卡当成通用事实

## 观测

- 关键日志/证据：
- build_index.py 索引 87 文件，check.py 错误 0 警告 0，garden.py 警告 0，eval_query.py 7/7 通过
- 与预期不一致的点：
- 未记录

## 结论

- 本次可以确认的事实：防重复动作的主入口应是 runs/attempt；knowledge 继续承载稳定结论，handoff 承载当前交接。
- 仍然不能确认的事实：
- 历史 attempt 回填覆盖率仍不足，需要继续补最近高风险任务

## 未验证风险

- 下一轮仍需补证据的边界：
- 优先回填 Wi-Fi、SoftAP、official_chat、AXP2101、ESP-DL 和 audio codec 的真实 attempt 记录
