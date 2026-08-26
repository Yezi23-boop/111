---
id: attempt-sdkconfig-wifi-ble-buffer-slim
tags: context, runs, attempt-log, sdkconfig, wifi, nimble, ble, internal-ram, memory-optimization
summary: sdkconfig Wi-Fi/NimBLE 缓冲区瘦身与无用 BLE Service 关闭，预计释放 internal RAM 约 33.3 KB 帮助 BLE presence 越过 internal heap 门槛。
last_reviewed: 2026-06-29
memory_type: episodic
scope: task
result: success
owners: sdkconfig
triggers: sdkconfig, Wi-Fi TX buffer, NimBLE MSYS2, ACL, EVT, BLE service, internal RAM, BLE presence
evidence_level: observed
record_reasons: route-choice, evidence
---

# Attempt: sdkconfig Wi-Fi/NimBLE 缓冲区瘦身与无用 BLE Service 关闭

- **日期**：2026-06-29
- **类型**：route-choice
- **证据级别**：observed
- **相关计划**：无独立 active plan；为 BLE presence internal heap 门槛准备的 sdkconfig 级内存优化。
- **设备/板型**：ESP32-S3 (QFN56 rev 0.2)，8MB Octal PSRAM
- **IDF 版本**：v5.5.3 (`D:\esp-idf\v5.5.3\esp-idf\export.ps1`)

## 目标

通过缩小 Wi-Fi/NimBLE 缓冲区配置并关闭 10 个未使用的 NimBLE GATT service，释放 internal RAM 约 33.3 KB，帮助冷启动后 internal free 越过 BLE presence 安全门槛（64 KB/40 KB）。

## 背景

冷启动后 internal heap 约 30 KB free、最大连续块约 14 KB，低于 BLE presence 门槛。之前在 CHANGELOG 记录的 0xabcc80 基线内存紧张。

两条关键约束：
1. `ESP_WIFI_DYNAMIC_TX_BUFFER_NUM` 历史上曾不足导致 UDP 音频 `errno=12`，32 是修复后值。
2. 10 个 BLE service 经全仓库搜索确认 0 处 `_init()` 调用，BLE 配网使用 `protocomm_nimble` 自定义 UUID。

## 修改清单

| 配置项 | 旧值 | 新值 | 省 internal |
|--------|:--:|:--:|:--:|
| `ESP_WIFI_DYNAMIC_TX_BUFFER_NUM` | 32 | 24 | ~12.8 KB |
| `ESP32_WIFI_DYNAMIC_TX_BUFFER_NUM` | 32 | 24 | 同上 |
| `ESP_WIFI_MGMT_SBUF_NUM` | 32 | 16 | ~12 KB |
| `ESP32_WIFI_MGMT_SBUF_NUM` | 32 | 16 | 同上 |
| `BT_NIMBLE_MSYS_2_BLOCK_COUNT` | 24 | 16 | ~2.5 KB |
| `BT_NIMBLE_TRANSPORT_ACL_FROM_LL_COUNT` | 24 | 16 | ~2 KB |
| `BT_NIMBLE_TRANSPORT_EVT_COUNT` | 30 | 16 | ~1 KB |
| 10 个 BLE service | y | not set | ~3 KB |

关闭的 service：PROX / ANS / CTS / HTP / IPSS / TPS / IAS / LLS / SPS / HR。
保留的 service：GAP（NimBLE 强制）、BAS（电池）、DIS（设备信息）。

## 执行

1. 直接在 `sdkconfig` 中手动修改 8 处数值 + 10 个 service 开关。
2. `idf.py fullclean && idf.py build`：通过，2453/2453 步骤全部完成。
3. `111.bin binary size 0xabcc40 bytes`，最小 app 分区剩余 `0x3433c0 bytes (23%)`，编译无 warning/error。

## 结果

- 编译通过，无 Kconfig 冲突。
- 与之前基线（`0xabcc80`）相比 bin 大小缩减 64 bytes（省掉的 service GATT 注册代码 + 静态结构）。
- 无串口设备在线，本 attempt 未做真机 flash/monitor 验证。internal RAM 实际省出量需真机 `printf_esp32_memory_stats()` 验证。

### 2026-06-29 真机验证（11:06）

- `idf.py -p COM3 app-flash` 成功。
- 冷启动 90 秒采集完整（日志：`board_logs/2026-06-29-sdkconfig-slim-cold-boot-memory-snapshot.log`）。
- 关键数据：
  - Internal RAM: **290 KB / 338 KB (86.0%)**
  - Internal free: **47,378 B** (46.3 KB)
  - Largest free block: **23,552 B** (23 KB)
  - PSRAM: 1,401 KB / 8,192 KB (17.1%)
- 与优化前基线（internal free ≈ 30 KB, largest ≈ 14 KB）相比：
  - 释放 ~16 KB internal free ✅
  - 释放 ~9 KB largest block ✅
- **BLE presence 门槛（64 KB free / 40 KB largest）仍未达标** ❌：
  - Internal free 差 ~18 KB（47,378 vs 65,536）
  - Largest block 差 ~17 KB（23,552 vs 40,960）

## 下一步

- ~~真机 `app-flash` + 冷启动 `printf_esp32_memory_stats()` 确认 internal free 是否 ≥ 64 KB~~（已完成，结论：不足）
- ~~若 internal free ≥ 64 KB，重新测 BLE presence 是否可成功启动~~（跳过，未达标）
- **启动方案 B：网络型任务栈迁 PSRAM**，需再释放约 18 KB internal。候选任务栈：
  - `wifi` (stack 4,320 B high-water)：Wi-Fi 相关栈
  - `network_service` (stack 1,796 B high-water)
  - `network_mgr` (stack 2,240 B high-water)
  - `official_chat_s` (stack 3,356 B high-water)
  - `time` (stack 4,068 B high-water)
  - `background_mgr` (stack 1,872 B high-water)
  - `mw_cancel` (stack 1,080 B high-water)
  - 以上 7 个任务栈累计可释放约 18.7 KB internal（如栈全部分配在 internal）

## 已知风险

- Wi-Fi TX buffer 从 32 降到 24 是历史 `errno=12` 修复后的下限；如果 UDP 音频流与后台 HTTPS 并发时出现 `errno=12`，需回退到 32（损失 12.8 KB internal）。
- NimBLE buffer 缩减（MSYS_2 / ACL / EVT）在 provisioning 高并发下未经过真机压力测试，若 provisioning 出现 GATT `ENOMEM` 或 `RESOURCES` 错误，需回退相应 buffer 到旧值。
