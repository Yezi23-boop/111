---
id: 2026-04-01-lvgl93-generated-ui-replacement-design
tags: [spec, lvgl, gui-guider, ui, official-chat, esp32s3]
summary: 用桌面新导出的 GUI Guider 源替换仓库 generated 层和配套 custom 文件，并将仓库 LVGL 升级到 9.3，同时保留现有 hand-written AI 聊天气泡页桥接。
last_reviewed: 2026-04-01
---

# 目标

将 `C:\Users\ye\Desktop\src` 中新生成的 GUI Guider UI 引入当前仓库，用它替换仓库现有的 generated 层和 GUI Guider 配套 custom 文件，并把仓库 `lvgl/lvgl` 从 `9.2.2` 升级到 `9.3.x`。  
升级后，主菜单中的 AI 入口仍然必须跳转到现有 hand-written AI 聊天气泡页，而不是改回 GUI Guider 内部页面。

# 当前状态

- 当前仓库 GUI Guider 层位于：
  - `main/ui/generated`
  - `main/ui/custom/custom.c`
  - `main/ui/custom/custom.h`
  - `main/ui/custom/lv_conf_ext.h`
  - `main/ui/custom/clock_functions.*`
  - `main/ui/custom/scroll_functions.*`
- 当前 hand-written AI 层位于：
  - `main/ui/custom/ai_ui_controller.*`
  - `main/ui/custom/ai_chat_view.*`
  - `main/ui/custom/ui_font_assets.*`
  - `main/ui/custom/cbin_font_bridge.*`
- 当前 AI 主菜单入口桥接依赖 generated 事件层中 `ai_ui_open()` 的调用。
- 当前仓库 `main/idf_component.yml` 依赖 `lvgl/lvgl: 9.2.2`。

# 范围

本轮只做以下事情：

- 用桌面新导出源替换 `main/ui/generated`
- 替换 GUI Guider 配套 custom 文件：
  - `custom.c`
  - `custom.h`
  - `lv_conf_ext.h`
  - `clock_functions.*`
  - `scroll_functions.*`
- 将 `main/idf_component.yml` 中的 `lvgl/lvgl` 升级到 `9.3.x`
- 修复因 generated 层替换和 LVGL 升级引起的最小桥接/构建问题
- 保持主菜单 AI 入口继续跳转到 hand-written AI 页面

本轮明确不做：

- 不删除现有 hand-written AI 页面
- 不把 AI 页面改回 GUI Guider 页面
- 不重做 official_chat 逻辑
- 不重构 `lvgl_port` 架构

# 文件所有权边界

## 可替换文件

- `main/ui/generated/**`
- `main/ui/custom/custom.c`
- `main/ui/custom/custom.h`
- `main/ui/custom/lv_conf_ext.h`
- `main/ui/custom/clock_functions.c`
- `main/ui/custom/clock_functions.h`
- `main/ui/custom/scroll_functions.c`
- `main/ui/custom/scroll_functions.h`

## 必须保留文件

- `main/ui/custom/ai_ui_controller.c`
- `main/ui/custom/ai_ui_controller.h`
- `main/ui/custom/ai_chat_view.c`
- `main/ui/custom/ai_chat_view.h`
- `main/ui/custom/ui_font_assets.c`
- `main/ui/custom/ui_font_assets.h`
- `main/ui/custom/cbin_font_bridge.c`
- `main/ui/custom/cbin_font_bridge.h`

这些 hand-written 文件属于业务桥接层，不应被 GUI Guider 导出结果覆盖。

# 方案比较

## 方案 A：替换 generated + GUI Guider 配套 custom，保留 hand-written AI 层

做法：

- 用 `C:\Users\ye\Desktop\src\generated` 覆盖仓库 `main/ui/generated`
- 用 `C:\Users\ye\Desktop\src\custom` 中与 GUI Guider 绑定的通用文件覆盖仓库对应文件
- 升级仓库 LVGL 到 `9.3.x`
- 针对 AI 入口桥接、构建清单和 API 差异做最小补丁

优点：

- 最符合用户指定边界
- GUI Guider 结构整体一致，替换逻辑简单
- hand-written AI 层仍然稳定保留

缺点：

- 需要重新把 `custom.h` 等文件中的 hand-written include 接回去
- LVGL 升级后可能暴露更多 API 差异

## 方案 B：只替换 generated，不替换任何 custom

优点：

- 表面改动更少

缺点：

- 当前仓库 `custom.h` 已经混入 AI 桥接 include
- 新 generated 层若依赖新的 custom 头结构，容易出现不匹配
- 实际集成风险更高

