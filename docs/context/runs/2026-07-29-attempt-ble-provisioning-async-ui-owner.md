---
id: run-ble-provisioning-async-ui-owner-2026-07-29
tags: context, runs, attempt-log, network-service, ble, provisioning, freertos, foreground-session
summary: 将 Wi-Fi 管理页 BLE/SoftAP provisioning 入口从 LVGL 同步调用迁到 network_service 异步 owner worker。
last_reviewed: 2026-07-29
memory_type: run
scope: network
status: partial
owners: main/services/network/network_service.c, main/services/network/network_service.h, main/ui/custom/wifi_management_controller.c, tests/test_network_service_ble_source.py, tests/test_wifi_management_controller_source.py
evidence_level: hardware
---

# Attempt Log: BLE Provisioning Async UI Owner

## 背景

- 阶段 3 目标要求 BLE provisioning 启停不在 LVGL 回调中同步执行。
- 复查 subagent 指出 Wi-Fi 管理页仍直接调用 `network_manager_start_ble_provisioning()` / `network_manager_start_softap_provisioning()`，会在 UI 线程里进入 manager mutex、presence stop 和 provisioning manager start。
- 普通 BLE presence 总开关已经具备 desired state、internal-stack worker、generation 和 gate，但 provisioning 入口仍未纳入 owner 调度。

## 改动

- `network_service_request_ble()` / `network_service_request_portal()` 从直接桥接 manager 改为返回 `esp_err_t` 的异步请求 API。
- `network_service` 增加 BLE/provisioning operation enum 和 provisioning request generation，由 network owner task 调度同一个 internal-stack worker。
- BLE provisioning start 在 worker 内先 acquire `FOREGROUND_RUNTIME_OWNER_BLE_PROVISIONING` gate，再调用 `network_manager_start_ble_provisioning()`；失败且本次申请了 gate 时立即 release。
- SoftAP provisioning start 也从 UI 线程迁到 worker 执行，但当前不持有 BLE foreground gate。
- `network_manager_stop_provisioning()` 暴露为窄 stop API，内部仍复用既有 `network_manager_stop_active_transport()`。
- `network_service_request_stop_provisioning()` 增加异步 stop operation；worker 调 `network_manager_stop_provisioning()`，成功且 BLE transport 不再 active 时释放 BLE foreground gate。
- `network_service` 增加独立 `s_ble_provisioning_gate_held` 标记，不再用最后一次 BLE operation 类型推断 gate 来源；ESP-IDF 自动结束 BLE provisioning 后，周期对账会释放这把临时 gate。
- SoftAP provisioning start 前会先检查并释放已经结束的 BLE provisioning gate，避免 BLE provisioning 自动结束与用户切 SoftAP 之间的 operation 覆盖导致 gate 泄漏。
- Wi-Fi 已连接且 BLE provisioning transport 仍 active 时，`network_service` 会自动投递 `STOP_PROVISIONING` 请求；真实 stop/deinit 继续由 internal-stack worker 调用 `network_manager_stop_provisioning()`，避免 `network_manager` 的 PSRAM 栈 monitor task 直接承担 NimBLE/provisioning manager 收尾。
- `network_service_snapshot_t` 增加 provisioning pending/error/generation：入口请求、自动 stop、worker 完成和 worker 创建失败都会发布结果，Wi-Fi 管理页可通过 300ms 刷新看到“启动中/配网失败”，不再把“请求入队成功”等同于 transport 启动成功。
- `network_service` 增加 Wi-Fi 管理页纯快照缓存，owner task 轮询 `network_manager_get_status()` 后发布 `network_service_wifi_status_t`；Wi-Fi 管理页改为调用 `network_service_get_wifi_status()`，不再由 LVGL timer 直接触发 `network_manager` runtime refresh。
- Wi-Fi 管理页返回按钮和 screen delete 回调只提交 stop 请求，不直接 stop/deinit provisioning。
- Wi-Fi 管理页按钮改为调用 `network_service_request_ble()` / `network_service_request_portal()`，不再直连 `network_manager_start_*_provisioning()`。
- source tests 更新为锁住 UI 不直连 provisioning manager，且 provisioning start 只出现在 network service worker 路径。

## 验证

