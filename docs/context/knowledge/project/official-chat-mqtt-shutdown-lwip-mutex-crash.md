---
id: official-chat-mqtt-shutdown-lwip-mutex-crash
tags: [project, official-chat, mqtt, lwip, shutdown, esp32s3]
summary: 记录 official_chat 在 MQTT 模式下离开 AI 页面时，`esp_mqtt_client_destroy()` 命中 `sys_mutex_lock` 断言的根因与最小修复策略。
last_reviewed: 2026-04-01
memory_type: semantic
scope: repo
owners: main/services/official_chat_service.c, components/official_chat, components/network_manager
triggers: official, chat, mqtt, shutdown, lwip, mutex, crash
evidence_level: observed
status: active
---

# official_chat MQTT 停机触发 lwIP mutex 断言

## 现象

- 真机日志在 AI 页面退出后的销毁路径中触发：
  - `assert failed: sys_mutex_lock ... (failed to take the mutex)`
  - 回溯落在：
    - `esp_mqtt_client_destroy()`
    - `esp_transport_list_destroy()`
    - `esp_tls_conn_destroy()`
    - `lwip_shutdown()`
    - `official_chat::MqttProtocol::StopMqttClientLocked()`
    - `official_chat::Application::~Application()`
    - `official_chat_destroy()`
    - `official_chat_service_task()`

## 根因

- 当前仓库此前只对 `connecting / listening / speaking` 做停机静默窗。
- 当 `official_chat` 已完成激活并进入 `idle` 时，MQTT/TLS 连接仍然保持在线订阅。
- AI 页面此时若同步阻塞式关闭并立即销毁，会走到 `official_chat_destroy()` 的高风险窗口。
- `Application::~Application()` 最终进入 `MqttProtocol::~MqttProtocol()`，在 MQTT 连接仍活跃或刚完成回调切换时直接 stop/destroy，容易把 `esp-tls` / `lwip` 套接字清理压到尚未完全静默的传输链路上，最终在 `lwip_shutdown()` 内命中 `sys_mutex_lock` 断言。

## 关键代码证据

- `D:\esp32S3\111\main\official_chat_service.c`
  - 退出页时的服务停机逻辑原先仅把 `connecting / listening / speaking` 视为“需要等静默”的状态。
- `D:\esp32S3\111\components\official_chat\protocols\mqtt_protocol.cc`
  - `StopMqttClientLocked()` 原先直接 `esp_mqtt_client_stop()` 后立刻 `esp_mqtt_client_destroy()`，没有先显式请求 disconnect，也没有等待 `MQTT_EVENT_DISCONNECTED`。
- `D:\esp32S3\111\main\ui\custom\ai_ui_controller.c`
  - 返回主页会触发 official_chat 停机，因此退出页就是这条销毁链路的直接触发点。

## 最小修复

### 1. 服务层把退出收敛成“先回 `idle`，再 destroy”

- `official_chat_service_requires_shutdown_quiet_period()` 现在包含：
  - `OFFICIAL_CHAT_STATE_CONNECTING`
  - `OFFICIAL_CHAT_STATE_IDLE`
  - `OFFICIAL_CHAT_STATE_LISTENING`
  - `OFFICIAL_CHAT_STATE_SPEAKING`
- 对 `connecting / listening / speaking` 不再直接 destroy，而是先调用 `official_chat_stop_listening()`，等待状态真正回到 `idle`。
- 若等待静默窗期间又被新的底层动作拉回 `connecting / listening / speaking`，静默计时必须取消并在再次回到 `idle` 后重启。
- 对 `idle` 不再立即 destroy，而是先进入 `kShutdownTransportQuietPeriodMs` 等待窗，再销毁句柄。

### 2. UI 层退出要改成异步两阶段

- 返回键不应同步调用阻塞式 `official_chat_service_shutdown()` 后立刻离页；V1 应投递 `official_chat_service_leave_foreground()`，等待 owner task 发布 `STOPPED` 后再离页。
- 正确做法是：
  - UI 先调用 `official_chat_service_leave_foreground()`
  - UI 本地要锁存一次 `exit requested`，不能只盯着 service 的 `shutdown_pending`
  - 页面进入“正在退出”态，禁用交互
  - 轮询 `official_chat_service_is_shutdown_pending()` 与 `official_chat_service_get_state()`
  - 只要 service 明确收敛到 `OFFICIAL_CHAT_SERVICE_STATE_STOPPED`，就应真正销毁 AI 页面并返回主页，不能要求“`shutdown_pending` 与 `STOPPED` 同时成立”
