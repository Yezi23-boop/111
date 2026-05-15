---
id: ui-management-prd
tags: context, plans, prd, ui, lvgl, gui-guider, ui-manager
summary: 已降级为历史 PRD 草案；最终采用 ADR-20260513 的轻量边界决策：GUI Guider 作为视觉主编辑器和 intent 入口生成器，当前不新增 ui_manager、bridge 或事件队列。
last_reviewed: 2026-05-15
memory_type: task
scope: repo
owners: main/ui, main/ui/custom, main/ui/generated, components/lvgl_port, main/features/alerts
triggers: ui-management, ui_manager, LVGL管理, GUI Guider, generated bridge, UI事件队列
evidence_level: design
status: superseded
superseded_by: docs/context/decisions/ADR-20260513-gui-guider-visual-editor-ui-runtime-boundary.md
---

# UI 管理收敛 PRD

> 当前文档已不再作为 active execution plan。最终沉淀结论见
> `docs/context/decisions/ADR-20260513-gui-guider-visual-editor-ui-runtime-boundary.md`。
> 当前不推进 `ui_manager`、`ui_generated_bridge` 或 UI 事件队列；仅保留本草案作为讨论背景。
> 2026-05-15 已移出 `plans/active/`，避免被后续 agent 当作待执行计划。

## Problem Statement

当前项目已经有可运行的 LVGL UI 主线，但 UI 管理职责分散在 GUI Guider 生成层、手写 controller、`lvgl_task` 主循环、显示告警适配器和刷新策略中。

用户希望重新梳理当前项目的 UI 管理方式，让后续新增页面、接入低功耗刷新、处理触摸恢复、接入危险识别/AI/Wi-Fi 等功能时，有清晰 owner、可回退边界和可验证路径，而不是每次都直接改生成文件或把业务逻辑塞进 LVGL 回调里。

当前痛点包括：

- GUI Guider 生成文件中混入业务入口，未来重新导出时容易丢失。
- 多个 hand-written controller 各自管理页面生命周期，缺少统一导航和事件入口。
- `lvgl_task` 同时承担 UI 初始化、后台 UI 轮询、LVGL handler 和刷新策略调度，后续继续增长会变成隐式总控。
- 触摸输入组件直接通知刷新策略，当前可用，但跨 component 边界偏硬。
- 后台功能已经存在“UI 线程处理 pending 请求”的雏形，但还没有统一事件队列或 UI facade。

## Solution

建立一个轻量 `ui_manager` 作为 UI 层稳定门面，保持现有单 UI 线程模型不变，把分散的 UI 初始化、页面导航、跨线程事件消费和周期性 UI 处理收敛到一个简单接口后面。

从用户视角看，完成后项目仍然保持当前界面行为：主界面、AI 页、危险识别页、Wi-Fi 管理页、亮度滑条、空闲降频和告警覆盖层都应继续工作。区别是后续新增或调整 UI 时，GUI Guider 继续作为页面视觉主编辑器；跨业务 owner 的动作不直接沉进 generated 业务逻辑，而是走 hand-written bridge 和 manager。

核心方向：

- 保留 `lvgl_task` 作为唯一 LVGL 主线程。
- 把 GUI Guider 定位为视觉主编辑器和 intent 入口生成器，而不是系统运行框架。
- GUI Guider generated 层可以保留控件树、样式、动画、资源、纯视觉跳转和 custom event 入口。
- 把业务状态、页面生命周期、导航策略放到 hand-written controller 和 `ui_manager`。
- 后台服务只发布状态或投递 UI 事件，不直接操作 LVGL。
- 刷新/亮度策略继续独立，不和导航管理混成一个大模块。
- 第一阶段 `ui_manager` 不接管 `lv_timer_handler()`、刷新策略轮询或 `vTaskDelay()` 编排；这些仍由 `lvgl_task` 作为 UI 调度 owner 串行处理。
- 第一阶段只做薄收敛，不引入大而全框架。

## User Stories

