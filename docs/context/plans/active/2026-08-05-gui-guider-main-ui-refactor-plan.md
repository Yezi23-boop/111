---
id: gui-guider-main-ui-refactor
tags: context, plans, gui-guider, lvgl, ui-refactor, ai-memory-watch
summary: 将 D:/esp32S3/111/main/ui 的固定界面彻底迁移到 GUI Guider，保留业务服务和最小动态数据绑定。
last_reviewed: 2026-08-05
memory_type: task
scope: task
owners: main/ui, main/services, gui-guider-project
triggers: GUI Guider 重构, main/ui 重构, controller 减少, UI 设计源
evidence_level: verified-plus-design
status: active
---

# GUI Guider 主 UI 彻底重构执行计划

## 目标与全局

`D:/esp32S3/111/main/ui/ai-memory-watch-guider/ai-memory-watch-guider.guiguider`
是 UI 设计源；GUI Guider 官方生成的 `gg_ui_t` 和事件代码是固件 UI 的唯一生成来源。
旧 `main/ui/generated` 只作为迁移回滚物，不再与新 UI 长期双轨运行。

最终链路：

```text
GUI Guider .guiguider
  -> 官方 C 生成
  -> gg_ui_t
  -> 极薄 UI 数据绑定/动作层
  -> 现有业务服务 owner
```

GUI Guider 负责：Screen、组件树、布局、尺寸、样式、资源、页面跳转、动画、状态、变量、普通
事件和交互预览。业务代码只负责 Wi-Fi、蓝牙、音频、传感器、AI、存储等服务，以及把业务快照
安全地投递到 LVGL 线程。

## 范围与非目标

必须迁移到 GUI Guider：

- 主表盘、控制下拉层、壁纸、日期/日历入口；
- AI、危险检测、记忆、Wi-Fi、音乐、通知、小游戏的固定页面骨架；
- 普通按钮、开关、滑块、Tab、页面跳转、状态样式、动画和可预见的列表/表格结构；
- 中文文本、字体绑定、图片/动画图片和页面预览。

允许保留的自定义代码：

- 业务服务 owner 及其协议/硬件访问；
- 少量 `ui_data_sync`/`ui_actions`：只更新已有对象或调用业务服务，不创建页面、不设置布局、不维护第二棵 UI 树；
- 真正不可预见数量的运行时数据（例如游戏棋盘单元、长列表行）所需的最小更新代码。

明确不做：

- 不直接手改 `generated/`；
- 不把旧 `*_controller.c` 改名后继续作为页面工厂；
- 不维护 `lv_ui` 与 `gg_ui_t` 两套长期对象树；
- 不为当前 GUI Guider 版本虚构 Flex/Grid 或 lottie 的 C 生成能力；
- 不在 LVGL 线程之外直接创建或修改 LVGL 对象。

## 进度

- [x] 盘点旧 `lv_ui`、`lvgl_task`、controller/view 和 CMake 入口。
- [x] 建立 `MainWatch`、`Wallpaper`、`Calendar` 的 GUI Guider 设计源。
- [x] 导入 47 个图片资源和 4 个中文/拉丁字体资源，官方生成 `gg_ui_t` C 代码。
- [x] 完成新旧 UI 接入差异审计：CMake 仍指向旧 `generated/`，新工程尚未进入固件。
- [x] 把固定页面骨架扩展到 AI、危险检测、记忆、Wi-Fi、音乐、小游戏和 OTA 入口。
- [x] 在 GUI Guider 中将 MainWatch 改为真实 TileView、功能卡改为原生 Button、Calendar 改为原生 Calendar，并用官方生成与 Worker 原生帧验收。
- [x] 在 GUI Guider 中将 Hermes 语音、收件箱、消息详情拆成三个可预览的原生 Screen，并接通原生 `load_screen` 导航。
- [ ] 在 GUI Guider 中继续补齐通知中心、真实动态列表/棋盘等页面结构。
- [x] 将当前批次的普通页面跳转和返回操作改为 GUI Guider 原生 `load_screen` 事件。
- [ ] 将动态代码收敛为最小 `ui_data_sync`/`ui_actions`，移除旧 UI 创建函数。
- [ ] 切换 CMake 到新生成目录并删除旧 UI 双轨入口。
- [ ] 完成官方生成、ESP-IDF 编译、预览截图、页面事件和中文显示验收。

本轮 Vue 复刻验收进度（2026-08-06）：

