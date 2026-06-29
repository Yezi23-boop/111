---
id: attempt-runtime-resource-gate-board-stress
tags: context, runs, attempt-log, runtime-resource-gate, board-test, com3, ble, background-https-gate, foreground-runtime-gate, freertos, ram
summary: Watch Runtime Resource Gate 阶段 6 的板端自动高压回归：新增默认关闭的 Kconfig 测试入口，COM3 验证 gate/quiet/BLE fail-closed 链路；公网 HTTPS 成功路径待 Wi-Fi 可用后补测。
last_reviewed: 2026-06-29
memory_type: episodic
scope: task
status: active
result: partial
owners: main/services/runtime_resource_gate_board_test.c, main/services/runtime_resource_gate_board_test.h, main/app/app_main.c, main/Kconfig.projbuild, tests/test_runtime_resource_gate_board_test_source.py
triggers: runtime resource gate board stress, COM3, BLE fail closed, PSRAM stack, internal stack, background HTTPS quiet window
evidence_level: observed
record_reasons: error-signature, evidence, repeat-risk
---

# Attempt Log: Runtime Resource Gate Board Stress

## 背景

- 本次承接 `docs/context/plans/active/2026-06-29-watch-runtime-resource-gate-plan.md` 阶段 6 真机高压回归。
- 用户允许“代码上做个测试代码”，目标是在不依赖手动连续操作的情况下，自动触发 Hermes foreground、后台 HTTPS gate、BLE owner/quiet window 等资源冲突路径。
- 该测试入口必须默认关闭，避免正常固件开机自动压测。

## 操作

- 新增默认关闭的板端测试入口：
  - `main/services/runtime_resource_gate_board_test.c`
  - `main/services/runtime_resource_gate_board_test.h`
  - `tests/test_runtime_resource_gate_board_test_source.py`
- 接入：
  - `main/Kconfig.projbuild`
  - `main/CMakeLists.txt`
  - `main/app/app_main.c`
  - `tests/main_paths.py`
- Kconfig：
  - `CONFIG_RUNTIME_RESOURCE_GATE_BOARD_TEST` 默认 `n`。
  - `CONFIG_RUNTIME_RESOURCE_GATE_BOARD_TEST_START_DELAY_MS` 默认 `12000`。
  - `CONFIG_RUNTIME_RESOURCE_GATE_BOARD_TEST_ENABLE_REAL_BLE_TOGGLE` 默认 `n`。
- 测试任务覆盖：
  - Hermes 前台 acquire/release。
  - `background_service_manager` foreground runtime 重算。
  - `background_https_gate` busy / quiet / deny。
  - Memory Watch health / inbox poll。
  - BLE foreground owner + background HTTPS quiet window。
  - 可选真实 BLE enable/disable。

## 关键发现

- 自动 gate 压测、不真实打开 BLE 时，COM3 完整跑完，无 Guru、panic、stack overflow、NO_MEM。
- 真实 BLE toggle 第一次在 PSRAM task stack 上触发 cache-disabled 断言：
  - `assert failed: spi_flash_disable_interrupts_caches_and_other_cpu cache_utils.c:127 (esp_task_stack_is_sane_cache_disabled())`
  - 调用链经过 `ble_control_store_enabled_pref`、`ble_control_set_enabled`、`network_manager_set_ble_enabled`、`network_service_set_ble_enabled`。
- 根因：真实 BLE toggle 会写 BLE NVS 偏好，NVS/flash 写入期间 cache 可能关闭；如果当前 task stack 在 PSRAM，会触发 cache-disabled stack sanity 断言。
- 修复：当 `CONFIG_RUNTIME_RESOURCE_GATE_BOARD_TEST_ENABLE_REAL_BLE_TOGGLE=y` 时，测试任务使用 `MALLOC_CAP_INTERNAL` 栈；默认模拟模式仍可用 `MALLOC_CAP_SPIRAM`。
- 修复后真实 BLE toggle 高压路径不再断言崩溃，BLE guard 返回 `ESP_ERR_NO_MEM` 并 fail closed，随后 `real_ble_disable` 返回 `ESP_OK`。
- 收尾时已刷回默认关闭测试的正常固件，并用 45 秒 COM3 启动监控确认不会自动出现 `runtime_gate_test` 压测日志。

## 证据

板端日志：