1. As a firmware developer, I want one clear UI owner, so that I know where LVGL object mutations are allowed.
2. As a firmware developer, I want GUI Guider generated files to remain mostly replaceable, so that future UI re-export does not erase business logic.
3. As a firmware developer, I want generated event callbacks to call a thin hand-written bridge, so that generated code only translates clicks and value changes.
4. As a firmware developer, I want a stable UI manager facade, so that page navigation and periodic UI updates do not keep growing inside the LVGL task.
5. As a firmware developer, I want background services to post UI events instead of touching LVGL directly, so that cross-task UI access remains safe.
6. As a firmware developer, I want the UI task to drain pending UI work in one place, so that watchdog and concurrency risks are easier to reason about.
7. As a UI feature developer, I want AI page entry to stay hand-written, so that GUI Guider does not need to own the official chat page.
8. As a UI feature developer, I want danger detection page entry to stay hand-written, so that model/service lifecycle remains outside generated callbacks.
9. As a UI feature developer, I want Wi-Fi and Bluetooth dropdown behavior to stay in the custom controller layer, so that network manager state remains the source of truth.
10. As a UI feature developer, I want brightness slider changes to go through a bridge, so that refresh policy and visual slider behavior stay coupled without living in generated code.
11. As a product user, I want touching the screen to immediately restore active refresh and brightness, so that the watch feels responsive after idle dim.
12. As a product user, I want idle dim and lower refresh to continue working, so that battery usage improves when I am not interacting.
13. As a product user, I want high-refresh pages to remain smooth when needed, so that animations, alerts or active views do not feel laggy.
14. As a product user, I want AI, danger detection and Wi-Fi pages to keep their current navigation behavior, so that the refactor does not change product behavior.
15. As a maintainer, I want page controllers to own their own lifecycle, so that cached LVGL object pointers are cleaned correctly when screens are deleted.
16. As a maintainer, I want a consistent event vocabulary, so that new features can request navigation, overlay updates, brightness changes or high-refresh mode without inventing one-off APIs.
17. As a maintainer, I want refresh policy to remain separate from UI navigation, so that power tuning can evolve without rewriting page management.
18. As a maintainer, I want display/touch port code to remain hardware-facing only, so that board porting does not depend on app-level UI modules.
19. As a maintainer, I want tests to catch generated-layer business coupling, so that accidental direct logic in generated callbacks is detected early.
20. As a maintainer, I want source-level tests around UI manager contracts, so that behavior can be checked without requiring board hardware for every change.
21. As a maintainer, I want board validation steps for navigation and idle dim, so that the refactor is proven on the CO5300/FT5x06 hardware path.
22. As a future agent, I want this PRD to define owner boundaries, so that I do not infer architecture only from current code shape.
23. As a future agent, I want re-export risk documented, so that I avoid putting durable behavior into generated screen setup files.
24. As a future agent, I want a minimal first slice, so that I can improve UI management without doing a broad rewrite.
25. As a UI designer, I want GUI Guider to remain the visual source of truth for screens, widgets, styles and simple visual transitions, so that visual iteration stays fast and tool-friendly.
26. As a system maintainer, I want GUI Guider events to emit product intents rather than own product state, so that embedded resource conflicts remain controlled by runtime owners.

## Implementation Decisions

- Keep a single LVGL foreground task as the only context that directly mutates LVGL objects.
- Introduce a light UI manager facade with initialization, periodic processing, page action dispatch and optional event posting APIs.
- The UI manager should be a deep module with a small interface: it hides controller polling, overlay processing and cross-task event draining behind stable calls.
- The first implementation slice should keep LVGL handler scheduling, refresh policy polling, refresh delay calculation and FreeRTOS delay orchestration in the LVGL task.
- Do not build a large resource manager or app framework as part of this PRD.
- GUI Guider remains the primary editor for visual facts: screens, widgets, styles, images, fonts, local visual animations and simple visual transitions.
- Generated files should own structure, style, static widgets, visual interaction details and custom event entrypoints.
- Generated callbacks may call hand-written bridge functions, but should not directly encode business policy, resource lifecycle, power policy, background service state or cross-task synchronization.
- The bridge layer should translate UI gestures into semantic actions such as opening AI, opening danger detection, opening Wi-Fi management, toggling Bluetooth, setting brightness or returning home.
- Existing AI, danger detection, Wi-Fi management and main dropdown controllers remain the page-level owners.
- Generated time page, wallpaper page, dropdown drag animation, brightness icon rotation and simple return-home animation are tolerated generated behavior in the first slice unless they block the manager facade. They should be documented as remaining generated coupling rather than silently treated as solved.
- If those generated behaviors become part of the cleanup scope, introduce explicit main-screen, dropdown, time and wallpaper bridge owners instead of moving them directly into the UI manager.
- The alert overlay path should be folded into the UI manager process loop without changing its current external behavior.
- The idle dim and refresh policy module remains independent and continues to own active/idle/force-active scheduling and brightness output.
- Touch activity should eventually be reported through a port-level hook or UI activity callback instead of app-level forward declaration from the input port.
- Event queue adoption should be incremental: first wrap existing polling/pending paths, then migrate external producers to queue-based UI events where useful.
- High-refresh force mode should remain available; if multiple future pages need it, evolve it into a lease-style API instead of a single global boolean.
- Screen deletion and cached object pointer invalidation remain mandatory for hand-written pages that use animated screen loading with deletion.
- The first implementation slice should preserve all current user-visible behavior.

