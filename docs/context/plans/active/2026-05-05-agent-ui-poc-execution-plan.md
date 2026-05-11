---
id: agent-ui-poc-execution-plan
tags: context, plan, ui, lvgl, pc-sim, agent-ui, poc
summary: Agent 直接生成 LVGL C、支持 PC 预览与最小板端验证的 UI 并行路线 PoC 执行计划。
last_reviewed: 2026-05-05
memory_type: task
scope: task
owners: docs/context/plans/active, main/ui/lvgl_task.c
triggers: agent-ui, pc-sim, lvgl-preview, ui-poc, execution-plan
evidence_level: design
status: active
---

# Agent UI PoC 执行计划

## Purpose / Big Picture

- 任务目标：
  - 为当前仓库增加一条“`Agent -> LVGL C -> pc_sim 预览 -> ESP32-S3 最小板端验证`”的并行 UI 路线。
  - 让 Agent 不只画单页，而是能生成一套可预览的 `app shell` 骨架：`home + status overlay + detail + 返回链`。
- 为什么现在做：
  - 当前仓库已有 `GUI Guider + custom/controller` 主线，但缺少一条“快速生成、快速预览、再最小上板验证”的独立路线。
  - 该路线的核心价值不是替代现有主线，而是把“UI 形态迭代”和“板级显示/触摸约束验证”拆开。
- 完成后用户会看到什么变化：
  - 仓库内会出现一套新的 `Agent UI` 代码骨架和 `pc_sim` 预览运行时。
  - `pc_sim` 可运行一个最小 `app shell` PoC。
  - 固件侧可通过编译期开关切到这套 PoC，并完成至少一次最小板端构建/显示验证。

## Scope / Non-Goals

- 本轮明确要做：
  - 新增并行路线，不替代当前 `GUI Guider` 主线。
  - 共享 UI 源码放在 `main/ui/agent_ui/`。
  - `pc_sim/` 只负责预览运行时，不承载正式 UI owner。
  - Agent 直接生成 `LVGL C`，默认按 `page` 粒度重生成。
  - 做一个最小 `app shell` PoC：
    - `home`
    - `status overlay`
    - `detail`
    - 统一返回动作
  - `pc_sim` 跑通，并完成一次最小板端验证。
- 本轮明确不做：
  - 不替换 `main/ui/generated`、`main/ui/custom` 的现有正式主线。
  - 不设计通用 UI DSL / JSON / YAML 中间层。
  - 不第一版就做完整 design system、完整资产注册表、完整导航栈。
  - 不把所有板端状态都接成真实数据。
  - 不直接搬用现有 `generated/events_init.c` 的代码组织方式。

## Route Contract

- 路线定位：
  - 这是当前仓库的并行 UI 路线，不是对现有 UI 主线的替换。
- 设计输入优先级：
  - 视觉以设计稿为准。
  - 交互与页面流转以文字需求为准。
  - 参考图只作风格参考。
- 预览与真机的真相来源：
  - 布局、结构、页面流转以 `pc_sim` 为主。
  - 显示、触摸、性能、生命周期以板端为主。
- 生成边界：
  - `Agent-owned`：页面骨架、组件树、样式装配、可重生成的 view/page 文件。
  - `Human-owned`：`app_shell_nav`、`preview_state`、`ui_platform_adapter`、`home_entry_defs`、最少一层公共 token、薄入口与构建接线。

## Target Architecture

### 1. 目录落点

- 共享 UI 源码：`main/ui/agent_ui/`
- 预览运行时：`pc_sim/`
- 固件侧薄入口：放在 `main/ui` 现有 owner 体系内，通过编译期开关接入
- 代码旁说明：`main/ui/agent_ui/README.md`

### 2. 分层与 owner

- `component`
  - 纯视觉组件。
  - 只负责创建子树、布局、样式、局部纯视觉更新。
  - 不自己跳页，不直接接业务状态。
- `page`
  - 负责本页结构与本页事件转发。
  - 通过统一生命周期接口对外暴露。
