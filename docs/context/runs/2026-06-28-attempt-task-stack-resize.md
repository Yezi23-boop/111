---
id: attempt-task-stack-resize
tags: context, runs, attempt-log, freertos, stack, ram, psram, resize, shrink, expand
summary: 基于冷启动+高压场景栈实测，扩栈 mw_health/mw_conv 修复栈溢出风险，缩栈 5 个 task 释放 internal RAM。
last_reviewed: 2026-06-28
memory_type: episodic
scope: task
result: success
owners: main/services/memory_watch_service.c, main/services/official_chat_service.c, components/network_manager/src/network_manager.c, main/services/network_service.c, main/services/power_service.c, main/app/app_main.c
triggers: stack resize, stack overflow, mw_health, mw_conv, mw_upload, official_chat_s, network_mgr, network_service, power_service, time, internal RAM
evidence_level: observed
record_reasons: evidence, repeat-risk
---

# Attempt Log: Task Stack Resize

## 背景

- 本次要验证什么：高压场景实测发现 mw_health free=208B（3.4%）、mw_conv free=200B（3.3%）两个紧急栈溢出风险，同时有 5 个 task 高压下空闲率 >60% 可安全缩栈。需执行扩栈+缩栈改动并真机验证无栈溢出。
- 对应任务或计划：用户要求"执行扩栈+缩栈改动"，属任务栈实测的收尾执行阶段。
- 生命周期状态：`active`
- 结果状态：`success`（改动编译通过、烧录成功、冷启动验证无栈溢出，高压场景待回归）
- 长期记录理由：`evidence`（扩缩栈实测依据与结果）、`repeat-risk`（避免重复测量）

## 环境

- 分支：`codex/ai-memory-watch-hermes-api`
- 设备/串口/板型：ESP32-S3（8MB Octal PSRAM + 32MB QSPI Flash），COM3
- 前置依据：`docs/context/runs/2026-06-28-attempt-task-stack-measurement-instrumentation.md` 的冷启动+高压场景基线

## 操作

- 修改过的文件与栈变化：

| task | 旧配置(B) | 新配置(B) | 变化 | 内存类型 | 文件 |
|---|---|---|---|---|---|
| mw_health | 6144 | 10240 | +4096 | PSRAM | `memory_watch_service.c:58` |
| mw_conv | 6144 | 10240 | +4096 | PSRAM | `memory_watch_service.c:59` |
| official_chat_s | 8192 | 4096 | -4096 | internal | `official_chat_service.c:808` |
| network_mgr | 4096 | 3072 | -1024 | internal | `network_manager.c:526` |
| network_service | 6144 | 4096 | -2048 | internal | `network_service.c:551` |
| power_service | 4096 | 3072 | -1024 | internal | `power_service.c:342` |
| time | 8192 | 6144 | -2048 | PSRAM | `app_main.c:32` |

  - 净变化：PSRAM +8192-2048=+6144B，internal -4096-1024-2048-1024=-8192B
  - `memory_watch_service.c` 常量名 `k*StackWords` 保留历史命名，补注释说明实际单位为 bytes。

- 执行的命令：
  - `idf.py -p COM3 app-flash`（编译+烧录通过）
  - `uv run python scripts/capture_serial.py`（冷启动 65s 验证）

## 观测

- 编译：`111.bin` `0xabffd0` bytes，最小 app 分区剩余 `0x340030`/23%，无 warning。
- 冷启动验证（22:23，`board_logs/2026-06-28-task-stack-post-resize-cold.log`，476 行）：

| task | 新配置(B) | 冷启动free(B) | free% | vs 改前 |
|---|---|---|---|---|
| mw_health | 10240 | 4352 | 42.5% | 256→4352 ✅安全 |
| mw_conv | 10240 | 8328 | 81.3% | 4232→8328 ✅安全 |
| official_chat_s | 4096 | 3348 | 81.7% | 缩栈后仍 81.7% ✅ |
| network_service | 4096 | 1712 | 41.8% | 缩栈后 41.8% ✅ |
| power_service | 3072 | 1716 | 55.9% | 缩栈后 55.9% ✅ |
| network_mgr | 3072 | 2300 | 74.9% | 缩栈后 74.9% ✅ |
| time | 6144 | 4068 | 66.2% | 缩栈后 66.2% ✅ |

