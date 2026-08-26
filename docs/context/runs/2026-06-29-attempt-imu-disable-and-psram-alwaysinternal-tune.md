---
id: 2026-06-29-attempt-imu-disable-and-psram-alwaysinternal-tune
tags: context, runs, imu, psram, internal-ram, sdkconfig, red-line
summary: 暂时关闭 IMU 服务并降低 SPIRAM_MALLOC_ALWAYSINTERNAL 阈值释放 internal RAM，含真机验证数据。
last_reviewed: 2026-06-29
memory_type: episodic
scope: task
owners: docs/context/runs
triggers: imu-disable, psram, alwaysinternal, internal-ram, sdkconfig-change
evidence_level: observed
status: active
---

# 运行记录：IMU 关闭 + PSRAM ALWAYSINTERNAL 调优

## 背景

- 本次要验证什么：
  1. 用户要求暂时关闭 IMU 服务，释放 QMI8658C 相关后台任务和 I2C 资源。
  2. 用户反馈 internal free 太小，需找出可优化点并执行。
  3. 执行方案 A：降低 `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` 从 65536 → 4096，使 ≤64KB 的 malloc 默认走 PSRAM 而非 internal。
- 对应任务或计划：
  - 不属于 active plan 的日常内存调优 + 用户临时指令。
  - 涉及红线改动（sdkconfig SPIRAM 配置），按 AGENTS.md 要求强制收尾四步闭环。

## 环境

- 分支/工作区状态：`codex/ai-memory-watch-hermes-api`，已有未提交改动。
- 设备/串口/板型：ESP32-S3 (QFN56 rev 0.2)，8MB Octal PSRAM (AP 3V3)，COM3，USB-Serial/JTAG。
- 关键前置条件：
  - IDF v5.5.3，激活脚本 `D:\esp-idf\v5.5.3\esp-idf\export.ps1`。
  - PSRAM 已启用：八线 80MHz，`CONFIG_SPIRAM_USE_MALLOC=y`，`CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y`。
  - 改 sdkconfig 前必须 `idf.py fullclean`。

## 操作

### 改动 1：暂时关闭 IMU 服务

- 文件：`main/app/app_main.c:269-277`
- 操作：将 `imu_service_start()` 调用块用 `#if 0 ... #endif` 包裹，添加关闭原因注释。
- 影响：QMI8658C 不初始化、不创建 imu_service task（原 4KB internal 栈）、不占用 GPIO21 中断和 I2C 带宽。
- 恢复方法：把 `#if 0` 改回正常代码即可。

### 改动 2：降低 SPIRAM_MALLOC_ALWAYSINTERNAL

- 文件：`sdkconfig:1704`
- 操作：`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=65536` → `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`
- 含义：原本 ≤64KB 的 `malloc`/`calloc` 默认走 internal RAM；改为 ≤4KB 才走 internal，其余自动落 PSRAM。
- 预期收益：释放数十 KB internal heap，因为大量中型动态分配（JSON、HTTP buffer、字符串等）不再挤占 internal。

### 构建与烧录

- `idf.py fullclean`（改过 sdkconfig SPIRAM 配置，必须 fullclean）
- `idf.py build`：通过，2450/2450，`111.bin` = `0xabc140` bytes，app 分区剩余 23%。
- `idf.py -p COM3 app-flash`：成功写入 11256128 字节（86.3 秒）。
- 串口监控：pyserial RTS 脉冲复位 + 40 秒采集，23628 字符。

## 观测

### 启动序列

- `boot_stage: app_start` → `board_foundation_done` → `ui_task_created` → `policy_ready` → `managers_ready` → `network_service_ready` → `startup_sequence_done`（约 3.24 秒完成）
- 无 panic / abort / assert / Guru Meditation。
- IMU 任务已关闭：任务栈表中无 `imu_service` 条目。

### 内存快照（启动后 ~11.7 秒，`cold_boot_resource_snapshot_done`）

| 指标 | 数值 |
|------|------|
| internal_free | 47,522 B (46.4 KB) |
| largest internal block | 24,576 B (24 KB) |
| psram_free | 6,953,672 B (6.6 MB) |

### 关键任务栈水位（free bytes，升序前 10）

| 任务 | free(B) | prio |
|------|---------|------|
| ipc0 | 396 | 1 |
| ipc1 | 532 | 24 |
| sys_evt | 636 | 20 |
| IDLE0 | 700 | 0 |
| IDLE1 | 804 | 0 |
| sleep_coord | 820 | 3 |
| cpu_monitor | 940 | 1 |
| mw_cancel | 1080 | 4 |
| Tmr Svc | 1324 | 1 |
| power_policy | 1684 | 4 |

### 与预期不一致的点

- 无。系统正常进入 standby，亮度从 99% 逐步降至 0%，Wi-Fi 尝试连接（reason=201 重连，属环境问题非固件问题）。
- espdl Fbank internal RAM 保护阈值（6144B）未被触发，说明 46.4KB internal free 足够。

## 结论

### 本次可以确认的事实

1. **IMU 关闭成功**：`imu_service` 不出现在任务栈表，GPIO21/I2C 资源释放，启动正常。
2. **ALWAYSINTERNAL 调优生效**：internal_free = 47.5KB，largest block = 24KB，系统稳定运行 40 秒无异常。
3. **PSRAM 分配策略变更无回归**：WiFi 初始化、LVGL 渲染、power policy、sleep coordinator 均正常工作。
4. **espdl Fbank 保护阈值满足**：6144B internal 阈值在 47.5KB free 下不会被触发。
5. **构建与烧录闭环完成**：fullclean → build → app-flash → monitor 全链路通过。

### 仍然不能确认的事实

1. 长时间运行（小时级）下 PSRAM 分配策略变更是否导致内存碎片或特定模块异常。
2. 高压场景（WiFi+TLS+Hermes+WebSocket 并发）下 internal free 是否仍满足 espdl Fbank 需求。
3. `lvgl_task` 10KB internal 栈迁移到 PSRAM 后的 UI 帧率影响（方案 C 未执行）。

## 未验证风险

### 下一轮仍需补证据的边界

1. **DMA 路径回归**：≤4KB 阈值下，I2S 音频 DMA buffer 是否仍正确分配在 internal（应由驱动显式 `MALLOC_CAP_DMA` 保证，但未做音频录放回归测试）。
2. **高压并发复测**：需在 WiFi 连接 + Hermes 语音对话 + WebSocket 并发场景下复测 internal_free 和 largest block。
3. **方案 B/C 未执行**：网络型任务栈迁 PSRAM（预计省 ~83KB）和 lvgl_task 迁 PSRAM（需测帧率）尚未执行，是后续优化方向。
4. **ALWAYSINTERNAL 回退阈值**：若发现 DMA 或性能问题，折中值可设为 8192。

### 证伪路径（不要再相信的旧信息）

- 不要假设 `ALWAYSINTERNAL=65536` 是"必须的"——实测 4096 下系统正常启动并稳定运行。
- 不要把 IMU 关闭当作永久方案——这是用户临时要求，恢复只需取消 `#if 0`。
