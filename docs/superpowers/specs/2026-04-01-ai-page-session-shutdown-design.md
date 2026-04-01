---
id: ai-page-session-shutdown-design
tags: [project, ai, official-chat, ui, lifecycle, lvgl]
summary: 设计正式 AI 页面和实验页在返回主页时彻底释放 official_chat 会话、消息缓存和页面对象，使 AI 对话只在当前页面内运行。
last_reviewed: 2026-04-01
---

# AI 页面返回主页即销毁会话设计

## 问题

当前正式 AI 页和实验页离开时只调用 `official_chat_service_leave_foreground()`，这只能把 AI 会话从“前台请求”状态切回后台等待，并不会释放：

- `official_chat` 句柄
- 音频链与内部任务
- 最近一轮文本缓存
- 小型消息队列缓存
- AI 页面已创建的 hand-written screen 对象

结果是 AI 对话会继续在后台存活，不符合“只在当前页面运行，离开页面后就消除”的目标。

## 目标

- 进入 AI 页面时再创建 AI 对话会话。
- 返回主页时彻底释放 AI 对话资源。
- 回到主页后不再保留 AI 音频链、消息缓存和活动会话。
- 下次再次进入 AI 页面时，按全新会话重新初始化。

## 范围

本次仅覆盖：

- `main/official_chat_service.h`
- `main/official_chat_service.c`
- `main/ui/custom/ai_ui_controller.c`
- `main/ai_experiment_ui.c`（如存在退出/关闭路径则同步）

不覆盖：

- 主菜单 generated UI 结构
- `network_service` 联网策略
- `official_chat` 组件内部协议/音频实现
- 配网页和主菜单其它应用

## 方案比较

### 方案 A：仅 leave_foreground

- 保持当前实现，只把前台请求关掉。
- 优点：改动最小。
- 缺点：不会释放 `official_chat` 资源，不满足需求。

### 方案 B：页面退出时销毁会话，保留服务任务骨架

- 返回主页时显式销毁 `official_chat` 句柄并清空缓存。
- AI 页 hand-written screen 也一并释放。
- 后台服务任务继续保留，但处于空闲态。
- 优点：满足“只在当前页面运行”，同时改动边界小、可回退。
- 缺点：再次进入 AI 页时会重新初始化，启动会略慢。

### 方案 C：页面退出时连服务任务一起删除

- 除销毁会话外，同时删除 service task。
- 优点：释放最彻底。
- 缺点：实现复杂，涉及任务重建和状态竞争，不适合当前最小改动目标。

## 结论

采用方案 B。

## 详细设计

### 1. `official_chat_service` 新增显式 shutdown 能力

在 `official_chat_service` 新增：

- `void official_chat_service_shutdown(void);`

该接口负责：

- 清除 `s_foreground_requested`
- 若 `s_chat_handle != NULL`：
  - 调用 `official_chat_destroy(s_chat_handle)`
  - 将 `s_chat_handle = NULL`
- 清空：
  - `s_last_user_text`
  - `s_last_assistant_text`
  - `s_message_history`
  - `s_message_count`
- 将 `s_service_state` 置回 `OFFICIAL_CHAT_SERVICE_STATE_STOPPED`
- 将 `s_last_error` 复位到 `ESP_OK`

### 2. 正式 AI 页返回主页时销毁 AI 会话与页面对象

在 `ai_ui_controller.c` 的返回主页路径：

- 由原先的 `official_chat_service_leave_foreground()` 改为：
  - `official_chat_service_shutdown()`
- 同时释放当前 AI 页 hand-written 资源：
  - 删除 AI 页面 screen 对象
  - 将 `s_view = NULL`
  - 取消当前页相关的“已请求前台”状态

`s_status_timer` 可保留为全局轮询器，但回调必须继续只在 AI 页激活时才工作；不强制删除 timer。

### 3. 下次进入 AI 页时重新创建

`ai_ui_open()` 保持现有策略：

- 若 `s_view == NULL`，重新执行 `ai_ui_ensure_screen_created()`
- 重新刷新状态
- 若网络已 ready，再重新请求 `official_chat_service_enter_foreground()`

这样下次进入页面时，会像新的 AI 会话一样重新启动。

### 4. 实验页同步行为

若 `main/ai_experiment_ui.c` 存在用户可见的退出/返回路径，也应在离开时调用相同的：

- `official_chat_service_shutdown()`

保证正式页和实验页的生命周期语义一致。

## 数据与状态影响

返回主页后将丢弃：

- 最近一轮用户文本
- 最近一轮助手文本
- 聊天消息队列
- 当前会话状态

这是预期行为，因为目标就是“离开即消除”。

## 验证计划

### 源码验证

- 增加/更新源码级测试，确认：
  - `official_chat_service.h` 暴露 `official_chat_service_shutdown`
  - `ai_ui_controller.c` 返回主页路径改为调用 `official_chat_service_shutdown`
  - AI 页再次打开时仍会重新请求进入前台

### 构建验证

- `idf.py build`

### 真机验证

1. 开机进入主菜单，不进入 AI 页面。
2. 进入 AI 页面，确认 AI 正常进入对话态。
3. 返回主页。
4. 观察串口：
   - 不应再继续有 AI 对话态推进日志
   - 会话状态应回到 stopped/空闲
5. 再次进入 AI 页面：
   - AI 会重新初始化
   - 聊天气泡区应为空或回到初始占位状态

## 风险

- 若在 `official_chat_destroy()` 时仍有异步事件回调飞入，可能触发状态竞争。
- 若 AI 页面对象删除时仍有 timer 回调访问旧对象，可能出现悬空访问。

## 风险控制

- `shutdown` 时先清除前台请求，再销毁句柄。
- AI 页 timer 回调继续保留“仅当当前 active screen 是 AI 页时才刷新”的保护。
- 删除页面对象后立刻将 `s_view = NULL`。

## 回滚

若出现销毁后重进 AI 页异常，可回滚为：

- 返回主页仅调用 `official_chat_service_leave_foreground()`
- 暂不删除页面对象

该回滚路径局部、简单，且不影响主菜单和联网主流程。