- 这样可以避免页面已经切走，但底层 `official_chat / MQTT / TLS / audio` 还在拆资源的竞态。
- 当前仓库已踩过一次实际坑：
  - service 完成停机时会先清掉 `shutdown_pending`
  - 若 UI 只在 `shutdown_pending == true && state == STOPPED` 这个瞬间才离页，就会错过返回主页时机，页面表现为“已经点了退出，但一直停在 AI 页”

### 3. `official_chat` 应用层必须有 shutdown fence

- 仅靠 service 外层“等 `idle` 再 destroy”还不够。
- speaking 退出实机曾出现：
  - 已经 `shutdown waiting for idle before destroy`
  - 之后又继续处理 `HandleActivationDoneEvent`
  - 又继续处理 `HandleToggleChatEvent`
  - 又再次收到 `WakeWordDetected`
  - 最后出现 `wake word detection task did not exit before destruction`
- 根因是 `Application` worker 在退出静默窗里仍会消费旧的 event bits 和已排队 callback，导致状态机被重新拉起。
- 当前仓库的修复是：
  - 新增 `official_chat_prepare_shutdown()`
  - `Application::PrepareForShutdown()` 把 `shutting_down_` 置位
  - 清空 `main_tasks_`
  - 清除 `ToggleChat / StartListening / WakeWordDetected / ActivationDone` 等待处理位
  - 在 `RunLoop()` 中显式忽略这些事件
  - 在 `HandleStateChanged()` 的 shutdown 路径里强制关闭 wake word detection / voice processing，并保持 decoder 复位
  - 音频 wake word callback 在 `shutting_down_` 期间不再继续投递 `kMainEventWakeWordDetected`

### 4. shutdown fence 不能拦截 StopListening

- 2026-04-24 真机日志显示退出 AI 页时：
  - `prepare shutdown armed`
  - `official_chat_stop_listening during shutdown failed: ESP_ERR_INVALID_STATE`
  - `shutdown waiting for idle before destroy state=listening`
- 根因是服务层先调用 `official_chat_prepare_shutdown()`，使 `Application::shutting_down_` 置位；随后 `official_chat_stop_listening()` 被 `StopListening()` 的 shutdown guard 拒绝。
- `kMainEventStopListening` 是停机收敛路径，不是新的用户交互入口；`RunLoop()` 在 shutdown 期间本来就允许执行 `HandleStopListeningEvent()`。
- 因此 `StopListening()` 只检查 `started_ / event_group_`，即使 `shutting_down_` 已置位也允许投递停止事件，让 `connecting / listening / speaking` 能关闭音频通道并回到 `idle`。

### 5. MQTT 层不要手动 `disconnect`

- `esp_mqtt_client_disconnect()` 在 ESP-IDF 5.5.3 里只是设置 `DISCONNECT_BIT`。
- MQTT 任务随后会在 `MQTT_STATE_CONNECTED` 分支里：
  - `send_disconnect_msg()`
  - `esp_mqtt_abort_connection()`
  - 任务退出前再次 `esp_transport_close(client->transport)`
- 若业务层自己又在 `StopMqttClientLocked()` 中手动 `disconnect` 后紧接 `stop/destroy`，会把 TLS/socket 关闭路径拉成重入窗口，实机已出现：
  - `esp_tls_internal_event_tracker_destroy`
  - `tlsf_free ... block already marked as free`
- 因此当前仓库的最小修复是：
  - 保留 service 侧 `idle` 静默窗
  - MQTT 侧只调用 `esp_mqtt_client_stop()` 与 `esp_mqtt_client_destroy()`
  - 不再额外手动 `esp_mqtt_client_disconnect()`

## 适用边界

- 该结论针对当前仓库的 `official_chat + MQTT + UDP` 路线成立。
- 它修复的是“页面退出时销毁过急”的生命周期问题，不等价于证明所有 `sys_mutex_lock` 断言都来自 MQTT。
- 若后续仍出现相同断言，需要继续核查：
  - 是否存在别的任务同时清理 socket
  - 是否有 lwIP/FreeRTOS 级内存破坏
  - 是否在 monitor 里看到旧镜像日志导致误判

## 验证

- 源码级回归：
  - `D:\esp32S3\111\tests\test_official_chat_service_source.py`
  - `D:\esp32S3\111\tests\test_official_chat_source.py`
- 构建验证：
  - `idf.py build`