- `board_logs/2026-06-29-10-18-11-runtime-resource-gate-board-test-auto.log`
- `board_logs/2026-06-29-10-18-11-runtime-resource-gate-board-test-auto.summary.json`
- `board_logs/2026-06-29-10-24-50-runtime-resource-gate-board-test-real-ble.log`
- `board_logs/2026-06-29-10-31-09-runtime-resource-gate-board-test-real-ble-internal-stack.log`
- `board_logs/2026-06-29-10-31-09-runtime-resource-gate-board-test-real-ble-internal-stack.summary.json`
- `board_logs/2026-06-29-10-42-20-runtime-resource-gate-normal-after-test.log`
- `board_logs/2026-06-29-10-42-20-runtime-resource-gate-normal-after-test.summary.json`

关键串口结论：

- `foreground acquired: owner=HERMES`
- `resource_blocked_change ... owner=HERMES`
- `fg_runtime=1`
- 后台 HTTPS 第二次 acquire 返回 `ESP_ERR_TIMEOUT`
- quiet window 内 acquire 返回 `ESP_ERR_INVALID_STATE`
- inbox poll 在 quiet window 中被 `background_https_gate` 拒绝
- BLE owner acquire/release 成功
- 修复后真实 BLE toggle 不再出现 cache-disabled 断言
- 修复后 BLE guard 在 internal heap 不足时 fail closed：`real_ble_enable: result=ESP_ERR_NO_MEM`

自动 gate 压测结束内存：

- `internal_free=47790`
- `largest=24576`
- `psram_free=6949572`

真实 BLE toggle 修复后结束内存：

- `internal_free=43690`
- `largest=20480`
- `psram_free=6953672`

## 验证

Source tests：

```powershell
uv run python -m unittest tests.test_runtime_resource_gate_board_test_source tests.test_background_https_gate_source tests.test_foreground_runtime_gate_source tests.test_safety_monitor_session_source tests.test_main_screen_ble_toggle_source tests.test_memory_watch_service_source tests.test_time_weather_source
```

结果：`Ran 39 tests ... OK`。

构建：

- 默认测试关闭构建通过：`111.bin` `0xabcc80`，最小 app 分区剩余 `0x343380`（23%）。
- 测试开启、真实 BLE toggle 关闭构建通过：`111.bin` `0xabd410`。
- 测试开启、真实 BLE toggle 打开构建通过：`111.bin` `0xabd480`。
- 收尾时已恢复 `sdkconfig`：`# CONFIG_RUNTIME_RESOURCE_GATE_BOARD_TEST is not set`，并重新执行 `idf.py fullclean; idf.py build` 通过。
- 收尾刷回正常固件：

```powershell
.\scripts\board\agent_serial_monitor.ps1 -Port COM3 -Action app-flash-monitor -DurationSeconds 45 -FlashTimeoutSeconds 240 -Tag runtime-resource-gate-normal-after-test -Pattern 'runtime_gate_test|Guru|panic|stack overflow|ESP_ERR_NO_MEM|SERVICE_READY|memory_watch|BLE|background_https'
```

结果：`AGENT_SERIAL_MONITOR_STATUS=ok`。45 秒启动日志无 `runtime_gate_test`、Guru、panic、stack overflow；Wi-Fi 因 reason=201 未连上，启动进入 UI、Memory Watch ready、display bounce buffer 分配成功。

## 未覆盖

- Wi-Fi 在本轮 COM3 自动压测中未连上，公网 HTTPS 成功路径未覆盖；health/inbox 只验证了 gate/quiet/失败路径。
- Safety Monitor 当前阻塞原因包含 `user_disabled`，所以本轮证明的是 foreground runtime flag 与 manager 重算，不是“正在运行的 ESP-DL 被强前台暂停”的完整动态路径。
- `agent_serial_monitor.ps1` 对真实 BLE 修复后日志给出 fail 状态，但日志中未见 panic/Guru/stack overflow；推测是 fatal matcher 把预期的 `ESP_ERR_NO_MEM` fail-closed 也计为失败。后续脚本 pattern 需要区分“可解释 NO_MEM”和崩溃。

## 下一步

- 阶段 6 不能完全归档；待 Wi-Fi 可用后补测公网 HTTPS 成功路径和更完整的 ESP-DL running -> foreground yield 场景。
- 后续若再次开启真实 BLE toggle 测试，必须确认测试任务在 internal stack 上运行。
- 正常固件必须保持 `CONFIG_RUNTIME_RESOURCE_GATE_BOARD_TEST` 关闭。
