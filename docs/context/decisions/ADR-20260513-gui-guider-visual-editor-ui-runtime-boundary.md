---
id: adr-gui-guider-visual-editor-ui-runtime-boundary
tags: decisions, ui, lvgl, gui-guider, architecture, esp-idf
summary: 固定 GUI Guider 在当前仓库中作为页面视觉主编辑器和 intent 入口生成器，而不是系统运行框架；业务状态、资源生命周期和跨任务 UI 更新继续由手写 runtime owner 管理。
last_reviewed: 2026-05-13
memory_type: procedural
scope: repo
owners: main/ui, main/ui/generated, main/ui/custom, main/ui/lvgl_task.c, main/features/alerts, components/lvgl_port
triggers: gui-guider, GUI Guider, LVGL, ui-management, events_init, ui_manager, generated, custom, 视觉主编辑器
evidence_level: design
---

# ADR: GUI Guider 作为视觉主编辑器，而不是系统运行框架

- Date: 2026-05-13
- Status: accepted
- Context:
  - 当前仓库使用 GUI Guider 生成 `main/ui/generated` 下的 screen、控件树、样式、动画和事件绑定。
  - 当前仓库也存在手写 UI runtime：`main/ui/lvgl_task.c` 负责唯一 LVGL 主线程与 `lv_timer_handler()` 调度，`main/ui/custom/*` 负责 AI、危险识别、Wi-Fi 管理和下拉菜单等业务 UI。
  - `main/ui/generated/events_init.c` 中已有若干一行式业务入口，例如打开 AI 页、危险识别页、Wi-Fi 管理入口、BLE 开关和亮度设置。
  - 这些入口目前大多仍是简单转发，尚未恶化到必须新增 `ui_generated_bridge`、`ui_manager` 或事件队列。
  - 用户已明确倾向：不要为了“更架构化”而增加一层极薄 bridge；先保留简单直接的现状。
- Decision:
  - GUI Guider 在当前仓库中定位为“页面视觉主编辑器 + intent 入口生成器”。
  - GUI Guider/generated 层可以继续拥有：
    - screen 与控件树；
    - 样式、图片、字体和静态资源；
    - 局部视觉动画；
    - 纯视觉页面跳转；
    - custom event 入口；
    - 当前仍为一行转发的 controller 调用。
  - GUI Guider/generated 层不应拥有：
    - 产品状态机；
    - 资源生命周期；
    - FreeRTOS 任务/队列编排；
    - `lv_timer_handler()` 调度策略；
    - 低功耗/亮度/刷新频率策略；
    - Wi-Fi/BLE、AI、危险识别、音频、模型等后台服务生命周期；
    - 错误恢复和跨任务同步。
  - `lvgl_task` 继续作为唯一 LVGL 主线程 owner，继续编排 `lv_timer_handler()`、`ui_refresh_policy` 和 `vTaskDelay()`。
  - `main/ui/custom/*` 与 feature/service owner 继续承担 AI、危险识别、Wi-Fi、告警 overlay 等真实业务行为。
  - 当前不新增 `ui_generated_bridge`、不新增 `ui_manager`、不新增 UI 事件队列。
  - `events_init.c` 可以保留一行调用 controller 的入口，但禁止继续沉入复杂业务判断、timer、队列、资源仲裁或后台服务编排。
- Trigger Conditions:
  - 只有当 `events_init.c` 开始出现多行业务逻辑、状态判断、资源生命周期或后台服务编排时，才重新评估是否需要 `ui_generated_bridge`。
  - 只有当 GUI Guider 重导出多次冲掉关键业务入口，并且维护成本高于一层 bridge 时，才重新评估是否需要 `ui_generated_bridge`。
  - 只有当 `lvgl_task` 主循环中的 pending work 明显膨胀到难读、难测或容易误改时，才重新评估是否需要 `ui_manager_process()`。
  - 只有当后台任务到 UI 的跨线程请求明显增多，并且现有 pending flag 模式不足以表达顺序和数据载荷时，才重新评估 UI 事件队列。
- Consequences:
  - 当前实现保持简单，避免为了少量一行转发引入新的抽象层。
  - GUI Guider 仍是视觉编辑的主入口，后续视觉迭代不需要绕开 GUI Guider。
  - 后续 agent 不能把“UI 管理 PRD”理解成马上要实现 `ui_manager` 或 bridge。
  - 若未来触发上述条件，可以基于本 ADR 再开新的 active plan，而不是在当前上下文中默认重构。
- Rollback Plan:
  - 若后续发现 generated 直接调用 controller 带来实际维护问题，可新增极薄 `ui_generated_bridge`，但 bridge 只允许做 intent 转发，不持有状态、不读 service、不管理 timer。
  - 若后续 `lvgl_task` 主循环膨胀，可新增 `ui_manager_process()` 只收敛 pending UI work，不接管 LVGL handler、刷新策略或 FreeRTOS delay。
