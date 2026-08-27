---
id: context-procedures-readme
tags: context, procedures, workflow
summary: 说明 procedures 目录用于沉淀可重复执行的标准流程、排查顺序和操作套路。
last_reviewed: 2026-08-07
memory_type: procedural
scope: repo
owners: scripts/context, docs/context
triggers: procedure, playbook, workflow, debug
evidence_level: design
status: active
---

# Procedures 目录说明

该目录用于沉淀跨任务可复用的标准做法，例如：

- 固件构建、烧录、monitor 闭环
- 常见配网/联网排查顺序
- UI 生命周期或板级 bring-up 排查套路
- 上下文系统自身的维护流程

适合进入本目录的内容：

- 多次任务里都会重复使用的步骤
- 需要固定顺序、前置条件或回退路径的操作流程
- 希望 agent 和人都按同一套路执行的排查清单

不适合进入本目录的内容：

- 单次实验记录
- 只对当前一个任务有效的临时计划
- 尚未验证的长期事实
