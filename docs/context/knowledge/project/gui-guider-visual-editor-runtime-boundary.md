---
id: gui-guider-visual-editor-runtime-boundary
tags: project, ui, lvgl, gui-guider, architecture, runtime-boundary
summary: 固定 GUI Guider 在当前仓库里的边界：它是视觉主编辑器和 intent 入口生成器，不是 UI runtime、导航总线、资源 owner 或产品状态机。
last_reviewed: 2026-05-16
memory_type: project_knowledge
scope: repo
owners: main/ui, main/ui/generated, main/ui/custom, main/ui/lvgl_task.c, main/features/alerts, components/lvgl_port
triggers: gui-guider, GUI Guider, LVGL, ui-management, events_init, ui_manager, generated bridge, UI事件队列, 视觉主编辑器
evidence_level: design
status: active
---

# GUI Guider 视觉编辑与 UI Runtime 边界

## 当前结论

GUI Guider 在当前项目里只作为视觉主编辑器和 intent 入口生成器使用。

它可以继续产出控件树、样式、资源、动画、静态页面结构和简单视觉跳转，但不成为系统运行框架，也不拥有资源生命周期、后台服务、功耗策略、跨任务同步或产品状态机。

## Generated 层可以拥有

- 页面结构、控件树、样式、图片、字体引用和局部视觉动画。
- 纯视觉跳转，例如不牵涉后台资源或跨模块状态的返回、展开、收起。
- custom event 入口，但入口只应表达用户 intent。
- 与具体控件强绑定的局部 UI 效果，例如图标旋转、下拉动画和静态页面切换。

## Generated 层不应拥有

- `official_chat`、危险识别、联网、音频、功耗、后台任务等产品状态机。
- 麦克风、喇叭、Wi-Fi、BLE、模型、DMA/internal RAM 等资源生命周期。
- 跨任务同步、队列、锁、服务启动停止、重试、退避或错误恢复。
- `power_policy`、`background_service_manager`、`danger_detection_service`、`network_manager` 等 runtime owner 的决策。

## 当前不新增的层

当前项目不默认新增：

- `ui_manager`
- `ui_generated_bridge`
- UI event queue
- 导航总线
- 只转发一跳的 UI wrapper

这些层只有在真实复杂度出现时才重新评估。不能因为 GUI Guider 生成文件存在，就反推必须增加一套 UI 框架。

## 现有运行时 owner

- `lvgl_task` 仍是唯一直接执行 LVGL 对象变更的 UI 线程 owner。
- hand-written controller 继续负责页面生命周期和业务 intent 翻译。
- service / manager 模块继续负责对应资源和状态机。
- `ui_refresh_policy` 只负责 UI 活跃度、刷新节流和亮度输出，不承担导航或业务状态。

## 重新评估触发条件

只有出现以下情况时，才考虑重新打开 UI runtime 重构计划：

- 多个页面都需要同一种跨线程 UI 事件投递，且现有 pending flag 已明显不可维护。
- generated callback 开始重复承载业务策略，且局部桥接无法压住复杂度。
- 页面导航、生命周期、对象缓存清理在多个 controller 中出现同类 bug。
- 后台服务需要统一向 UI 投递状态更新，但当前 UI 线程消费入口已经分散到不可追踪。

即便触发重评估，也应先新建 active plan，明确 owner、调用方向和验收证据，再做最小实现。
