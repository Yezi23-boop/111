---
id: attempt-ble-presence-internal-heap-guard
tags: context, runs, attempt-log, ble, nimble, network-manager, internal-heap, watchdog, esp32s3
summary: 记录点击主界面 Bluetooth 后 BLE controller 因 internal heap 大块申请失败触发 `emi.c` assert / interrupt WDT 的证据、判断与最小防护修复。
last_reviewed: 2026-06-27
memory_type: episodic
scope: task
result: success
owners: components/ble_presence, components/network_manager
triggers: BLE_INIT Malloc failed, emi.c, Bluetooth button, ble_presence, internal heap, NimBLE
evidence_level: observed
record_reasons: error-signature, evidence
---

# Attempt Log: BLE Presence Internal Heap Guard

## 背景

- 用户日志显示 Hermes / AI Memory Watch 启动链路正常：
  - `memory_watch_ready`
  - `network state: WIFI_READY -> SERVICE_READY`
  - `watch endpoint health result: hermes_online=1 err=ESP_OK`
  - `inbox: poll ok items=0 unread=0`
- 崩溃发生在用户点击主界面 Bluetooth 后：
  - `main_dropdown: Bluetooth button clicked: ble_enabled=0 ble_active=0`
  - `BLE_INIT: Malloc failed`
  - `BLE assert emi.c 164, param 00000000 00007800`
  - `Guru Meditation Error: Core 0 panic'ed (Interrupt wdt timeout on CPU0)`
- backtrace 落在 BT controller 初始化：
  - `r_lld_core_init`
  - `r_lld_init`
  - `r_rwble_init`
  - `r_rwip_reset`
  - `btdm_controller_on_reset`
  - `btdm_controller_task`

## 判断

- 这不是 Hermes WebSocket、ASR-first UI 时序或 inbox polling 导致的崩溃。
- 触发点是普通 BLE presence 启动路径：`main_dropdown_controller -> network_manager_set_ble_enabled(true) -> network_manager_sync_ble_presence() -> ble_presence_start() -> nimble_port_init()`。
- 日志中 `param 00007800` 对应约 30 KiB 大块需求，说明 BT controller 初始化阶段需要 internal 8-bit heap 的连续块；当 Wi-Fi、LVGL、Hermes 相关后台服务已运行时，internal heap 可能碎片化或余量不足。

## 修复

- `components/ble_presence/src/ble_presence.c`
  - 在 `nimble_port_init()` 前新增 `ble_presence_check_internal_heap()`。
  - 检查 `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT` 的 free bytes 与 largest free block。
  - 当前门槛：free >= 64 KiB，largest block >= 40 KiB。
  - 不满足时返回 `ESP_ERR_NO_MEM`，避免进入已知会触发 controller assert 的路径。
- `components/network_manager/src/network_manager.c`
  - `network_manager_sync_ble_presence()` 在 `ble_presence_start()` 失败时回滚：
    - `ble_control_set_active(false)`
    - `ble_control_set_enabled(false)`
  - 这样用户点击失败后 UI 可以 toast 失败，后台 250ms 状态同步也不会因为默认 BLE enabled 偏好反复尝试启动。
- 用户复测后新增发现：
  - 新日志没有再出现 `emi.c` assert、Guru 或 interrupt WDT。
  - 但 BLE presence 在 `network_manager_start()` 读到 latest Wi-Fi 后自动启动，早于 LVGL display bounce buffer 和 SPI DMA 私有 TX buffer 申请。
  - 结果 BLE 正常 advertising 后，显示链路连续报 `Failed to allocate display bounce buffer`、`setup_dma_priv_buffer failed`、`Display flush failed: ESP_ERR_NO_MEM`。
- 追加修复：
  - 将 `network_manager_sync_ble_presence()` 改为 `network_manager_sync_ble_presence(bool allow_start)`。
  - 后台刷新、开机 latest Wi-Fi 路径、自动回退路径、transport stop/softap 路径均传 `false`，只允许 stop/收口，不允许自动 start 普通 BLE presence。
  - 只有 `network_manager_set_ble_enabled(true)` 这条用户显式 Bluetooth 开关路径传 `true`，保留手动启动普通 BLE 广播能力。

## 验证

- 源码测试：
  - `uv run python -m pytest tests/test_ble_presence_source.py tests/test_network_manager_source.py tests/test_main_screen_ble_toggle_source.py -q`
  - 首轮结果：`14 passed`
  - 追加自动启动边界后结果：`15 passed`
- ESP-IDF 编译：
  - `. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`
  - 首轮结果：通过，`111.bin binary size 0xabdea0 bytes`，最小 app 分区剩余 `0x342160 bytes (23%)`。
  - 追加自动启动边界后结果：通过，`111.bin binary size 0xabdeb0 bytes`，最小 app 分区剩余 `0x342150 bytes (23%)`。
- 最新真机冷启动复测：
  - 开机阶段未出现 `BLE_INIT` / `ble_presence` / `BLE presence advertising: ESP32S3-723C`。
  - LVGL 显示链路恢复：`Successfully allocated display bounce buffer 0/1 of size 8200`。
  - 未再出现 `Failed to allocate display bounce buffer`、`setup_dma_priv_buffer failed`、`Display flush failed: ESP_ERR_NO_MEM`、`emi.c`、Guru 或 panic。
  - 网络与 Hermes 仍正常：Wi-Fi 进入 `SERVICE_READY`，`watch endpoint health result: hermes_online=1 err=ESP_OK`，inbox poll job 已派发。
- 最新真机手动点击 Bluetooth 复测：
  - UI 多次点击主界面 Bluetooth 后，均停在 fail-closed 路径：`Bluetooth button clicked: ble_enabled=0 ble_active=0`。
  - `ble_presence` 报 internal heap 不足并拒绝进入 NimBLE：`free=31203/30851 largest=14336 min_free=65536 min_largest=40960`。
  - UI 显示 `BLE switch update failed`，`network_manager` 返回 `ESP_ERR_NO_MEM`。
  - 未再出现 `BLE_INIT: Malloc failed`、`emi.c` assert、Guru、interrupt WDT，也未破坏显示链路或 Hermes health/inbox。

## 结论

- 已完成最小防护：internal heap 不足时普通 BLE presence 启动会 fail closed，不再进入 BT controller assert 路径。
- 已完成启动时序防护：NVS 中 BLE enabled 偏好为 true 时，开机后台不会自动启动普通 BLE presence，避免抢占 LVGL/SPI DMA 需要的 internal RAM。
- 这次修复不修改 `official_chat`，不修改 Hermes 业务协议，不改变 BLE provisioning owner 边界。
- 最新真机冷启动与手动点击 Bluetooth 复测均已通过防护目标：开机阶段不会自动启动普通 BLE presence，显示 DMA internal RAM 错误消失；手动点击在 internal heap 不足时稳定 fail closed。

## 后续

- 如果用户仍需要 Wi-Fi 已连接、Hermes 后台服务运行时也稳定开启普通 BLE presence，下一步应做 internal RAM 预算收敛，而不是继续提高 guard 门槛。
- 可选方向包括：延迟/关闭普通 BLE presence 默认启动、收紧 NimBLE controller 配置、减少同时在线后台任务的 internal RAM 占用、把可迁移 buffer 放 PSRAM。
