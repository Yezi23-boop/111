---
id: task-stack-measurement-full-summary
tags: context, knowledge, project, freertos, stack, ram, psram, measurement, resource-arbitration
summary: 任务栈与内存占用三阶段实测汇总（owner init 逐步采样 + 冷启动基线 + 高压场景峰值 + 扩缩栈后回归），作为资源仲裁方案的唯一事实依据。
last_reviewed: 2026-06-28
memory_type: semantic
scope: project
status: active
owners: docs/context/knowledge/project/task-stack-measurement-full-summary.md
triggers: task stack, high water mark, internal RAM, PSRAM, resource arbitration, mw_health, mw_conv, mw_upload, owner init, cold boot, high pressure
evidence_level: observed
---

# 任务栈与内存占用三阶段实测汇总

> 采集时间：2026-06-28 21:59~22:50，COM3，ESP32-S3（8MB Octal PSRAM + 32MB QSPI Flash）
> 分支：`codex/ai-memory-watch-hermes-api`
> 数据来源：
> - owner init 逐步采样：`board_logs/2026-06-28-owner-init-stack-sampling.log`（681 行）
> - 冷启动基线：`board_logs/2026-06-28-task-stack-cold-boot.log`（500 行，扩栈前）
> - 高压场景峰值：`board_logs/2026-06-28-task-stack-high-pressure.log`（1150 行，扩栈前，周期采样 90s，含 2 次 AI 对话）
> - 扩缩栈后回归：`board_logs/2026-06-28-task-stack-post-resize-high-pressure.log`（1410 行，扩栈+缩栈后，周期采样 90s，含 2 次 AI 对话）

---

## 1. owner init 后栈高水位逐步采样

### 启动时序与 internal RAM 变化

| 阶段 | 时间(ms) | internal_free(B) | largest(B) | psram_free(B) | 新增 task |
|---|---|---|---|---|---|
| policy_ready | 2980 | 130054 | 90112 | 8344048 | power_service, power_policy, sleep_coord, wakeup_evidence |
| managers_ready | 3070 | 125566 | 86016 | 8344048 | background_mgr |
| network_service_ready | 3400 | 82490 | 51200 | 8337784 | network_service, network_mgr, wifi, time, sys_evt, tiT |
| official_chat_ready | 3610 | 44962 | 29696 | 7059824 | official_chat_s (+ UI 渲染消耗) |
| memory_watch_ready | 3780 | 38114 | 23552 | 6958156 | memory_watch, mw_upload, mw_health, mw_cancel, mw_conv, mw_inbox, cpu_monitor |
| imu_service_ready | 3940 | 33534 | 19456 | 6958156 | imu_service |
| cold_boot_snapshot(8s) | 11800 | 30474 | 19456 | 6906060 | (WiFi 连接 + health check 后) |

**关键发现**：
- **network_service_ready 是 internal RAM 最大降幅点**：130KB→82KB（-47KB），WiFi 驱动 + LWIP + PHY 一次性吃掉大量 internal RAM
- **official_chat_ready 是第二大降幅点**：82KB→44KB（-37KB），但这里主要是 LVGL UI 渲染（display_foundation_done + ui_first_frame_ready 在此期间发生），不是 official_chat_s 本身
- **memory_watch_ready 降 6.7KB**：6 个 mw_* task（PSRAM 栈）+ cpu_monitor 创建
- **imu_service_ready 降 4.5KB**：imu_service task（internal 栈）
- 冷启动 8 秒后比 init 完成仅多降 3KB（WiFi 连接 + health check）

### 各 task 栈高水位逐步变化

> 栈高水位只降不升（历史累积最小值）。"—"表示该 task 尚未创建。

