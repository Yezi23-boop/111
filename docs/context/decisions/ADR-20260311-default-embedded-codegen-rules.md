---
id: adr-default-embedded-codegen-rules
tags: decisions, rules, esp32, mcu, embedded, c-cpp
summary: 将 ESP32/MCU 嵌入式 C/C++ 工程规则提升为本仓库默认代码生成规范的决策记录。
last_reviewed: 2026-03-11
---
# ADR: 将嵌入式 C/C++ 工程规则提升为仓库默认规则

- Date: 2026-03-11
- Status: accepted
- Context:
  - 当前仓库是 `ESP32-S3 + ESP-IDF` 固件工程，主要任务集中在显示/UI、触摸输入、音频播放、Wi-Fi 配网和板级集成。
  - 仓库已引入 `docs/context` 上下文工作流，需要一套默认生效的嵌入式工程编码约束，减少后续新增规则时的分散解释成本。
  - 用户明确要求把 `ziji` 的上下文框架和规则迁入当前仓库，并尽量保持结构一致、内容适配当前项目。
- Decision:
  - 在 `AGENTS.md` 中新增“嵌入式 C/C++ 代码生成默认规范”入口，作为仓库默认规则的一部分。
  - 在 `docs/context/knowledge/project/embedded-c-cpp-engineering-rules.md` 中维护详细条款，覆盖分层、模块边界、复杂度、资源、安全、接口和算法/AI 模块拆分要求。
  - 该规范默认适用于本仓库中的代码生成、代码评审、重构和架构建议任务。
  - 该规范不覆盖更高优先级的 system / developer 指令，也不替代现有上下文检索流程或计划模式规则。
- Consequences:
  - 后续新增或修改代码时，应优先满足分层、边界、安全、资源和可维护性要求。
  - 涉及显示、触摸、音频、配网和板级驱动的改动，需要显式考虑 MCU 资源限制、错误路径和降级策略。
  - 算法或 AI 相关实现应默认拆分为预处理、推理、后处理和模型配置模块。
  - `AGENTS.md` 保持高层摘要，详细规范留在上下文文档中，便于长期维护和检索。
- Rollback Plan:
  - 若后续证明该规范默认生效范围过宽，可将 `AGENTS.md` 中的默认入口缩减为“仅嵌入式任务触发”，并保留详细文档供按需引用。
  - 若详细规则与实际项目节奏冲突，可在不破坏顶层结构的前提下收缩具体条款，保持 `AGENTS.md + 详细规则文档` 的双层结构不变。
