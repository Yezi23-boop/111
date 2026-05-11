# 上下文包

- 生成时间(UTC): 2026-05-07T18:30:24.144525+00:00
- 查询: AGENTS.md 定位 低 token 入口 规则冲突 注释规范
- 范围: mixed
- 模式: brief
- 包含导航文档: False
- 低 token 规则: 先读本包；只有命中项确实需要证据时再打开原文。

## 命中文档

1. `docs/context/knowledge/project/embedded-c-cpp-engineering-rules.md` (score=55)
   - 标题: ESP32/MCU 嵌入式 C/C++ 工程规则
   - 摘要: 本仓库默认采用的 ESP32/MCU 嵌入式 C/C++ 工程规则，覆盖 App/UI、Service、Manager/Domain、Driver Adapter、Vendor/SDK 分层、接口、复杂度、资源、安全和算法/AI 模块边界。
2. `docs/context/decisions/ADR-20260311-default-embedded-codegen-rules.md` (score=50)
   - 标题: ADR: 将嵌入式 C/C++ 工程规则提升为仓库默认规则
   - 摘要: 将 ESP32/MCU 嵌入式 C/C++ 工程规则提升为本仓库默认代码生成规范的决策记录。
3. `docs/context/knowledge/project/embedded-c-cpp-comment-style-skill.md` (score=47)
   - 标题: embedded-c-cpp-comment-style 技能
   - 摘要: 仓库新增 embedded-c-cpp-comment-style 技能，用于约束 agent 为嵌入式 C/C++ 代码生成说明型注释，强调 Doxygen 函数头、原因说明和关键变量/常量解释。
4. `docs/context/knowledge/project/agent-operational-rules.md` (score=44)
   - 标题: Agent 执行型详细规则
   - 摘要: 当前仓库面向 agent 的执行型详细规则，覆盖 IDF 环境、build/flash/monitor、硬件安全边界、UI 状态读取原则和项目专项默认实践。
5. `docs/context/knowledge/project/hearing-assist-danger-alert-parameter-defaults-table.md` (score=43)
   - 标题: 听障危险提醒参数与默认值建议表
   - 摘要: 面向听障用户的手表端危险提醒功能参数与默认值建议表，统一收敛产品常量、规则固定数值可调项、可调默认值，以及用户可配置项。

## Brief Context

- `docs/context/knowledge/project/embedded-c-cpp-engineering-rules.md` score=55 type=procedural evidence=design status=active
  title: ESP32/MCU 嵌入式 C/C++ 工程规则
  summary: 本仓库默认采用的 ESP32/MCU 嵌入式 C/C++ 工程规则，覆盖 App/UI、Service、Manager/Domain、Driver Adapter、Vendor/SDK 分层、接口、复杂度、资源、安全和算法/AI 模块边界。
  owners: AGENTS.md, docs/context/knowledge/project/embedded-c-cpp-engineering-rules.md
  snippet L7: - 若与更高优先级的 system、developer 或仓库顶层规则冲突，以更高优先级规则为准。

- `docs/context/decisions/ADR-20260311-default-embedded-codegen-rules.md` score=50 type=procedural evidence=design status=active
  title: ADR: 将嵌入式 C/C++ 工程规则提升为仓库默认规则
  summary: 将 ESP32/MCU 嵌入式 C/C++ 工程规则提升为本仓库默认代码生成规范的决策记录。
  owners: docs/context/decisions
  snippet L10: - 在 `AGENTS.md` 中新增“嵌入式 C/C++ 代码生成默认规范”入口，作为仓库默认规则的一部分。

- `docs/context/knowledge/project/embedded-c-cpp-comment-style-skill.md` score=47 type=procedural evidence=design status=active
  title: embedded-c-cpp-comment-style 技能
  summary: 仓库新增 embedded-c-cpp-comment-style 技能，用于约束 agent 为嵌入式 C/C++ 代码生成说明型注释，强调 Doxygen 函数头、原因说明和关键变量/常量解释。
  owners: docs/context/knowledge/project/embedded-c-cpp-comment-style-skill.md, AGENTS.md
  snippet L27: - 详细来源保存在 `.agents/skills/embedded-c-cpp-comment-style/references/authoritative-sources.md`。

- `docs/context/knowledge/project/agent-operational-rules.md` score=44 type=procedural evidence=design status=active
  title: Agent 执行型详细规则
  summary: 当前仓库面向 agent 的执行型详细规则，覆盖 IDF 环境、build/flash/monitor、硬件安全边界、UI 状态读取原则和项目专项默认实践。
  owners: AGENTS.md, docs/context/knowledge/project/agent-operational-rules.md
  snippet L4: 本卡承接 `AGENTS.md` 中不适合长期堆在入口文件里的详细规则。默认只在相关任务触发时按需阅读。


> 已打包片段数: 4/5，片段字符预算: 1800