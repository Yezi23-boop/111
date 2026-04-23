---
id: embedded-framework-mentor-skill
tags: [project, skill, architecture, embedded, esp-idf, mentoring]
summary: 仓库新增 `embedded-framework-mentor` 技能，用于在当前 ESP-IDF 项目中以“项目真实边界 + 外部权威原则”的双基线指导新功能设计、重构、评审和架构归属判断。
last_reviewed: 2026-04-23
---

# embedded-framework-mentor 技能

## 位置

- 技能目录：`.agents/skills/embedded-framework-mentor`
- UI 元数据：`.agents/skills/embedded-framework-mentor/agents/openai.yaml`

## 目标

- 让 agent 在当前 ESP32-S3 / ESP-IDF 项目里，优先从“模块边界”和“owner 归属”角度回答问题。
- 适用于：
  - 新功能设计
  - 模块重构
  - 架构评审
  - 跨模块 bug 定位
  - 判断某项改动该落在哪层
- 面向新手，默认中文输出，先解释“为什么这样分层”，再解释“这次怎么做”。

## 双基线

### 项目真实边界

- 优先以当前仓库和上下文库结论为准：
  - `network_manager`
  - `wifi_control`
  - `network_provisioning_adapter`
  - `ap_portal_adapter`
  - `network_service`
  - `main/ui`

### 外部工程原则

- 用官方文档和权威白皮书解释：
  - 为什么这样分层
  - owner-first 的判断方法
  - HAL / Driver / Service / App 的典型职责
  - 状态机、资源约束和可验证性为什么会反过来影响架构

## 文件结构

- `SKILL.md`
  - 定义触发条件、默认工作顺序、默认输出结构和新手友好规则
- `references/current-project-boundaries.md`
  - 收敛当前仓库真实模块边界
- `references/framework-principles.md`
  - 收敛外部权威原则
- `references/decision-checklist.md`
  - 提供“该落哪层”的判断清单
- `references/learning-path.md`
  - 面向当前项目的新手学习顺序
- `references/provisioning-refactor-case.md`
  - 用配网重做做真实案例

## 默认回答方式

skill 要求 agent 默认按下面结构回答：

1. 当前目标
2. 建议归属层
3. 不建议修改的层
4. 最小实现路径
5. 风险与验证

## 适用边界

- 当前 skill 最适合网络、服务编排、UI 语义、模块 owner、状态机这类架构问题。
- 它不是“覆盖所有嵌入式知识”的通用教程，而是优先帮助当前项目把架构判断做稳定、做清楚。