| task | 配置(B) | policy_ready | managers_ready | network_svc_ready | official_chat_ready | memory_watch_ready | imu_service_ready | cold_boot(8s) |
|---|---|---|---|---|---|---|---|---|
| **ipc0** | 系统 | 388 | 388 | 388 | 388 | 388 | 388 | 388 |
| **ipc1** | 系统 | 540 | 540 | 540 | 540 | 540 | 540 | 540 |
| **sys_evt** | 系统 | — | — | 624 | 624 | 624 | 624 | 592 |
| **IDLE0** | 系统 | 692 | 692 | 692 | 692 | 692 | 692 | 692 |
| **IDLE1** | 系统 | 812 | 812 | 812 | 748 | 748 | 748 | 748 |
| **sleep_coord** | 3072 | 820 | 820 | 820 | 820 | 820 | 820 | 740 |
| **main** | 系统 | 1196 | 1196 | 1196 | 1196 | 1196 | 1196 | —(已退出) |
| **Tmr Svc** | 系统 | 1316 | 1316 | 1316 | 1316 | 1316 | 1316 | 1316 |
| **power_policy** | 4096 | 1692 | 1692 | 1692 | 1692 | 1692 | 1692 | 1692 |
| **power_service** | 3072 | 1716 | 1716 | 1716 | 1716 | 1716 | 1716 | 1716 |
| **wakeup_evidence** | 4096 | 2068 | 2068 | 2068 | 2068 | 2068 | 2068 | 1988 |
| **background_mgr** | 4096 | — | 2108 | 2108 | 2108 | 1900 | 1900 | 1900 |
| **network_service** | 4096 | — | — | 1772 | 1772 | 1772 | 1772 | 1724 |
| **network_mgr** | 3072 | — | — | 2300 | 2300 | 2300 | 2300 | 2300 |
| **tiT** | 系统 | — | — | 2508 | 2508 | 2508 | 2508 | 1724 |
| **esp_timer** | 系统 | 3348 | 3348 | 3092 | 3092 | 3092 | 3092 | 3092 |
| **wifi** | 系统 | — | — | 3420 | 3420 | 3420 | 3420 | 3420 |
| **time** | 6144 | — | — | 4068 | 4068 | 4068 | 4068 | 4068 |
| **official_chat_s** | 4096 | — | — | — | 3352 | 3352 | 3352 | 3352 |
| **cpu_monitor** | 4096 | — | — | — | — | 3392 | 3392 | 896 |
| **memory_watch** | 6144 | — | — | — | — | 3712 | 3712 | 2096 |
| **imu_service** | 4096 | — | — | — | — | — | 1948 | 1900 |
| **mw_cancel** | 3072 | — | — | — | — | 1076 | 1076 | 1076 |
| **mw_inbox** | 8192 | — | — | — | — | 7012 | 7012 | 4404 |
| **mw_health** | 10240 | — | — | — | — | 7880 | 7880 | 4248 |
| **mw_conv** | 10240 | — | — | — | — | 8320 | 8320 | 8320 |
| **mw_upload** | 24576 | — | — | — | — | 22508 | 22508 | 22508 |
| **swdraw** | 系统 | 7380 | 7380 | 7380 | 7380 | 6628 | 6628 | 6628 |
| **lvgl_task** | 10240 | 7844 | 7844 | 7844 | 7844 | 6772 | 6772 | 6772 |

### init 期间栈消耗 vs 稳态运行消耗

对比 "imu_service_ready（init 完成，3.94s）" 和 "cold_boot_snapshot（8s 稳态）"：

