---
id: attempt-task-stack-measurement-instrumentation
tags: context, runs, attempt-log, freertos, stack, ram, psram, measurement, instrumentation
summary: 为任务栈与内存占用实测建立观测能力：新增全任务栈高水位聚合采样函数，恢复冷启动内存快照打印，为后续缩栈优化提供事实依据。
last_reviewed: 2026-06-28
memory_type: episodic
scope: task
result: success
owners: components/z_print_esp32, main/ui/lvgl_task.c
triggers: stack high water mark, task stack measurement, printf_esp32, cpu_monitor_task, resource baseline, RAM
evidence_level: observed
record_reasons: evidence, repeat-risk
---

# Attempt Log: Task Stack Measurement Instrumentation

## 背景

- 本次要验证什么：当前 21 个 FreeRTOS 任务的栈占用几乎全是未知数（除 `mw_upload` 已知 3172 words），无法安全缩栈。需要先建立观测能力，再决定哪些任务栈多余。
- 对应任务或计划：用户要求"先合理测试出当前各个任务的资源占用"。属于资源仲裁方案 Gate 0（测量基线）的前置工作。
- 生命周期状态：`active`
- 结果状态：`success`（观测能力已建立并编译通过，待真机采样）
- 长期记录理由：`evidence`（为后续缩栈决策提供事实依据）、`repeat-risk`（避免重复造观测工具）

## 环境

- 分支：`codex/ai-memory-watch-hermes-api`
- 设备/串口/板型：ESP32-S3（8MB Octal PSRAM + 32MB QSPI Flash），COM3
- 关键前置条件：sdkconfig 已启用 `CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY=y`、`CONFIG_FREERTOS_USE_TRACE_FACILITY=y`、`CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y`，`uxTaskGetSystemState` 可用。

## 操作

- 修改过的文件或 owner：
  - `components/z_print_esp32/printf_esp32.h`：声明 `printf_esp32_all_task_stack_stats()`。
  - `components/z_print_esp32/printf_esp32.c`：实现 `printf_esp32_all_task_stack_stats()`，遍历 `uxTaskGetSystemState`，按剩余空间升序排序打印所有任务的栈高水位（bytes），并附带 internal heap free / largest block / psram free 快照。
  - `main/ui/lvgl_task.c`：`cpu_monitor_task` 首次循环延迟 8 秒后调用 `printf_esp32_memory_stats()` + `printf_esp32_all_task_stack_stats()` 打印一次冷启动稳态快照，之后保持静默避免刷屏。
- 执行的命令：
  - `. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`
- 已尝试但不应直接重复的路径：
  - 不在 `app_main.c` 末尾新建一次性采样任务：`cpu_monitor_task` 已在 `ui_first_frame_ready` 后创建，8 秒延迟足够覆盖所有 deferred service 初始化，无需重复。
  - 不让 `cpu_monitor_task` 每 5 秒都打印：会导致串口刷屏并干扰调试，只在冷启动打印一次。

## 观测

- 关键日志/证据：
  - `idf.py build` 通过：`111.bin binary size 0xabffc0 bytes`，最小 app 分区剩余 `0x340040 bytes (23%)`。
  - 相比上次基线 `0xabef90`/`0x341070`，bin 增大约 `0x10d0`（~4.2KB），来自新增聚合函数和格式化字符串。
  - 编译无 warning/error。
- 与预期不一致的点：无。

## 结论

- 本次可以确认的事实：
  - 项目已有完整监控基建（`printf_esp32_memory_stats`、`printf_esp32_task_stack_stats`、sdkconfig trace facility），但之前未实际启用。
  - 新增 `printf_esp32_all_task_stack_stats()` 可一次性遍历所有 task 并按剩余空间排序，`TaskStatus_t` 不含栈总大小，需配合各 owner 的 `xTaskCreate` 传入值对照计算使用率。
  - 冷启动 8 秒后采样可覆盖所有 service task 稳态。
  - **真机冷启动基线已采集（2026-06-28 21:59）**：`cold_boot_resource_snapshot_done` 在 11.85s 出现，含 28 个运行中 task 的栈高水位 + 内存快照。`boot_stage: memory_watch_ready` / `imu_service_ready` 均在采样前出现，8 秒延迟充分。`cpu_monitor_task` 4096B 栈执行采样函数后 free=988B，无 stack overflow。
  - **单位确认**：ESP-IDF 头文件 `idf_additions.h` 明确 `xTaskCreateWithCaps` / `xTaskCreatePinnedToCoreWithCaps` 栈参数单位为 bytes（非 words）。`memory_watch_service.c` 的 `k*StackWords` 常量命名为误导，实际按 bytes 处理。