- [x] 为主屏、下拉、功能页、Hermes、AI、危险、日历、壁纸、Wi-Fi、OTA、小游戏、通知和音乐入口建立可重复的 `?screen=` 预览状态。
- [x] 按真实 LVGL 根容器修正 Vue 410×502 外壳 `radius=120`，补齐功能页首/末卡居中滚动、通知中心固定槽位和 AI 滚动状态。
- [x] 使用 LVGL host 与 Vue 同画布截图完成主屏、功能页、Orbit 静态帧及其余已存在基准页的批量差异报告；字体栅格和少量颜色差异按当前验收约束放行。
- [ ] 继续将剩余真实业务状态接入 Vue，并在 host 交互验收后再进入真机验收。

本轮继续复刻证据（2026-08-06）：

- [x] 按真实 `main/ui` 入口补齐 Vue 小游戏菜单、2048、Flappy、Dino 的进入/返回/重开/暂停状态。
- [x] 按 `watch_notification_center.c` 补齐全局通知气泡预览及进入 Hermes 的路径。
- [x] 完成上述状态的 Vue build、LVGL host build、固定 410×502 截图；2048/Flappy/Dino/通知气泡差异率分别约 1.54%/2.49%/2.38%/3.55%。
- [x] 按 `scroll_functions.c` 补齐功能页纵向拖动释放后的最近卡片中心吸附；首卡和末卡均通过 host 交互几何验证，末卡中心偏差为 0px。
- [x] 按 `setup_scr_screen_wallpaper.c` / `events_init.c` 补齐壁纸横向拖动、中心切换和长按应用；按 `watch_notification_center.c` 补齐通知气泡右滑 48px 清除与点击进入 Hermes。
- [x] 完成本轮 Vue build、host build、壁纸/通知/功能页固定 410×502 截图及分层报告；严格整页差异保留字体栅格、图像编码和外壳抗锯齿误差，结构层按允许的轻微配色差异验收。
- [x] 完善 Vue 完整交互：显式 `screen` 深链优先于 `polish` 音乐演示模式；功能页锁定水平滚动，入口按真实 LVGL 的“左滑进入、纵向滚动、短按打开、右滑返回”链路验证。
- [x] 将功能入口短按容差从 8px 修正为 LVGL 一致的 12px；验证真实坐标短按 AI、游戏卡片均可进入对应页面，并重新生成 `vue-interaction-final-v2.png`。
- [x] 用浏览器真实短按逐项验证 AI、游戏、日历、危险识别、壁纸、Hermes 与 OTA 入口；全部进入对应已 Vue 化页面，心率卡保持与 LVGL 一致的无业务跳转。功能卡补充语义标签，避免无文字图标卡无法准确命中。
- [x] 兼容聊天复制时误带到 `screen` 参数末尾的中英文标点；`?screen=function，` 与标准功能页入口使用同一路由状态。
- [x] 固定 Vue 原型外层视口滚动，避免浏览器页面滚动造成可视卡片与鼠标命中坐标漂移；功能页内部滚动仍由功能列表处理。
- [x] 修复功能列表抢占 pointer 后真实鼠标轻微位移会丢失原生 `click`：按下时记录卡片，抬起位移不超过 12px 直接进入页面，超过阈值才按滚动处理。
- [ ] 继续将剩余真实业务动态数据接入 Vue，并在用户确认 host 整页状态后再进入真机验收；本轮不烧录。

## 决策记录

1. 采用“两种操作模式”：实时模式用于用户观看和小步修改；批处理模式用于 Agent 一次完成一页后再交付。
2. 采用“原生优先”：能用 GUI Guider 组件、属性、事件和 `load_screen` 生成的内容，不写自定义 C。
3. 不建复杂兼容层：迁移期可有极窄的字段访问适配，但不复制旧 `lv_ui` 结构，也不把旧 controller 作为 UI API。
4. 动态业务采用快照/消息进入 `lvgl_task`，由一个绑定点更新已有对象；业务服务不反向创建 UI。
5. 当前 GUI Guider 2.0.0.20 的 Flex/Grid 只保存 JSON、不能生成对应 LVGL C；布局优先用嵌套容器、百分比和显式对齐，确需 Flex/Grid 时单独登记为生成器缺口。
6. 当前固件 LVGL 为 9.5，而迁移工程 metadata 为 9.4；切换 CMake 前必须用目标固件头文件/编译结果确认 API 兼容，不能只看 Simulator 通过。
7. `MainWatch` 的两个分页节点已改为真实 `tileview/tile`；下拉控件、亮度/音量滑块、页面导航和功能卡事件均保留稳定 ID，后续只补业务状态绑定。

## 迁移顺序

```text
盘点旧页面
  -> GUI Guider 静态骨架
  -> 原生页面跳转/样式/资源
  -> 最小动作层
  -> 动态数据绑定
  -> 删除旧页面工厂
  -> CMake 切换
  -> 编译与预览验收
```

每个页面完成的定义：