## 方案 C：整包替换整个 `main/ui`

优点：

- 与桌面导出源最一致

缺点：

- 会覆盖 hand-written AI 层
- 直接违反当前任务边界

# 推荐方案

推荐方案 A。

原因：

- 它精确命中“只替换 generated 和 GUI Guider 配套 custom 文件”的用户要求。
- 它允许继续复用当前 official_chat 与 AI 聊天气泡页桥接。
- 它把风险限制在 generated/custom 与 LVGL 版本升级，而不是扩散到整个 UI 架构。

# 详细设计

## 1. UI 替换策略

将桌面导出源映射到仓库如下：

- `C:\Users\ye\Desktop\src\generated\**` -> `main/ui/generated/**`
- `C:\Users\ye\Desktop\src\custom\custom.c` -> `main/ui/custom/custom.c`
- `C:\Users\ye\Desktop\src\custom\custom.h` -> `main/ui/custom/custom.h`
- `C:\Users\ye\Desktop\src\custom\lv_conf_ext.h` -> `main/ui/custom/lv_conf_ext.h`
- `C:\Users\ye\Desktop\src\custom\clock_functions.*` -> `main/ui/custom/clock_functions.*`
- `C:\Users\ye\Desktop\src\custom\scroll_functions.*` -> `main/ui/custom/scroll_functions.*`

替换后，需要把仓库 hand-written AI 桥接相关 include 和声明重新补回：

- `custom.h` 中继续暴露 `ai_ui_controller.h`
- 保持 AI 桥接层仍能访问 `ui_font_assets.h`

## 2. AI 入口桥接保持不变

升级后的 generated 事件层中，主菜单 AI 图标仍需通过 `ai_ui_open()` 打开 hand-written AI 页面。

验收条件：

- `screen_main_option_2` 或其等效新对象仍然在点击时触发 `ai_ui_open()`
- 不允许改为直接加载 GUI Guider 新 screen

## 3. LVGL 版本升级

将 `main/idf_component.yml` 中：

- `lvgl/lvgl: 9.2.2`

升级到：

- `lvgl/lvgl: 9.3.x`

升级后需要重新生成依赖锁并重新编译，重点观察：

- `lvgl_port`
- `lvgl_task`
- generated 代码中控件 API
- hand-written AI 视图中的文本、滚动、布局 API
- `ui_font_assets` 运行时字体链

## 4. 字体链处理

LVGL 升级到 `9.3+` 后，当前 `ui_font_assets` 中对 `LVGL < 9.3.0` 的短路保护应重新评估。

本轮目标不是立即把该保护删除，而是：

- 先保证升级后的仓库成功编译和启动
- 再验证运行时 `cbin` 字体链在 `9.3+` 下是否恢复可用

如果升级后运行时字体链正常，再在后续最小改动中去除回退保护。

# 风险

## 1. LVGL API 兼容风险

虽然 generated 层来自新导出 UI，但仓库中的：

- `components/lvgl_port`
- `main/lvgl_task.c`
- hand-written AI 视图

都可能受 `9.3` 升级影响。

## 2. GUI Guider 导出结构微变风险

即便文件名相同，也可能出现：

- 对象命名变化
- 回调注册点变化
- 资源清单变化

所以 AI 入口桥接不能只假设旧对象名永远不变，必须以新 generated 实际内容为准。

## 3. 运行时字体恢复风险

升级到 `9.3+` 后，理论上更接近 `xiaozhi-esp32` 的运行时字体链，但仍需真机验证，不能仅凭版本号直接下结论。

# 验证计划

## 源码级

- 检查 `main/ui/generated/events_init.c` 中 AI 入口仍调用 `ai_ui_open()`
- 检查 `main/CMakeLists.txt` 仍包含 hand-written AI 源
- 检查 `custom.h` 仍包含 AI 桥接必需 include

## 构建级

- `idf.py build`
- 依赖锁更新成功
- 无 generated/custom 相关链接缺符号

## 真机级

- 屏幕正常点亮
- 主菜单正常显示
- 时间页、壁纸页正常
- 点击 AI 入口跳转到 hand-written 聊天气泡页
- AI 页面中文正常
- official_chat 在进入 AI 页面后才前台化

# 回滚策略

如果升级后出现大面积 UI 或 LVGL 兼容问题，按以下顺序回滚：

1. 回滚 `lvgl/lvgl` 版本到 `9.2.2`
2. 回滚 replaced generated/custom 文件到当前仓库旧版本
3. 保持 hand-written AI 层不动

这样可以快速回到当前已知可启动状态。