- 仍然不能确认的事实：
  - 各任务的真实栈峰值（冷启动稳态只反映空闲水位，高压场景峰值需后续场景化采样）。
  - 6 个高压场景（LVGL 复杂渲染 / Hermes 前台语音 / 离页 pending / 音频+告警 / 联网+SNTP / 危险识别推理）下的栈水位尚未采集。

## 已验证风险（冷启动基线，2026-06-28 21:59）

- `cpu_monitor_task` 4096B 栈执行采样函数后 free=988B（24.1%），无 overflow，安全。
- 8 秒延迟充分：`memory_watch_ready`(3251ms) / `imu_service_ready`(3271ms) 均在采样(11.7s)前完成；`network_service_ready`(3211ms) 亦在采样前。冷启动快照覆盖所有 deferred service 稳态。
- ⚠️ **新发现紧急风险：`mw_health` free=160B（2.6%）**。health worker 执行 HTTP health check 后高水位仅剩 160B，冷启动已近栈溢出，**必须扩栈**（建议 6144→10240 PSRAM）。这是冷启动基线暴露的最危险项。
- 缩栈候选（free%>65%，需高压验证后执行）：mw_upload(91.6%)、official_chat_s(90.8%)、network_mgr(81.1%)、time(74.7%)、mw_conv(68.9%)、power_service(66.8%)、lvgl_task(66.1%)、network_service(62.2%)。
- 完整对照表见 artifact：`task-stack-cold-boot-baseline.md`。
- 日志存档：`D:\esp32S3\111\board_logs\2026-06-28-task-stack-cold-boot.log`（500 行）。

## 下一轮待补证据

1. ~~6 个高压场景的栈水位采样~~ **已完成（2026-06-28 22:11）**，见下方"高压场景验证"。
2. `mw_health` / `mw_conv` 扩栈后复测确认 free% 回到安全水位。
3. 缩栈项改动后回归冷启动基线 + 对应高压场景。
4. 补采音频告警 + 危险识别推理场景的栈水位（本轮未触发）。

## 高压场景验证（2026-06-28 22:11）

- 采集方式：临时启用 `cpu_monitor_task` 每 5 秒周期采样，90 秒采集窗口，用户配合操作。
- 用户触发的场景：WiFi 连接+SNTP、天气获取、Hermes 前台语音对话×2、WebSocket 上传、IMU 抬腕×3、WiFi 管理页面+SoftAP 配网尝试。
- 未触发：音频告警播放、危险识别推理（需特定条件）。
- 日志存档：`D:\esp32S3\111\board_logs\2026-06-28-task-stack-high-pressure.log`（1150 行）。
- 完整对照表见 artifact：`task-stack-high-pressure-baseline.md`。

### 高压验证关键结论（推翻部分冷启动建议）

1. ⚠️ **mw_conv free=200B（3.3%）— 新发现紧急风险**。冷启动 4232B → 高压(AI对话) 200B，掉 4032B。必须扩栈 6144→10240。
2. ⚠️ **mw_health free=208B（3.4%）— 持续紧急**。必须扩栈 6144→10240。
3. ❌ **推翻冷启动结论：mw_upload 不能缩栈**。冷启动 91.6% 空闲建议缩到 8192，但高压(语音上传)后 free=3172B（12.9%），实际用 21404B。缩到 8192 会栈溢出。保持 24576。
4. lvgl_task 高压 free=5140B（50.2%），边界值，保守保持 10240。
5. 可安全缩栈（高压验证后）：official_chat_s(8192→4096)、network_mgr(4096→3072)、network_service(6144→4096)、power_service(4096→3072)、time(8192→6144)。
6. 周期采样固件已恢复为冷启动一次采样（临时测量用，不留正式固件）。
7. 附带发现：Captive Portal DNS 启动失败(ESP_FAIL)、SSL 期间 esp-aes 分配失败（internal RAM 紧张），均为独立 bug，不影响栈分析。