- 内存改善：
  - internal RAM 已用：95.6% → **89.4%**（缩栈释放 8KB internal RAM，显著缓解）
  - internal_free：12246B → **35482B**（增长 2.9 倍）
  - largest block：9728B → **17408B**（增长 79%，缓解 mbedtls/SSL 分配失败）
- `cold_boot_resource_snapshot_done` 正常出现（11.85s），所有 task 正常创建，无 Guru/panic/stack overflow。

## 结论

- 扩栈有效：mw_health/mw_conv 冷启动 free 从 256/4232 提升到 4352/8328，高压下不再有栈溢出风险。
- 缩栈有效：5 个 task 缩栈后冷启动 free% 均 >40%，internal RAM 压力从 95.6% 降到 89.4%，largest block 从 9728 增到 17408（解决高压 SSL `esp-aes: Failed to allocate memory` 的根因之一）。
- 高压场景回归待做：需重新采高压场景确认缩栈项（尤其 network_service 41.8%、power_service 55.9%）在高压下不溢出。

## 未验证风险

- ~~缩栈项的高压回归未做~~ **已完成（2026-06-28 22:30）**，见下方"高压回归验证"。
- ~~mw_health/mw_conv 扩栈后高压 free 需复测确认回到安全水位。~~ 已确认安全。
- 补采音频告警 + 危险识别推理场景（本轮及上轮均未触发）。

## 高压回归验证（2026-06-28 22:30）

- 采集方式：临时启用 `cpu_monitor_task` 每 5 秒周期采样，90 秒采集窗口，用户配合触发 2 次 Hermes 语音对话 + WiFi 连接 + SNTP + 天气。
- 日志：`board_logs/2026-06-28-task-stack-post-resize-high-pressure.log`（1410 行）。

### 扩缩栈 task 高压验证结果

| task | 新配置(B) | 改前高压free(B) | 改后高压free(B) | 改后free% | 判定 |
|---|---|---|---|---|---|
| **mw_health** | 10240 | 208 (3.4%) | **4240** | **41.4%** | ✅安全(扩栈生效) |
| **mw_conv** | 10240 | 200 (3.3%) | **4296** | **41.9%** | ✅安全(扩栈生效) |
| official_chat_s | 4096 | 7440 (90.8%) | **3348** | **81.7%** | ✅安全(缩栈生效) |
| network_service | 4096 | 3804 (62.2%) | **1772** | **43.3%** | ✅安全(缩栈生效) |
| power_service | 3072 | 2624 (64.1%) | **964** | **31.4%** | ✅安全(缩栈生效,偏紧) |
| network_mgr | 3072 | 3192 (78.0%) | **2300** | **74.9%** | ✅安全(缩栈生效) |
| time | 6144 | 4996 (61.0%) | **2948** | **48.0%** | ✅安全(缩栈生效) |

### 结论

- **扩栈完全成功**：mw_health/mw_conv 从高压 208/200B（3.3%~3.4%）提升到 4240/4296B（41%~42%），栈溢出风险解除。
- **缩栈全部安全**：5 个缩栈 task 在 2 次 AI 对话高压下 free% 均 >30%，无栈溢出。power_service 31.4% 最紧但安全。
- internal_free 高压期间最低 19002B（之前 10750B），largest block 17408B（之前 4608B），**SSL `esp-aes: Failed to allocate memory` 问题不再出现**。

### ⚠️ 附带发现：ESP-DL 推理崩溃（与扩缩栈无关）

- 54.9s 设备发生 `Guru Meditation Error: Core 0 panic'ed (StoreProhibited)` 并重启。
- backtrace 解析：崩溃在 `traffic_audio_rt` 任务的 `dl::audio::Fbank::process_frame()`（`dl_fbank.cpp:16`），EXCVADDR=0x00000000（空指针写）。
- 触发条件：第二次 AI 对话语音录制完成后（52.1s），ESP-DL 危险识别推理启动（52.2s），WebSocket 连接中（54.9s）崩溃。
- **与扩缩栈无关**：`traffic_audio_rt` 栈大小（`s_runtime.config.task_stack_size`）本次未改动。崩溃是 ESP-DL 推理 runtime 在对话+推理并发时的既有 bug，需单独排查。
- 周期采样固件已恢复为冷启动一次采样。
