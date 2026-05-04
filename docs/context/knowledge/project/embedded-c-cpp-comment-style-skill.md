---
id: embedded-c-cpp-comment-style-skill
tags: project, skill, embedded, c-cpp, comments, doxygen
summary: 仓库新增 embedded-c-cpp-comment-style 技能，用于约束 agent 为嵌入式 C/C++ 代码生成说明型注释，强调 Doxygen 函数头、原因说明和关键变量/常量解释。
last_reviewed: 2026-04-15
memory_type: procedural
scope: repo
owners: docs/context/knowledge/project/embedded-c-cpp-comment-style-skill.md, AGENTS.md
triggers: embedded, c, cpp, comment, style, skill
evidence_level: design
---
# embedded-c-cpp-comment-style 技能

## 位置

- 技能目录：`.agents/skills/embedded-c-cpp-comment-style`
- UI 元数据：`.agents/skills/embedded-c-cpp-comment-style/agents/openai.yaml`

## 目标

- 约束 agent 在嵌入式 `C/C++` 代码中采用“说明型”注释风格。
- 默认要求函数使用 `Doxygen` 风格函数头注释。
- 默认要求复杂代码块补“原因说明”注释，而不是逐行翻译代码。
- 默认要求关键变量、共享变量、协议常量和魔法数字必须解释用途、单位、来源或风险。

## 核心规则

- 注释优先解释为什么这样做、受什么约束、改动后会带来什么风险。
- 禁止“设置标志位”“调用初始化函数”“计数器加一”这类表面动作注释。
- 对共享状态，必须说明读写方、运行上下文和一致性保护方式。
- 对超时、阈值、采样率、分包长度、重试次数等值，必须解释单位和设计理由。
- 默认只处理注释；若注释无法解决可读性问题，必须先获得用户明确允许，才可做最小重构。
- `/** ... */` 仅用于 `Doxygen` 文档注释；实现内部普通说明注释优先允许使用 `//`，多行原因说明可使用 `/* ... */`。

## 依据

- 该 skill 的规则依据来自 `ESP-IDF`、`Zephyr`、`Linux kernel`、`RTEMS`、`Google C++ Style Guide` 和 `C++ Core Guidelines`。
- 详细来源保存在 `.agents/skills/embedded-c-cpp-comment-style/references/authoritative-sources.md`。

## 按 skill-creator 补强项

- 已将 frontmatter 描述扩展为更强的触发描述，覆盖“补注释”“统一注释风格”“审查注释质量”等使用场景。
- 已补 `agents/openai.yaml`，为技能列表和快捷调用提供 `display_name`、`short_description` 与 `default_prompt`。
- 已使用 `skill-creator/scripts/quick_validate.py` 对技能目录做快速校验，当前通过。

## 适用边界

- 适用于嵌入式驱动、协议处理、状态机、RTOS 任务、ISR 协作和一般 MCU/ESP-IDF `C/C++` 代码。
- 若仓库或用户对注释语言、函数文档粒度有更高优先级要求，应以更高优先级规则为准。