- 复查 subagent 结论：普通 BLE presence 异步化已基本成立；P1 缺口是 provisioning 入口仍同步、gate 未覆盖 provisioning active 全期；P2 缺口是 `ble_presence_stop()` 超时后仍可能继续 deinit。
- `uv run python -m pytest tests/test_network_service_ble_source.py tests/test_wifi_management_controller_source.py tests/test_main_screen_ble_toggle_source.py -q`：12 passed。
- `ble_presence_stop()` 超时路径改为返回 `ESP_ERR_TIMEOUT`，不继续 `nimble_port_deinit()`，不清空 runtime；source test 锁定 host 未确认退出时 fail closed。
- `uv run python -m pytest tests/test_ble_presence_source.py tests/test_network_manager_source.py tests/test_network_service_ble_source.py tests/test_wifi_management_controller_source.py tests/test_main_screen_ble_toggle_source.py -q`：26 passed。
- `uv run python scripts/context/check_layering.py --verbose`：warning_count=0，known_exception_count=2。
- `uv run python -m pytest tests -q`：427 passed / 5 failed；失败项为当前工作树既有基线漂移，涉及 CO5300 queue depth、danger UI 坐标、official_chat partition、resources partition、UI font seam。
- `. D:/esp-idf/v5.5.3/esp-idf/export.ps1; idf.py build`：通过；最新 `111.bin` size `0xabdb50`，最小 app 分区剩余 `0x3424b0` / 23%。
- `scripts/board/agent_serial_monitor.ps1 -Port COM7 -Action app-flash-monitor -DurationSeconds 30 -FlashTimeoutSeconds 240 -Tag ble-provisioning-async-ui-owner -QuietConsole`：`panic_log_seen=false`；日志 `board_logs/2026-07-29-13-53-10-ble-provisioning-async-ui-owner.log` 显示 `BLE transition complete: enabled=0 generation=1 result=ESP_OK gate_held=0`，随后 Wi-Fi 到 `SERVICE_READY`。
- `scripts/board/agent_serial_monitor.ps1 -Port COM7 -Action app-flash-monitor -DurationSeconds 30 -FlashTimeoutSeconds 240 -Tag ble-presence-stop-timeout-fail-closed -QuietConsole`：`panic_log_seen=false`；日志 `board_logs/2026-07-29-13-59-12-ble-presence-stop-timeout-fail-closed.log` 显示 BLE transition 完成后 Wi-Fi 到 `SERVICE_READY`。
- `scripts/board/agent_serial_monitor.ps1 -Port COM7 -Action app-flash-monitor -DurationSeconds 30 -FlashTimeoutSeconds 240 -Tag ble-provisioning-stop-owner -QuietConsole`：`panic_log_seen=false`；日志 `board_logs/2026-07-29-14-06-03-ble-provisioning-stop-owner.log` 显示 BLE transition 完成后 Wi-Fi 到 `SERVICE_READY`。
- 复查 subagent 追加发现 P1：若 BLE provisioning 自动结束但 gate 尚未被下一轮 poll 释放，用户又提交 SoftAP start，会覆盖 `s_ble_operation_active`，旧 gate 可能失去释放条件。本轮改为独立记录 `s_ble_provisioning_gate_held`，并在 SoftAP start 前先释放已完成的 BLE provisioning gate。
- `uv run python -m pytest tests/test_ble_presence_source.py tests/test_network_manager_source.py tests/test_network_service_ble_source.py tests/test_wifi_management_controller_source.py tests/test_main_screen_ble_toggle_source.py -q`：28 passed。
- `uv run python scripts/context/check_layering.py --verbose`：warning_count=0，known_exception_count=2。
- `uv run python -m pytest tests -q`：429 passed / 5 failed；失败项仍为当前工作树既有基线漂移，涉及 CO5300 queue depth、danger UI 坐标、official_chat partition、resources partition、UI font seam。
- `git diff --check`：无 whitespace error，仅 LF/CRLF line ending warning。
- `. D:/esp-idf/v5.5.3/esp-idf/export.ps1; idf.py build`：通过；最新 `111.bin` size `0xabdc20`，最小 app 分区剩余 `0x3423e0` / 23%。
- `scripts/board/agent_serial_monitor.ps1 -Port COM7 -Action app-flash-monitor -DurationSeconds 30 -FlashTimeoutSeconds 240 -Tag ble-provisioning-gate-owner-flag`：`panic_log_seen=false`；日志 `board_logs/2026-07-29-14-20-54-ble-provisioning-gate-owner-flag.log` 显示 BLE transition 完成、Wi-Fi 从 `WIFI_READY` 到 `SERVICE_READY`。
- 自动收口分支补齐后：聚焦 BLE/network/UI source tests 29 passed；全量 tests 430 passed / 5 failed，失败仍为同一组既有基线漂移；`idf.py build` 通过，最新 `111.bin` size `0xabdd10`，最小 app 分区剩余 `0x3422f0` / 23%。
- `scripts/board/agent_serial_monitor.ps1 -Port COM7 -Action app-flash-monitor -DurationSeconds 30 -FlashTimeoutSeconds 240 -Tag ble-provisioning-auto-stop-on-wifi-connected`：`panic_log_seen=false`；日志 `board_logs/2026-07-29-14-32-15-ble-provisioning-auto-stop-on-wifi-connected.log` 显示 BLE transition 完成、Wi-Fi 到 `SERVICE_READY`。本次未人工进入 BLE provisioning，因此只证明冷启动无副作用；自动 stop 分支的真实触发仍需 Wi-Fi 页面配网完成路径验证。
- provisioning pending/error snapshot 补齐后：聚焦 BLE/network/UI source tests 30 passed；全量 tests 431 passed / 5 failed，失败仍为同一组既有基线漂移；`idf.py build` 通过，最新 `111.bin` size `0xabde30`，最小 app 分区剩余 `0x3421d0` / 23%。
- `scripts/board/agent_serial_monitor.ps1 -Port COM7 -Action app-flash-monitor -DurationSeconds 30 -FlashTimeoutSeconds 240 -Tag ble-provisioning-snapshot-ui-status`：`panic_log_seen=false`；日志 `board_logs/2026-07-29-14-44-29-ble-provisioning-snapshot-ui-status.log` 显示 BLE transition 完成、Wi-Fi 到 `SERVICE_READY`。本次未人工打开 Wi-Fi 管理页，因此只证明新增 snapshot/UI 读取没有冷启动副作用。
- 复查 subagent 追加发现 P1：旧 provisioning worker 完成时无条件清 `provisioning_transition_pending`，可能覆盖新请求的 pending/error。已按 generation current 修复：`network_service_finish_ble_provisioning()` 只有在 `s_ble_provision_request_generation == generation` 时才写 UI snapshot；旧 worker 仍可更新 applied generation 和完成通知。复查后聚焦 BLE/network/UI source tests 30 passed；全量 tests 431 passed / 5 failed；`idf.py build` 通过，`111.bin=0xabde30`。
- Wi-Fi 管理页 getter 纯化后：聚焦 BLE/network/UI source tests 34 passed；layering warning 0；全量 tests 431 passed / 5 failed，失败仍为同一组既有基线漂移；`git diff --check` 无 whitespace error，仅 line ending warning；`idf.py build` 通过，`111.bin=0xabdf90`，app free `0x342070` / 23%。COM7 `ble-provisioning-pure-wifi-snapshot` 30 秒 `panic_log_seen=false`，日志显示 `OFFLINE -> CONNECTING -> WIFI_READY -> SERVICE_READY`，未见 BLE/provisioning panic。复查 subagent 发现旧 provisioning error 可能压过后续已连接文案，已让错误分支额外要求 `!status.wifi_connected`；补测后聚焦 tests 34 passed，`idf.py build` 仍通过。真实页面点击路径仍待验证。

## 风险与下一步

- 本轮只把 Wi-Fi 页面 provisioning start 从 UI 线程迁到 network owner worker，不等于阶段 3 完成。
- BLE provisioning 已有页面退出 stop、Wi-Fi 连接后自动 stop、自动结束后 gate 对账释放、SoftAP 切换前 gate 收口以及 UI 可见的 pending/error snapshot；仍缺真实 Wi-Fi 页面 BLE Provision 进入/退出与配网完成路径验证。
- 普通 presence 的 `ble_presence_stop()` 超时 fail-closed 已处理：host 未确认退出时不再继续 deinit/清空 runtime。
- 后续最小下一步：做一次真实 Wi-Fi 页面 BLE Provision 进入/退出验证；如能触发配网完成事件，再确认自动 stop/deinit/release、普通 BLE presence 恢复策略与 gate snapshot 一致。
