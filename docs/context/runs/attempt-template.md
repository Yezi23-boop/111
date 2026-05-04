---
id: context-attempt-template
tags: context, runs, attempt-log, anti-repeat, template, agent
summary: agent 做过什么修改和尝试的记录模板，重点记录已改文件、已尝试路径、验证证据和避免重复的动作。
last_reviewed: 2026-05-05
memory_type: episodic
scope: task
owners: docs/context/runs, scripts/context/log_attempt.py
triggers: attempt-log, anti-repeat, tried, failed, repeated-action, agent, 做过什么, 修改, 尝试, 避免重复
evidence_level: design
record_reasons: repeat-risk, evidence
---

# Attempt Log 模板

## 背景

- 本次要验证什么：
- 对应任务或计划：
- 结果状态：`success | partial | failed | abandoned | superseded`
- 长期记录理由：`repeat-risk | high-cost | owner-architecture | evidence | handoff | plan-decision | project-knowledge | framework-constraint`

## 环境

- 分支/工作区状态：
- 设备/串口/板型：
- 关键前置条件：

## 操作

- 修改过的文件或 owner：
- 执行的命令或动作：
- 已尝试但不应直接重复的路径：

## 观测

- 关键日志/证据：
- 与预期不一致的点：

## 结论

- 本次可以确认的事实：
- 仍然不能确认的事实：

## 未验证风险

- 下一轮仍需补证据的边界：