## Testing Decisions

- Good tests should validate observable contracts: which public UI action routes to which manager/controller behavior, whether generated code stays thin, and whether refresh policy still maps activity states to delay/brightness decisions.
- Do not test LVGL internals or private implementation details of widget construction unless the behavior is externally meaningful.
- Source-level tests should cover that generated event callbacks forward to bridge/manager entrypoints rather than directly owning feature logic.
- When bridge entrypoints replace direct generated calls, update the existing source tests that currently assert direct AI, danger, Wi-Fi, Bluetooth or LVGL-task calls so that tests follow the new public contract instead of pinning the old coupling.
- Existing refresh policy source tests should continue to cover active delay, idle delay, force-active behavior, dim brightness and touch recovery semantics.
- Add or extend source tests for no direct LVGL calls from non-UI service paths where practical.
- Add source checks for UI manager process ownership: alert overlay processing, danger page polling and queued UI work should be reachable from the UI loop through the manager.
- Keep refresh policy tests focused on the refresh policy module and LVGL task scheduling contract; do not require refresh policy polling to be owned by the UI manager in the first slice.
- Board validation must still be done for screen navigation, touch restore, idle dim, brightness slider, AI page entry, danger detection page entry, Wi-Fi management page lifecycle and alert overlay.
- Build validation remains required after code changes.
- If `sdkconfig` changes are not involved, a normal build is enough; if `sdkconfig` changes are introduced later, run fullclean before build.

## Out of Scope

- Replacing GUI Guider or deleting the generated UI layer.
- Rebuilding the visual design of main screen, AI page, danger page or Wi-Fi page.
- Introducing deep sleep, light sleep or a full watch power state machine.
- Changing CO5300 panel initialization, FT5x06 touch initialization, display flush mode or hardware pin definitions.
- Reworking official chat, network manager, danger detection service or background service manager internals.
- Moving every existing controller into a new abstract page framework in one step.
- Moving generated time, wallpaper, dropdown drag, brightness icon rotation or simple return-home animation out of `events_init` in the first slice, unless a later task explicitly expands the cleanup scope.
- Moving LVGL handler scheduling, refresh policy polling or FreeRTOS delay orchestration out of the LVGL task in the first slice.
- Creating a host preview runner for this PRD unless a later UI visual change requires it.
- Publishing to an external issue tracker from this repository state; no issue tracker configuration or triage label vocabulary was found during this PRD capture.

## Further Notes

- Current project evidence already supports the intended shape: the LVGL task is the UI foreground owner, alert overlay uses pending flags consumed on the UI thread, and refresh policy is already separate from LVGL tick semantics.
- The final boundary is: GUI Guider is the visual editor and intent-entry generator; it is not the navigation bus, resource manager, power policy owner or product state machine.
- The biggest immediate cleanup target is cross-business generated callback ownership, not visual generated behavior.
- The safest first slice is a behavior-preserving wrapper: create the UI manager, move alert/controller pending work behind it, keep LVGL handler and refresh scheduling in the LVGL task, build, then only afterward thin out generated callbacks.
- Re-export safety matters because this repository has previously replaced GUI Guider generated output; durable product behavior should live outside generated files.
- This PRD is stored as an active context plan because no external issue tracker configuration was discoverable in the repository.

## Next Safe Step

- 不执行本 PRD 的原第一步。若未来触发 ADR 中的重评估条件，再重新创建 active plan。