| task | init 完成时 free(B) | 稳态 free(B) | init→稳态 变化 | 说明 |
|---|---|---|---|---|
| **cpu_monitor** | 3392 | **896** | -2496 | 采样函数执行消耗大（uxTaskGetSystemState + 排序） |
| **memory_watch** | 3712 | **2096** | -1616 | health check 期间栈消耗大 |
| **mw_health** | 7880 | **4248** | -3632 | health check HTTP 请求消耗 |
| **mw_inbox** | 7012 | **4404** | -2608 | inbox poll 期间消耗 |
| **mw_upload** | 22508 | 22508 | 0 | init 后未运行 |
| **mw_conv** | 8320 | 8320 | 0 | init 后未运行 |
| **sleep_coord** | 820 | 740 | -80 | dry_run 轻微消耗 |
| **network_service** | 1772 | 1724 | -48 | WiFi 连接期间轻微消耗 |
| **wakeup_evidence** | 2068 | 1988 | -80 | rtc 采样轻微消耗 |
| **tiT** | 2508 | 1724 | -784 | timer 系统运行消耗 |
| **imu_service** | 1948 | 1900 | -48 | WoM 配置后轻微消耗 |
| **lvgl_task** | 7844→6772 | 6772 | -1072 | UI 渲染（setup_ui + controllers）消耗 |

### init 期间结论

- **init 期间栈消耗已被稳态采样覆盖**：栈高水位是累积最小值，init 期间的消耗如果比稳态更大则已被捕获，如果更小则不影响。实测显示所有 task 的稳态水位 ≤ init 时水位，说明 init 不是栈峰值来源。
- **栈峰值来自高压场景**（AI 对话、health check、inbox poll），不是 init。
- **internal RAM 峰值消耗在 network_service init**（WiFi+LWIP+PHY 一次吃 47KB），这是资源仲裁方案 Gate 1 需要关注的。
- **official_chat_ready 阶段的 37KB 降幅主要是 LVGL UI 渲染**（display bounce buffer 2×8200B + LVGL 单缓存 615KB PSRAM），不是 official_chat_s 本身。

---

## 2. 所有真实栈使用情况汇总（三阶段实测）

> 整合冷启动基线、高压场景峰值、扩缩栈后回归三阶段数据，作为资源仲裁的唯一事实依据。

### A. PSRAM 栈任务（WithCaps）

| task | 配置(B) | 冷启动free | 高压free(扩前) | 高压free%(扩前) | 扩缩后配置 | 扩缩后高压free | 扩缩后free% | 真实使用(B) | 判定 |
|---|---|---|---|---|---|---|---|---|---|
| **mw_health** | 6144 | 256 | 208 | 3.4%⚠️ | **10240** | 4240 | 41.4%✅ | 6000 | 扩栈后安全 |
| **mw_conv** | 6144 | 4232 | 200 | 3.3%⚠️ | **10240** | 4296 | 41.9%✅ | 5944 | 扩栈后安全 |
| **mw_upload** | 24576 | 22500 | 3172 | 12.9% | 24576(不动) | 22508 | 91.6% | 21404 | 高压必要，不可缩 |
| mw_inbox | 8192 | 3452 | 3292 | 40.2% | 8192(不动) | 3484 | 42.5% | 4900 | 保持 |
| mw_cancel | 3072 | 1084 | 1084 | 35.3% | 3072(不动) | 1084 | 35.3% | 1988 | 保持 |
| memory_watch | 6144 | 2104 | 2072 | 33.7% | 6144(不动) | 2096 | 34.2% | 4072 | 保持 |
| time | 8192 | 6116 | 4996 | 61.0% | **6144** | 2948 | 48.0% | 3196 | 已缩栈，安全 |

### B. internal 栈任务（本项目自建）