- GUI Guider 中存在稳定 ID，页面可由 Worker 预览；
- 官方生成物中出现对应 `gg_<Screen>.c` 和事件代码；
- 业务代码不再调用 `lv_obj_create`、`lv_obj_set_pos`、`lv_obj_set_size` 创建该页面；
- 动态刷新只通过已有对象句柄；
- 通过页面加载、点击/滑动、中文字体、生成代码和编译检查。

## 提示词

### 总控提示词（批处理模式）

```text
你是本仓库的 GUI Guider UI 重构 Agent。

目标：彻底重构 D:/esp32S3/111/main/ui。GUI Guider 工程
D:/esp32S3/111/main/ui/ai-memory-watch-guider/ai-memory-watch-guider.guiguider
是唯一 UI 设计源，官方生成的 gg_ui_t/事件 C 是固件最终 UI 来源。

硬约束：
1. 先阅读 D:/esp32S3/111/AGENTS.md、C:/Users/ye/Desktop/gui/GUI_GUIDER_AGENT_OPERATIONS.md
   和本计划；先盘点，再改代码。
2. 能用 GUI Guider 原生组件、属性、样式、事件、load_screen、动画、变量完成的，不写自定义 C。
3. 禁止手改 generated/；每次 UI 变更必须通过 GUI Guider 官方生成。
4. 禁止继续维护第二棵 lv_ui UI 树；不把旧 controller/view 改名后保留为页面工厂。
5. 自定义代码只能保留业务服务、动态数据快照和少量 ui_actions/ui_data_sync；不得创建页面或设置布局。
6. 所有 LVGL 操作只在 lvgl_task 所在线程执行，服务线程通过已有消息/快照进入 UI 线程。
7. 当前 GUI Guider 版本 Flex/Grid/lottie 的生成边界必须实测；不把 JSON 能保存当成 C 能生成。
8. 保留用户已有未相关修改；不要 reset、checkout 或覆盖无关文件。

执行顺序：
A. 盘点旧页面、controller/view 的 UI 创建调用和业务调用，列出迁移/保留/删除清单；
B. 在 GUI Guider 中完成一个页面的静态骨架、稳定 ID、资源和原生事件；
C. 官方生成并检查 gg_ui_t、事件 C、资源 C；
D. 把动态数据接到已有对象，删除该页面旧 UI 创建代码；
E. 运行 ESP-IDF 编译/静态检查，使用 Worker 原生帧截图验证预览，并用 tools/vision.js 描述截图；
F. 更新本计划和 GUI_GUIDER_AGENT_OPERATIONS.md，记录证据路径、剩余 controller 和下一步。

每轮只完成一个可验收页面或一个明确迁移批次。最终汇报必须包含：改动文件、删除的 UI 创建代码、保留的业务代码、
生成命令/结果、编译结果、截图证据、未完成项和下一轮建议。
```

### 单页面迁移提示词

```text
迁移页面：<页面名>（来源：<旧 view/controller 文件>）。

请先统计该文件中 UI 创建/布局/样式/事件调用与业务调用；将固定结构全部在 GUI Guider 中重建，使用稳定 ID：
<列出 ID 规则>。普通导航、开关、滑块、列表和状态样式必须使用 GUI Guider 原生能力。

只允许保留：
- 业务服务调用；
- 动态文本/数值/状态对已有对象的更新；
- 无法预先声明数量的内容的最小刷新函数。

完成后必须：
1. 官方生成 C 代码；
2. 删除该页面旧的 lv_obj_create/布局/样式/导航代码；
3. 编译或至少做目标源静态检查；
4. 截取 Worker 原生画布并验证页面入口、返回和中文文本；
5. 把删除/保留清单和证据写入计划。
```

### 实时小步修改提示词

```text
实时模式：只修改 GUI Guider 工程中 ID=<组件 ID> 的 <属性>。
先读取最新 project/UI 基线，确认用户没有改动同一组件；用 update_property 驱动实时预览，
截图确认后再按用户指令决定是否 project.updateUI。不要重载整个工程，不要改 generated/，不要触碰其他 ID。
如果用户已经手动修改同一组件，停止覆盖并报告冲突。
```

### Controller 清理提示词

```text
审计 <controller/view 文件>：把每个函数标为“页面创建/布局样式/导航/动态刷新/业务服务/硬件访问”。
页面创建、布局样式和普通导航迁移到 GUI Guider 后删除；动态刷新合并到单一 ui_data_sync；业务服务和硬件访问保留。
不要创建新的 controller 层，不要复制 lv_ui 结构。输出删除函数、保留函数、调用方变化和编译验证结果。
```

## 验证与验收

