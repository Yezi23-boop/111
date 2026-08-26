---
id: attempt-planb-task-stack-psram
tags: context, runs, attempt-log, psram, task-stack, internal-ram, ble, memory-optimization
summary: 方案 B：将 9 个非 Flash/NVS/ISR 关键路径的任务栈从 Internal RAM 迁移到 PSRAM，释放 ~43 KB internal RAM，使 BLE presence preflight 门槛（64 KB free / 40 KB largest）被越过。internal free 从 47,378 B 升至 73,982 B。
last_reviewed: 2026-06-29
memory_type: task
scope: attempt
---

# Attempt: 方案 B — 任务栈迁 PSRAM 释放 Internal RAM

**日期**: 2026-06-29
**类型**: route-choice
**关联**: `docs/context/runs/2026-06-29-attempt-sdkconfig-wifi-ble-buffer-slim.md`

## 背景

sdkconfig 瘦身后 internal free = 47,378 B (46.3 KB)、largest = 23,552 B (23 KB)，距离 BLE presence preflight 门槛（64 KB free / 40 KB largest）仍差 ~18 KB / ~17 KB。

方案 B：将非 Flash/NVS/ISR 关键路径的任务栈从 Internal RAM 迁移到 PSRAM。

## 行动

### 第 1 批（4 个网络/后台任务）

| 任务 | 栈容量 | 文件 |
|------|:---:|------|
| `official_chat_service` | 4,096 B | `main/services/official_chat_service.c` |
| `network_mgr` | 3,072 B | `components/network_manager/src/network_manager.c` |
| `background_mgr` | 4,096 B | `main/services/background_service_manager.c` |
| `network_service` | 4,096 B | `main/services/network_service.c` |

- 改为 `xTaskCreateWithCaps(..., MALLOC_CAP_SPIRAM)` / `xTaskCreatePinnedToCoreWithCaps(..., MALLOC_CAP_SPIRAM)`
- 各文件补充 `#include "esp_heap_caps.h"`
- 编译通过（`111.bin` `0xabcc60`），真机验证：
  - internal free: 62,754 B (61.3 KB) — 距 64 KB 差 2,782 B
  - largest block: 38,912 B (38 KB) — 距 40 KB 差 2,048 B

### 第 2 批（3 个电源/唤醒任务）

| 任务 | 栈容量 | 文件 |
|------|:---:|------|
| `power_policy` | 4,096 B | `main/services/power_policy.c` |
| `power_service` | 3,072 B | `main/services/power_service.c` |
| `wakeup_evidence` | 4,096 B | `main/services/wakeup_evidence_service.c` |

- 风险分析：3 个任务均无 Flash/NVS/sleep 入口操作；`sleep_coordinator` 当前 DRY_RUN；`wakeup_evidence` 明确不进入 sleep
- 改为 `xTaskCreateWithCaps(..., MALLOC_CAP_SPIRAM)`，补充 `#include "esp_heap_caps.h"`
- 编译通过（`111.bin` `0xabcc70`）

## 真机验证结果

冷启动内存快照（COM3，日志 `board_logs/2026-06-29-planb-task-stack-psram-cold-boot.log`）：

| 指标 | sdkconfig 瘦身后 | 方案 B 第1批 | **方案 B+ 第2批** | BLE 门槛 |
|------|:--:|:--:|:--:|:--:|
| Internal free | 47,378 B | 62,754 B | **73,982 B** | ≥ 65,536 B |
| Largest block | 23,552 B | 38,912 B | **49,152 B** | ≥ 40,960 B |
| Internal RAM | 290 KB (86.0%) | 275 KB (81.5%) | **264 KB (78.3%)** | — |
| PSRAM | 1,401 KB (17.1%) | 1,416 KB (17.3%) | **1,427 KB (17.4%)** | — |

**累计释放**: ~43,982 B (~43 KB) internal RAM（相比优化前基线 ~30 KB free）。

所有迁移任务高水位正常（与迁移前一致），无 crash/panic。

## 结论

方案 B 成功。BLE presence preflight 门槛（64 KB free / 40 KB largest）已满足，可进入 BLE presence 真机实测阶段。

## 下一步

- 真机测试 BLE presence 启动（`ble_presence` preflight 应通过）
- 若 BLE presence 启动成功，验证 BLE 广播 + 配网流程
- 若 BLE 仍有其他内存问题（如 NimBLE 运行时动态分配），需单独分析
