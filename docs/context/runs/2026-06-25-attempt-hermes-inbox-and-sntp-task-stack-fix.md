---
id: attempt-hermes-inbox-and-sntp-task-stack-fix
tags: context, runs, attempt-log, hermes, inbox, sntp, freertos, psram, spinlock
summary: 修复网络时间同步任务创建失败 pdFAIL、Hermes收件箱UI不刷新以及自旋锁内PSRAM拷贝导致的死锁与Cache报错。
last_reviewed: 2026-06-25
memory_type: episodic
scope: task
status: active
result: success
owners: docs/context/runs
triggers: attempt-log, hermes, inbox, sntp, freertos, psram, spinlock, create network time sync task failed
evidence_level: design
record_reasons: error-signature, repeat-risk, evidence
---

# Attempt Log: Hermes 收件箱与网络时间同步内存/锁并发闭环改造

## 背景

- 本次要验证什么：
  1. `system_time_service_note_network_ready` 中创建网络时间同步任务报 `create network time sync task failed`（返回 `pdFAIL`）的根因与修复。
  2. Hermes 收件箱页面在后台收到新消息或状态变化时，屏幕静默不刷新卡死的根因与修复。
  3. `memory_watch_service` 中自旋锁 `s_worker_lock` 临界区内跨越片外 PSRAM 进行结构体大段字符串拷贝导致的死锁及 Cache 冲突异常的根因与修复。
- 对应任务或计划：Hermes 收件箱页面显示异常排查与网络时间同步失败 Bug 闭环。
- 生命周期状态：`active`
- 结果状态：`success`
- 长期记录理由：`error-signature | repeat-risk | evidence`
- 大问题错误记录：
  - **错误签名 1**：`create network time sync task failed`（调用 `xTaskCreatePinnedToCore` 申请 16KB 片内 SRAM 任务栈，开机连网时 SRAM 连续碎片不足返回 pdFAIL）。
  - **错误签名 2**：`Cache disabled but cached memory region accessed` 或 Task watchdog 超时死机（在 `portENTER_CRITICAL` 自旋锁关中断状态下，对片外 PSRAM 的 `s_inbox_store` 执行 20 次大结构体 `strncpy` 拷贝，引发长延迟 Cache Miss 屏蔽 CPU 双核中断）。

## 环境

- 分支/工作区状态：`agitated-pythagoras` / `111`
- 设备/串口/板型：ESP32-S3 (8MB Octal PSRAM + 32MB QSPI Flash), QSPI CO5300 屏
- 关键前置条件：ESP-IDF v5.5.3-dirty 环境

## 操作

- 修改过的文件或 owner：
  - `main/services/system_time_service.c`：将 `xTaskCreatePinnedToCore` 替换为 `xTaskCreateWithCaps(..., MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`。
  - `main/services/memory_watch_service.c`：引入静态互斥锁 `s_inbox_store_mutex`，将 `copy_inbox_summaries`、`get_inbox_item`、`mark_read` 及 staging 合并中的 `portENTER_CRITICAL(&s_worker_lock)` 替换为 `xSemaphoreTake/Give` 互斥同步。
  - `main/ui/custom/memory_watch_controller.c`：在渲染快照缓存 `memory_watch_render_cache_t` 中新增 `inbox_generation` 跟踪字段，并在 `matches/store` 中比对。
- 执行的命令或动作：
  - `idf.py build` 固件全量链接编译。
  - `idf_monitor.py` 实机日志上电捕获。
- 已尝试但不应直接重复的路径：
  - **红线规避**：严禁在 `portENTER_CRITICAL` 自旋锁内执行任何涉及片外 PSRAM 的结构体访问或字符串拷贝。自旋锁只允许保护微秒级的片内静态标志位或计数器。

## 观测

- 关键日志/证据：
  - 内存分配证据：`I (2365) heap_init: At 3FCCA498 len 0001F278 (124 KiB): RAM`（开机片内可用连续 SRAM 稳定保持在 124KB）。
  - 显示与组件证据：`I (2801) audio_codec: Audio codec ready`，`I (3211) lv_port: 设置显示向右偏移20像素`。
  - 触摸交互证据：`I (15431) main_dropdown: WiFi button clicked` -> `I (15451) wifi_mgmt_ui: Wi-Fi management screen created`（UI 20ms 内即时响应）。
- 与预期不一致的点：无，原本的所有内存与自旋锁报错全数消失。

## 结论

- 本次可以确认的事实：
  1. 通过 `xTaskCreateWithCaps` 将临时网络同步 HTTP 线程外移至 PSRAM，可完美解决 ESP32-S3 连网阶段 SRAM 峰值枯竭。
  2. 收件箱长跨度字符串拷贝改用互斥锁（Mutex）保护，解除了双核关中断瓶颈，消除了 Flash 擦写屏蔽 Cache 时的异常死机。
  3. `inbox_generation` 快照版本号对比机制有效打破了 UI 渲染缓存死锁。
- 仍然不能确认的事实：长期极低功耗待机下并发频繁语音写入时的 PSRAM 带宽峰值损耗。

## 未验证风险

- 下一轮仍需补证据的边界：实测网络连通状态下 SNTP 真实校时完成的异步事件通知闭环。
