---
id: attempt-runtime-resource-gate-hermes-https-ble
tags: context, runs, attempt-log, runtime-resource-gate, hermes, background-https-gate, ble, espdl, freertos, ram
summary: Watch Runtime Resource Gate 阶段 3-5：Hermes 前台接入强前台 gate，后台 HTTPS 串行错峰，Bluetooth 显式点击增加 quiet-window 单次重试。
last_reviewed: 2026-06-29
memory_type: episodic
scope: task
status: active
result: partial
owners: main/services/memory_watch_service.c, main/services/background_https_gate.c, main/services/background_https_gate.h, main/services/memory_watch_voice_client.c, main/features/weather/hptts.c, main/ui/custom/main_dropdown_controller.c
triggers: Hermes foreground owner, background HTTPS gate, BLE quiet retry, Runtime Resource Gate, ESP-DL yield, Safety Monitor
evidence_level: design
record_reasons: route-choice, evidence
---

# Attempt Log: Runtime Resource Gate Hermes / HTTPS / BLE

## 背景

- 本次承接 `docs/context/plans/active/2026-06-29-watch-runtime-resource-gate-plan.md` 阶段 3-5。
- 阶段 1 已提供 `foreground_runtime_gate` 状态事实；阶段 2 已让 Safety Monitor / ESP-DL 在强前台 active 时让路。
- 本次目标是把真实前台 Hermes、低优先级后台 HTTPS、主界面 Bluetooth 显式启动峰值接入同一资源口径。

## 操作

- Hermes 前台接入：
  - `memory_watch_service_set_foreground_active(true)` acquire `FOREGROUND_RUNTIME_OWNER_HERMES`。
  - `memory_watch_service_set_foreground_active(false)` release `FOREGROUND_RUNTIME_OWNER_HERMES`。
  - acquire/release 后通知 `background_service_manager_notify_foreground_runtime_changed()`，让 Safety Monitor / ESP-DL 及时重算目标态。
  - 录音麦克风占用仍由 `memory_watch_recorder.c` 通过 foreground audio 声明，不在 service 外层重复声明。

- 后台 HTTPS gate 接入：
  - 新增 `main/services/background_https_gate.[ch]`，使用静态 binary semaphore + quiet window。
  - 覆盖 Memory Watch 后台 health、`/sync`、inbox poll、inbox mark-read，以及天气 HTTPS。
  - 不覆盖 Hermes 前台 WebSocket、用户主动语音上传、文本命令和 cancel。
  - gate 忙或 quiet window 内，后台 owner 按现有 retry/稍后调度路径延后，不阻塞 UI。

- Bluetooth quiet-window retry：
  - 主界面 Bluetooth 显式点击启动 BLE 前 acquire `FOREGROUND_RUNTIME_OWNER_BLE_PROVISIONING`。
  - 同时打开 `background_https_gate_quiet_for(...)`，避免新的后台 HTTPS 撞 BLE controller 启动峰值。
  - 如果 `network_manager_set_ble_enabled(true)` 返回 `ESP_ERR_NO_MEM`，短等待后只重试一次；仍失败则 fail closed 并保持失败 toast。
  - 没有修改 `components/network_manager` 反向依赖 `main/services`。

## 边界

- 本轮没有新增集中 job queue 或大 ResourceManager。
- `foreground_runtime_gate` 和 `background_https_gate` 都不直接 suspend/delete 业务 task。
- ESP-DL 让路仍由 `background_service_manager` / Safety Monitor owner 观察 gate 后执行。
- Bluetooth 配网是否关闭 Wi-Fi 属于后续更强互斥策略，本轮只处理普通 Bluetooth 显式点击的启动峰值。

## 验证

Source tests：

```powershell
uv run python -m unittest tests.test_background_https_gate_source tests.test_foreground_runtime_gate_source tests.test_safety_monitor_session_source tests.test_danger_detection_controller_source tests.test_main_screen_ble_toggle_source tests.test_memory_watch_service_source tests.test_memory_watch_recorder_source tests.test_memory_watch_voice_client_source tests.test_memory_watch_ws_client_source tests.test_time_weather_source tests.test_network_manager_source tests.test_ble_presence_source
```

结果：`Ran 75 tests ... OK`。

构建：

```powershell
& 'D:\esp-idf\v5.5.3\esp-idf\export.ps1'; idf.py build
```

结果：通过。`111.bin` 大小 `0xabcc20`，最小 app 分区剩余 `0x3433e0`（23%）。

Diff 检查：

```powershell
git diff --check
```

结果：只有 CRLF warning，无 whitespace error。

## 未验证风险

- 尚未完成阶段 6 真机高压回归：COM3 冷启动、Hermes 前台/离页、Bluetooth 点击、天气/inbox/health 并发、Safety Monitor/ESP-DL 让路需要串口证据。
- 后台 HTTPS gate 会让 weather/inbox/health/sync 在高压窗口延后；如果 UI 文案把一次跳过显示成永久失败，需要另开 UI 状态收敛小闭环。
- BLE 如果长期 internal RAM 最大连续块不足，quiet retry 只能降低撞车概率，不能保证 BLE 一定启动成功。

## 下一步

- 执行阶段 6 真机高压回归。
- 重点看日志关键词：`foreground_runtime`、`background_https_gate`、`memory_watch`、`inbox`、`weather`、`BLE`、`foreground_audio`、`safety_monitor`、`ESP-DL`、`Guru`、`panic`、`stack overflow`、`esp-aes`。