- `app_shell_nav / shell_runtime`
  - 统一管理固定路由、当前页、返回动作、overlay 开关和轮询刷新节奏。
- `preview_state`
  - 统一提供 PC 预览假数据。
- `ui_platform_adapter`
  - 板端/PC 共享 UI 的唯一平台状态入口。
  - 真实状态只允许从这里进入共享 UI。

### 3. 容器模型

- 整套 PoC 只保留一个根 `screen`。
- `app shell` 常驻。
- `status overlay` 与壳层公共层常驻。
- `page` 只在内容容器里创建/销毁。
- `page` 进入时创建，离开时销毁，避免长期悬挂对象引用。

### 4. 页面生命周期接口

- 每个 page 默认提供统一入口：
  - `create`
  - `bind_events`
  - `apply_snapshot`
  - `destroy` 或等价退出入口
- 外部不直接到处抓 `lv_obj_t *` 改对象树。

### 5. Overlay 语义

- `status overlay` 属于 `app shell` 共享层，不属于 `home page`。
- `home` 只负责触发它，不拥有其本体。
- `overlay` 开关状态由 `shell_state/nav` 统一控制。
- `overlay` 内容由 `preview_state` 或板端统一快照提供。
- 返回语义：
  - 只要 `overlay` 打开，返回优先关闭 `overlay`，不直接退页。
- 第一版手势语义：
  - 借现有 `main/ui` 的“拖拽跟手 + 松手吸附”语义。
  - 采用两态：`closed / open`
  - 不直接复用旧 `generated` 文件结构。

### 6. 导航模型

- 第一版只做固定路由，不做通用导航栈。
- `detail` 通过小而强类型的上下文进入：
  - `detail_kind`
  - 少量展示字段，例如 `title / subtitle / icon_id`

### 7. Home 入口定义

- `home` 至少包含 `2~3` 个入口：
  - 至少两张同构卡片
  - 至少一个异构入口
- `home` 页面只负责渲染和事件转发。
- 入口定义表 `home_entry_defs` 为 `Human-owned`。
- 契约策略：
  - 先有最小必填字段
  - 再留少量可选字段
- 契约落地方式：
  - 代码里有真实结构体
  - 代码旁 README 有简短字段说明

## Implementation Phases

### Phase 0: 骨架先行

- 第一刀先落 `app_shell` 根骨架，而不是先落单页或单独 `pc_sim`。
- 先建立真实目录和真实接口：
  - `main/ui/agent_ui/`
  - `pc_sim/`
  - `app_shell_nav`
  - `shell_runtime`
  - `preview_state`
  - `ui_platform_adapter`

### Phase 1: 壳层与固定路由

- 建立单根 `screen` 与常驻 `shell` 容器。
- 建立内容容器、overlay 容器、统一返回动作。
- 建立 `home <-> detail` 固定路由。

### Phase 2: Home / Detail / Overlay PoC

- `home`：
  - 从 `home_entry_defs` 读取入口定义。
  - 渲染至少 `2~3` 个入口。
- `detail`：
  - 使用统一骨架。
  - 按 `detail_context` 展示不同标题、图标和说明。
- `status overlay`：
  - 走 `closed/open` 两态。
  - 支持拖拽跟手和松手吸附。

### Phase 3: Preview Runtime

- `pc_sim` 建立最小运行时。
- 统一用 `preview_state` 提供假数据。
- 保证最小 `app shell` 可预览、可点击、可返回、可拉出 overlay。

### Phase 4: 最小板端接入

- 增加编译期开关，例如 `CONFIG_AGENT_UI_POC`。
- 开关开时切到 Agent UI PoC。
- 板端只接一部分真实状态，优先：
  - `Wi-Fi`
  - `BLE`
- 所有真实状态仍只允许经 `ui_platform_adapter` 进入。

## Progress

