---
id: ai-ui-entry-network-guidance
tags: [project, ui, official-chat, lvgl, gui-guider, network-service]
summary: 记录当前仓库在替换 GUI Guider 新导出层并升级到 LVGL 9.3 后，如何保持主菜单 AI 图标继续跳转到 hand-written AI 页面，以及当前 AI 页面布局与联网引导方案。
last_reviewed: 2026-04-17
memory_type: semantic
scope: repo
owners: main/ui/custom/ai_ui_controller.c, main/ui/custom/ai_chat_view.c, main/services/network/network_service.c
triggers: ai, ui, entry, network, guidance
evidence_level: observed
---

# AI 页面入口与未联网引导

## 结论

- 当前仓库已用 `C:\Users\ye\Desktop\src` 替换 `main/ui/generated` 和 GUI Guider 配套通用 `custom` 文件，并同步升级到 `lvgl/lvgl 9.3.0`。
- 新导出层默认没有保留 `screen_main_option_2 -> ai_ui_open()` 这条桥接，因此仓库中已在新的 `events_init.c` 里手工补回：
  - `screen_main_option_2`
  - `screen_main_Xiao_Zhi`
  - 点击后继续进入 hand-written `ai_ui_controller.c`
- 当前仓库仍然没有 GUI Guider 生成的 `screen_ai`，AI 页面继续完全由 hand-written 层承担。
- AI 页面第一版只负责：
  - 读取 `network_service` 状态
  - 展示“未联网 / 正在联网 / 网络已连接 / AI 服务已就绪 / 网络异常”
  - 提供“进入配网”按钮调用 `network_service_request_portal()`

## 代码位置

- 入口事件：
  - `D:\esp32S3\111\main\ui\generated\events_init.c`
  - `D:\esp32S3\111\main\ui\generated\gui_guider.h`
- 手写桥接层：
  - `D:\esp32S3\111\main\ui\custom\ai_ui_controller.h`
  - `D:\esp32S3\111\main\ui\custom\ai_ui_controller.c`
- 初始化接缝：
  - `D:\esp32S3\111\main\ui\lvgl_task.c`
  - `D:\esp32S3\111\main\ui\custom\custom.h`

## 当前页面行为

- 从主菜单点击 AI 图标后，加载手写 AI 页面。
- 新 GUI Guider 导出层的 `custom.h` 已重新补回：
  - `ai_ui_controller.h`
  - `ui_font_assets.h`
  以保证 generated/custom 层仍能看到 hand-written AI 接缝。
- 页面通过 `lv_timer` 轮询 `network_service_get_state()` 和 `network_service_get_ip()`。
- 当未联网或需要重配网时，页面显示本地 AP 配网提示，并保留 `http://192.168.100.1/` 引导文案。
- 正式 AI 页 `main/ui/custom/ai_ui_controller.c` 现在只在当前页面生命周期内持有 AI 会话：
  - 进入页面后，若网络已就绪，才调用 `official_chat_service_enter_foreground()`
  - 返回主页时调用 `official_chat_service_leave_foreground()`
  - 同时销毁当前 hand-written AI 页面对象并清空消息视图句柄
  - 下次再次进入 AI 页时，通过 `ai_ui_ensure_screen_created()` 重新建页并重新拉起新的 AI 会话
  - V1 语义是“退出页面就完整停止 official_chat 会话”，但真正停机由 `official_chat_service` owner task 异步收敛，不阻塞 LVGL 页面线程：
    - 若仍处于 `connecting / listening / speaking`，先调用 `official_chat_stop_listening()`
    - 再等待一段传输静默窗口后才真正 `official_chat_destroy()`
    - 这样可以避免 speaking 态直接销毁 `official_chat` 时触发 `esp_mqtt_client_destroy()` 与 lwIP 互斥崩溃
- 正式入口 `main/app/app_main.c` 当前只负责：
  - 启动 `lvgl_task`
  - 启动后台 `network_service`
  - 初始化 `official_chat_service`
  - 不再在主菜单阶段自动调用 `official_chat_service_enter_foreground()`
- 正式入口 `main/app/app_main.c` 当前已恢复 `lvgl_task` 创建；此前若屏幕完全不亮且串口里没有 `co5300_panel / lv_port` 日志，首查点就是 `lvgl_task` 是否仍被临时注释。
- 页面状态文案会进一步映射 `official_chat_service`：
  - `IDLE` -> `待唤醒`
  - `LISTENING` -> `聆听中`
  - `SPEAKING` -> `回答中`