- `node --check` 检查迁移/恢复脚本；
- GUI Guider `project.updateUI` 后直接读取 `.guiguider` 校验 ID、默认页和资源引用；
- `uic.generateCodes` 成功且生成目录只被官方写入；
- ESP-IDF 编译前确认 `D:/esp-idf/v5.5.3/esp-idf/export.ps1` 存在并加载；
- Worker 原生帧截图不以 DOM 空白作为失败依据；截图交给 `node tools/vision.js`；
- 页面事件、多页跳转、中文字体、动态数据更新和回滚路径均有证据。
- 当前 `.guiguider` 实际统计为 12 个 Screen、176 个组件节点、24 个事件目标；像素对比基准固定为 410x502、通道容差 8、差异率门槛 0.5%。Hermes 最近一次证据仍未通过门槛：Voice 3.8077%、Inbox 7.2146%、Detail 5.6967%。
- 已确认 GUI Guider 2.0.0.20 保存 `long_mode` 但不生成对应 C API；列表省略号暂用设计侧固定文本，严格动态截断需生成器补丁或绑定层处理。

## 意外与发现

- 本轮确认正确顺序是“先在 GUI Guider 中仿照 `main/ui` 完成设计和预览，再做固件接入”；固件 `main/CMakeLists.txt`、`lvgl_task.c/h` 和 `app_main.c` 已恢复到原入口，当前没有把半成品 `gg_ui_t` 接入固件。
- 新生成代码使用按 Screen 命名空间组织的 `gg_ui_t`，不能直接替换旧扁平 `lv_ui`。
- 新生成头文件只声明 `extern gg_ui_t guider_ui`；实例必须由非 `generated/` 的目标包装层定义，不能手改生成文件。
- 新工程当前未被 `main/CMakeLists.txt` 纳入，且主循环仍走旧 `setup_ui/events_init/lv_ui`，因此现在只能作为设计源，不能宣称已接入固件。
- 当前 GUI Guider 生成器没有 Flex/Grid 和 lottie 的 LVGL C 输出。
- `update_widget` 需要完整组件 JSON，并在批处理后显式 `project.updateUI`。
- 当前目标工程的图片源同时存在 RGB565A8 和 RGB565，恢复脚本必须按格式区分 Alpha 平面。
- 本轮设计工程包含 12 个 Screen、176 个组件节点、24 个原生导航事件；`gg_MainWatch.c` 已出现 `lv_tileview_create`/`lv_tileview_add_tile`，`gg_Calendar.c` 已出现 `lv_calendar_create`，Wallpaper 的四张 `imagebutton` 已收进 410px 安全区。
- Hermes 设计侧已对应旧 `memory_watch_view` 的 voice/inbox/detail 三段结构：`screen_memory`、`screen_memory_inbox`、`screen_memory_detail`；固定演示消息只用于预览，后续由快照更新稳定 ID。
- Worker 原生帧验证了 Hermes 三页中文、按钮、消息卡片和返回路径；初版根容器默认滚动条造成右侧/底部灰线，已在设计脚本中关闭静态根/气泡容器滚动条，动态列表若超出再由绑定层按状态开启。
- Worker 预览需要先从首页“选择”进入 UI Editor；仅 `project.open` 注册工程不会建立可捕获的当前页面。对未在 Worker 创建的目标 Screen，先发送 `createObject({parent_id:'', widget_json: screen}, true)`，再用 `loadScreen({id})` 捕获目标页。
- 像素复刻的误差不能归结为单一坐标问题：LVGL 编译字体与 Worker TTF 栅格化、Host 透明圆角 Mask 与 GUI Guider radius、以及 `long_mode` 生成缺口必须分别验证和记录。
- 动态列表/通知中心推荐“GUI Guider 固定 row + 稳定 ID + `ui_data_sync` 快照”；当前 2048 棋盘推荐预建 16 个原生 cell。GUI Guider 当前不生成 Flex/Grid，不能把 JSON 中保存的布局字段当成已交付能力。

## 下一步

先在 GUI Guider 中补通知中心、动态列表/2048 棋盘的静态结构；随后重新截图收口 Hermes 像素差异，再做统一业务快照绑定。
当前批次的 MainWatch、Wallpaper、Calendar 和 Hermes 三页已通过官方生成和 Worker 原生帧预览，固件接入仍保持待办。
每完成一页就删除对应旧 UI 创建函数，不等待所有页面完成后再做一次大爆炸式切换。

## 幂等与恢复

- 保留旧 `main/ui/generated` 直到新生成目录能独立通过编译；
- GUI Guider 工程使用唯一 `projectId`，迁移脚本重复执行不得覆盖用户工程；
- 每个页面迁移前保存 `.guiguider` 和生成物清单；失败时只回滚该页面/该批次；
- 不删除用户未授权的旧代码，删除前先输出精确函数/文件清单。