| task | 配置(B) | 冷启动free | 高压free(扩前) | 高压free%(扩前) | 扩缩后配置 | 扩缩后高压free | 扩缩后free% | 真实使用(B) | 判定 |
|---|---|---|---|---|---|---|---|---|---|
| **cpu_monitor** | 4096 | 988 | 928 | 22.7% | 4096(不动) | 896 | 21.9% | 3168 | 不动(<25%) |
| sleep_coord | 3072 | 820 | 708 | 23.1% | 3072(不动) | 740 | 24.1% | 2364 | 不动(<25%) |
| **power_service** | 4096 | 2704 | 2624 | 64.1% | **3072** | 964 | 31.4% | 2108 | 已缩栈，偏紧但安全 |
| power_policy | 4096 | 1692 | 1692 | 41.3% | 4096(不动) | 1692 | 41.3% | 2404 | 保持 |
| background_mgr | 4096 | 1896 | 1896 | 46.3% | 4096(不动) | 1900 | 46.4% | 2200 | 保持 |
| wakeup_evidence | 4096 | 1936 | 1936 | 47.3% | 4096(不动) | 1988 | 48.5% | 2160 | 保持 |
| imu_service | 4096 | 1888 | 1456 | 35.5% | 4096(不动) | 1900 | 46.4% | 2640 | 保持 |
| **network_service** | 6144 | 3804 | 3804 | 62.2% | **4096** | 1772 | 43.3% | 2340 | 已缩栈，安全 |
| **network_mgr** | 4096 | 3320 | 3192 | 78.0% | **3072** | 2300 | 74.9% | 904 | 已缩栈，安全 |
| **lvgl_task** | 10240 | 6772 | 5140 | 50.2% | 10240(不动) | 6772 | 66.1% | 5100 | 边界保持 |
| **official_chat_s** | 8192 | 7440 | 7440 | 90.8% | **4096** | 3348 | 81.7% | 748 | 已缩栈，安全 |

### C. 系统/第三方任务（配置由 IDF/WiFi/LVGL 决定，仅参考）

| task | 配置 | 冷启动free | 高压free | 说明 |
|---|---|---|---|---|
| ipc0 | 系统 | 388 | 388 | ESP-IDF IPC，不可改 |
| ipc1 | 系统 | 540 | 540 | ESP-IDF IPC，不可改 |
| sys_evt | 系统 | 604 | 592 | ESP-IDF event loop |
| IDLE0 | 系统 | 692 | 692 | FreeRTOS IDLE |
| IDLE1 | 系统 | 700 | 700 | FreeRTOS IDLE |
| Tmr Svc | 系统 | 1316 | 1316 | FreeRTOS Timer |
| tiT | 系统 | 1720 | 1640 | ESP-IDF timer |
| esp_timer | 系统 | 3092 | 3092 | esp_timer |
| wifi | 系统 | 3448 | 3448 | WiFi driver |
| swdraw | 系统 | 6552 | 6392 | esp_lcd 软件渲染 |
| oc_ssl_rx | 第三方 | — | 1608 | official_chat SSL 接收，动态创建 |

### D. 冷启动/高压未触发的任务（无栈数据）

| task | 配置(B) | 触发条件 | 备注 |
|---|---|---|---|
| RecTask | 4096 | 录音启动 | 已随 `audio_app` 删除（原 `audio_app.c:239`） |
| audio_alert | 4096 | 告警播放 | `audio_alert_player.c:16` |
| system_time_sync | 4096(PSRAM) | SNTP 同步 | `system_time_service.c:112` |
| traffic_audio_rt | 配置值 | 危险识别推理 | `traffic_audio_runtime.cc:98`，高压时动态创建，已发现 internal RAM 不足崩溃 |
| official_chat 子任务 | 各异 | 对话激活 | mqtt/esp_tcp/esp_ssl/afe/audio_io/opus |

### E. 内存快照三阶段对比

| 指标 | 冷启动基线 | 高压峰值(扩前) | 扩缩栈后冷启动 | 扩缩栈后高压 |
|---|---|---|---|---|
| internal RAM 已用 | 323KB (95.6%) | — | 302KB (89.4%) | 311KB (92.2%) |
| internal_free | 12246B | 10750B | **35482B** | 25726B |
| largest block | 9728B | **4608B**⚠️ | **17408B** | **18432B** |
| psram_free | 6920396B | 6910592B | 6954056B | 6927820B |

### F. 扩缩栈改动净效果