- 页面布局已重构为：
  - 顶部轻状态条
  - 中部静态 AI 图标卡片
  - 中下部官方式左右气泡聊天区
  - 底部操作区
- 当前新 GUI Guider 导出层仍带有少量旧 API 痕迹，例如旧版图片旋转调用；仓库已在兼容收敛阶段把与现有 build 链冲突的接缝修到可编译状态。
- 聊天区由 `main/ui/custom/ai_chat_view.c` 统一构建，保持：
  - 用户消息右侧气泡
  - 小智消息左侧气泡
  - 空消息时显示居中占位提示
  - 刷新后自动滚到最新一条
  - 顶部标题区和静态 AI 卡片已进一步压缩为紧凑版布局，把更多垂直空间让给聊天区，同时保留底部双按钮的持续可见性
- `official_chat` 现已通过 public event 新增 `OFFICIAL_CHAT_EVENT_USER_TEXT` 和 `OFFICIAL_CHAT_EVENT_ASSISTANT_TEXT`，由 `official_chat_service` 同时维护：
  - 最近一轮用户/助手文本兼容缓存
  - 一个 8 条的小型消息队列，供聊天区按“从旧到新”重建气泡列表
- 历史上的独立实验页 `main/ai_experiment_ui.c` / `main_ai_chat_experiment.c` 已从仓库删除；
  当前只应以 `main/app/app_main.c + main/ui/custom/ai_ui_controller.c + official_chat_service` 作为正式 AI 链路理解。
- `main/CMakeLists.txt` 现已显式维护 `app/services/features/ui` 的 hand-written 源码清单，并仅对 `main/ui/generated/*.c` 保留 glob 收集，避免入口与服务文件继续混在根目录或重复注册。
- `ui_font_assets` 当前默认用 `78/xiaozhi-fonts` 的 `cbin/font_puhui_common_20_4.bin` 内嵌字体承接 `title/body/meta`，`assets` 分区只作为运行时可替换版本；若仍看到 `invalid assets package`，优先重新执行完整 `idf.py flash` 以确保 `assets` 分区与当前构建一致。

## official_chat_service 位置

- `D:\esp32S3\111\main\services\official_chat_service.h`
- `D:\esp32S3\111\main\services\official_chat_service.c`

该服务负责：

- 统一封装 `official_chat_create()`、事件回调和 `official_chat_start()`
- 在 `network_service` 真正进入 `SERVICE_READY` 后再启动 `official_chat`
- 给正式 UI 主流程提供统一的 AI 启动骨架
- 缓存最近一轮文本并维护小型消息队列，供 hand-written AI 页面读取
- 提供 `official_chat_service_leave_foreground()`，由正式 AI 页在返回主页时投递“离开前台并完整停止”命令；service task 负责销毁 `official_chat` 句柄、清空缓存并发布 stopped snapshot
  - 对于 `speaking` 态，必须先走 `official_chat_stop_listening()` 和传输静默等待，不能只靠“状态回到 idle”就立即 destroy

## 共享聊天视图位置

- `D:\esp32S3\111\main\ui\custom\ai_chat_view.h`
- `D:\esp32S3\111\main\ui\custom\ai_chat_view.c`

该视图负责：

- 创建顶部标题、网络徽标、静态 AI 图标卡片和底部操作区
- 创建可滚动消息列表容器
- 按 user / assistant 角色生成左右分侧气泡
- 根据消息队列重建聊天区并滚动到末尾

## 为什么这样落

- GUI Guider 新导出层可以整体替换，但 AI 业务桥接继续放在 `main/ui/custom`，这样后续再次导出 UI 时只需要重补很少的桥接点。
- `events_init.c` 中把 AI 图标事件重新指向 `ai_ui_open()`，是当前最小、最可回退的做法。
- 升级到 LVGL 9.3 后，GUI Guider 新导出层与运行时字体链的版本方向终于一致，不再需要维持 9.2.2 时期那套长期兼容假设。
- 当前文档中若再看到 `ai_experiment_ui.c` 或 `main_ai_chat_experiment.c`，应视为历史背景，而不是当前仓库有效入口。

## 后续建议

- 下一步优先做真机验证，确认新 generated 层下主菜单、时间页、壁纸页和 AI 入口都正常。
- 继续确认 AI 页面中文、聊天区滚动和运行时字体链在 `LVGL 9.3.0` 下的真实表现。
- 再下一步处理离开 AI 页面后的 `leave_foreground` 和音频 owner 行为边界，避免与音乐播放器等功能冲突。
- 字体资源链的当前状态见 [`ai-font-assets`](./ai-font-assets.md)。
