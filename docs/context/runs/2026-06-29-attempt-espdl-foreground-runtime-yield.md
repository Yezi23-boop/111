---
id: attempt-espdl-foreground-runtime-yield
tags: context, runs, attempt-log, espdl, safety-monitor, foreground-runtime-gate, resource-arbitration, freertos, ram
summary: Watch Runtime Resource Gate 阶段 2：Safety Monitor 在强前台 owner active 时让路，不启动或恢复 ESP-DL runtime。
last_reviewed: 2026-06-29
memory_type: episodic
scope: task
result: partial
owners: main/services/background_service_manager.c, main/services/background_service_manager.h, main/ui/custom/danger_detection_controller.c, tests/test_safety_monitor_session_source.py
triggers: ESP-DL yield, Safety Monitor foreground runtime block, Hermes foreground, BLE foreground, foreground_runtime_gate
evidence_level: design
record_reasons: route-choice, evidence
---

# Attempt Log: ESP-DL Foreground Runtime Yield

## 背景

- 本次要验证什么：在 `foreground_runtime_gate` 已存在的前提下，让 Safety Monitor / ESP-DL 作为可抢占增强任务给 Hermes、BLE 配网、OTA、未来前台重交互页面让路。
- 对应计划：`docs/context/plans/active/2026-06-29-watch-runtime-resource-gate-plan.md` 阶段 2。
- 关键边界：不让 gate 直接 suspend/delete ESP-DL task，也不让 `components/espdl_inference` 反向依赖 `main/services`。

## 操作

- `background_service_manager` 新增 `BACKGROUND_SERVICE_MANAGER_DANGER_BLOCK_FOREGROUND_RUNTIME` 阻塞原因。
- `background_service_manager_apply_policy()` 合成 Safety Monitor 目标态时读取：
  - `foreground_runtime_gate_is_active()`
  - `foreground_runtime_gate_current_owner()`
  - `foreground_runtime_gate_owner_text()`
- 强前台 active 时，`danger_should_run=false`，阻塞原因为 `foreground_runtime`，从而不启动或不恢复 ESP-DL runtime。
- `danger_detection_controller` 对该阻塞原因显示为“前台任务中，暂时等待”。
- Source tests 增加边界检查：manager include gate、记录 foreground runtime 资源阻塞日志、不得出现 `vTaskSuspend` / `vTaskDelete`。

## 当前边界

- 如果 ESP-DL 已经在跑，强前台进入后需要下一轮 manager policy apply 才会调用 `safety_monitor_session_apply(false, ...)` 走正常 stop；本阶段不做异步硬抢占。
- 还没有接入 Hermes acquire/release，因此本阶段只是让路机制就位，真实触发要等阶段 3。

## 验证

```powershell
uv run python -m unittest tests.test_safety_monitor_session_source tests.test_danger_detection_controller_source tests.test_foreground_runtime_gate_source
```

结果：`Ran 10 tests ... OK`。

## 下一步

- 阶段 3：Hermes 前台 WebSocket/录音开始时 acquire `FOREGROUND_RUNTIME_OWNER_HERMES`，离开页面或终态时 release。