| 改动 | task | 变化 | 内存类型 |
|---|---|---|---|
| 🔴扩栈 | mw_health | 6144→10240 (+4096) | PSRAM |
| 🔴扩栈 | mw_conv | 6144→10240 (+4096) | PSRAM |
| 🟡缩栈 | official_chat_s | 8192→4096 (-4096) | internal |
| 🟡缩栈 | network_mgr | 4096→3072 (-1024) | internal |
| 🟡缩栈 | network_service | 6144→4096 (-2048) | internal |
| 🟡缩栈 | power_service | 4096→3072 (-1024) | internal |
| 🟡缩栈 | time | 8192→6144 (-2048) | PSRAM |
| **净效果** | — | PSRAM +6144B，internal **-8192B** | — |

**关键收益**：
- 消除 2 个栈溢出风险（mw_health 3.4%→41.4%，mw_conv 3.3%→41.9%）
- internal_free 从 12246→35482B（2.9倍），largest block 9728→17408B（+79%）
- **SSL `esp-aes: Failed to allocate memory` 不再出现**（largest block 从 4608→18432B）
- 真实最大栈使用者：mw_upload（21404B）、cpu_monitor（3168B）、mw_health（6000B）、mw_conv（5944B）

---

## 3. ESP-IDF 栈单位确认

ESP-IDF 头文件 `esp_additions/include/freertos/idf_additions.h` 明确：

```
@param usStackDepth The size of the task stack specified as the number of bytes
```

`xTaskCreateWithCaps` / `xTaskCreatePinnedToCoreWithCaps` / `xTaskCreate` / `xTaskCreatePinnedToCore` 的栈参数单位**统一为 bytes**。

⚠️ `main/services/memory_watch/memory_watch_service.c` 中常量命名 `kTaskStackWords` / `kUploadWorkerStackWords` / `kHealthWorkerStackWords` / `kCancelWorkerStackWords` / `kConversationWorkerStackWords` / `INBOX_WORKER_STACK_WORDS` 为**误导性命名**——实际按 bytes 处理，并非 words。建议后续重命名为 `*StackBytes`。

---

## 4. 附带发现：ESP-DL 推理崩溃

高压回归复测时设备在 54.9s 发生 `Guru Meditation Error: Core 0 panic'ed (StoreProhibited)` 并重启。

- **崩溃位置**：`traffic_audio_rt` 任务的 `dl::audio::Fbank::process_frame()`（`dl_fbank.cpp:16`），EXCVADDR=0x00000000（空指针写）
- **根因**：`Fbank` 构造函数用 `MALLOC_CAP_INTERNAL` 分配 `m_cache`（2KB），高压并发时 internal RAM 不足导致 `heap_caps_aligned_alloc` 返回 NULL，而 esp-dl `Fbank` 构造函数不检查返回值（同库 `Spectrogram` 类有 assert 但 `Fbank` 没有），后续 `process_frame` 中 `memcpy(m_cache=NULL)` 崩溃
- **触发条件**：第二次 AI 对话语音录制完成后，ESP-DL 危险识别推理启动，同时 WebSocket+SSL 占用大量 internal RAM
- **修复**：在 `espdl_feature_pipeline.cpp` 的 `espdl_feature_build_fbank` 构造 Fbank 前预检 internal RAM（free<6144 或 largest<4096 时返回 `ESP_ERR_NO_MEM`）；`runtime_task` 对 `ESP_ERR_NO_MEM` 跳过当前窗口而非退出整个 runtime
- **与扩缩栈无关**：`traffic_audio_rt` 栈大小未改动，是 internal RAM 枯竭导致的既有 bug
- 修复 attempt log：`docs/context/runs/2026-06-28-attempt-task-stack-resize.md`

---

## 5. 相关文档

- 观测能力建设：`docs/context/runs/2026-06-28-attempt-task-stack-measurement-instrumentation.md`
- 扩缩栈改动：`docs/context/runs/2026-06-28-attempt-task-stack-resize.md`
- 资源仲裁方案：`watch-resource-arbitration-report.md`（artifact）
- 实测方案：`task-stack-measurement-plan.md`（artifact）
- runtime owner 合同：`docs/context/knowledge/project/runtime-owner-contract.md`