- `[x]` 已完成：口头架构边界收敛
- `[x]` 已完成：PoC 范围、owner、验证模型收敛
- `[ ]` 待开始：创建 `main/ui/agent_ui/` 骨架
- `[ ]` 待开始：创建 `pc_sim/` 预览运行时骨架
- `[ ]` 待开始：定义 page 生命周期接口与 `home_entry_defs` 契约
- `[ ]` 待开始：落 `home + status overlay + detail` 最小 PoC
- `[ ]` 待开始：接入编译期开关并完成最小板端验证

## Decision Log

- 2026-05-05：该路线定位为并行路线，不替代现有 `GUI Guider` 主线。
- 2026-05-05：共享 UI 源码放 `main/ui/agent_ui/`，`pc_sim/` 只放预览运行时。
- 2026-05-05：Agent 直接生成 `LVGL C`，默认按 `page` 粒度重生成。
- 2026-05-05：组件纯视觉；page 负责结构和事件转发；`shell/nav` 管导航与 overlay。
- 2026-05-05：PoC 采用单根 `screen` + 常驻 `app shell` + 内容容器替换 page 的模式。
- 2026-05-05：`status overlay` 属于共享 shell 层，采用 `closed/open` 两态，借现有手势语义但不复用旧代码组织。
- 2026-05-05：真实状态只允许经 `ui_platform_adapter` 进入共享 UI。
- 2026-05-05：`home_entry_defs` 为 `Human-owned`，页面骨架为 `Agent-owned`。
- 2026-05-05：第一版状态更新走低频轮询，由 `shell_runtime` 统一拉快照。
- 2026-05-05：板端接入采用编译期开关，最小真实状态优先接 `Wi-Fi/BLE`。

## Surprises & Discoveries

- 当前仓库 `main/ui/generated/events_init.c` 已有可借用的顶部抓手下拉语义，但其代码组织不适合作为新路线模板。
- 当前仓库已有 `main_dropdown_controller` 等状态同步 owner，可作为新路线板端接线的语义参考。
- 当前仓库历史上已经踩过 `LVGL` screen 删除后的悬空对象风险，因此 PoC 第一版默认避免“多 screen + 长期缓存对象指针”模型。

## Validation and Acceptance

### 计划运行的验证命令

- `pc_sim` 侧：
  - `cmake ..`
  - `mingw32-make`
  - 运行模拟器可执行文件
- 固件侧：
  - 先确认 `export.ps1`
  - `idf.py build`
  - 必要时 `idf.py -p COMx flash`
  - 必要时 `idf.py -p COMx monitor`

### 期望看到的结果

- `pc_sim`：
  - 最小 `app shell` 成功运行
  - `home` 成功显示
  - `status overlay` 可拖拽、吸附、关闭
  - 至少 `2~3` 个入口能进入统一 `detail`
  - 页面返回与 overlay 返回优先级正确
- 固件：
  - 开启编译期开关后可成功构建
  - 板端能进入 Agent UI PoC
  - 至少一部分真实 `Wi-Fi/BLE` 状态可经 adapter 显示到 overlay

### 当前实际结果

- 当前仅完成执行计划收敛和文档落地，代码尚未开始。

## Idempotence and Recovery

- 如果中途中断，下次从哪里继续：
  - 先从 `Phase 0` 的骨架创建继续，不需要重新讨论架构边界。
  - 所有新增代码先按本计划的目录与 owner 落位。
- 如果本轮方案失败，最小回退路径是什么：
  - 不改现有正式主线。
  - 关闭 `CONFIG_AGENT_UI_POC` 或对应编译期开关即可回到当前 UI 主线。
  - `pc_sim/` 与 `main/ui/agent_ui/` 可独立迭代，不影响现有产品路径。

## Deliverables

- 代码：
  - `main/ui/agent_ui/` 最小共享 UI 骨架
  - `pc_sim/` 最小预览运行时
  - 固件侧编译期开关入口
- 文档：
  - `main/ui/agent_ui/README.md`
  - 本计划文档
  - 一份简短的下一阶段扩展说明

## Next Step

- 下一步最小动作：
  - 先创建 `main/ui/agent_ui/` 顶层骨架与 `app_shell` 根接口，再补 `pc_sim/` 空运行时。
