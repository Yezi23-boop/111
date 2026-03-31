---
id: ai-ui-entry-network-guidance
tags: [project, ui, official-chat, lvgl, gui-guider, network-service]
summary: 记录当前仓库把主菜单 AI 图标接到手写 AI 页面，并通过 network_service 展示未联网引导的最小桥接方案。
last_reviewed: 2026-03-31
---

# AI 页面入口与未联网引导

## 结论

- 当前仓库没有 GUI Guider 生成的 `screen_ai`，第一版不要先重导 UI。
- 最稳的做法是：
  - 保持 `main/ui/generated` 只做结构和事件绑定
  - 在 `main/ui/custom/ai_ui_controller.c` 中手写独立 AI 页面
  - 主菜单 `screen_main_option_2` / `screen_main_Xiao_Zhi` 点击后跳转到该页面
- AI 页面第一版只负责：
  - 读取 `network_service` 状态
  - 展示“未联网 / 正在联网 / 网络已连接 / AI 服务已就绪 / 网络异常”
  - 提供“进入配网”按钮调用 `network_service_request_portal()`

## 代码位置

- 入口事件：
  - `D:\esp32S3\111\main\ui\generated\events_init.c`
- 手写桥接层：
  - `D:\esp32S3\111\main\ui\custom\ai_ui_controller.h`
  - `D:\esp32S3\111\main\ui\custom\ai_ui_controller.c`
- 初始化接缝：
  - `D:\esp32S3\111\main\lvgl_task.c`
  - `D:\esp32S3\111\main\ui\custom\custom.h`

## 当前页面行为

- 从主菜单点击 AI 图标后，加载手写 AI 页面。
- 页面通过 `lv_timer` 轮询 `network_service_get_state()` 和 `network_service_get_ip()`。
- 当未联网或需要重配网时，页面显示本地 AP 配网提示，并保留 `http://192.168.100.1/` 引导文案。
- 当前版本在 `NETWORK_SERVICE_STATE_SERVICE_READY` 后会自动调用 `official_chat_service_enter_foreground()`。
- 页面状态文案会进一步映射 `official_chat_service`：
  - `IDLE` -> `待唤醒`
  - `LISTENING` -> `聆听中`
  - `SPEAKING` -> `回答中`

## official_chat_service 位置

- `D:\esp32S3\111\main\official_chat_service.h`
- `D:\esp32S3\111\main\official_chat_service.c`

该服务负责：

- 统一封装 `official_chat_create()`、事件回调和 `official_chat_start()`
- 在 `network_service` 真正进入 `SERVICE_READY` 后再启动 `official_chat`
- 给正式 UI 主流程和实验入口提供同一套 AI 启动骨架

## 为什么先这样做

- 可以避免现在就改大量 GUI Guider 生成文件。
- 可以把业务逻辑留在 `main/ui/custom`，降低后续重新导出 UI 的覆盖风险。
- 可以先把“AI 是主菜单里的独立应用”这个交互路径跑通，再接 `official_chat_service`。

## 后续建议

- 下一步补 `leave_foreground` 的行为边界，决定离开 AI 页面时是否只退前台，还是主动停止监听。
- 再下一步处理 AI 页面和音乐播放器/其它音频功能的资源协调，不要把 owner 逻辑直接塞回入口文件。
- 字体资源链的当前状态见 [`ai-font-assets`](./ai-font-assets.md)。
