---
id: attempt-2026-05-13-safety-monitor-readiness-gate
tags: watch, startup, readiness, safety-monitor, danger-detection
summary: safety-monitor-readiness-gate；结果：success。
last_reviewed: 2026-05-13
memory_type: episodic
scope: task
status: active
result: success
owners: main/services/startup_readiness.[ch], main/services/background_service_manager.c, main/ui/lvgl_task.c, main/app/app_main.c, docs/context/plans/completed/2026-05-12-apple-watch-like-boot-flow-plan.md
triggers: ui_first_frame_ready readiness gate background_service_manager Safety Monitor 安全监听
evidence_level: observed
record_reasons: owner-architecture, framework-constraint, evidence
force_reason:
---

# Attempt Log: safety-monitor-readiness-gate

## 背景

- 本次要验证什么：在用户确认 `安全监听` 上板交互实验 OK 后，按 Apple Watch 启动框架继续把后台重任务门控从固定 5s defer 升级为真实 `ui_first_frame_ready` readiness gate。
- 对应任务或计划：apple-watch-like-boot-flow-plan-20260512
- 结果状态：success
- 长期记录理由：owner-architecture, framework-constraint, evidence

## 操作

- 修改过的文件或 owner：
- `main/services/startup_readiness.[ch]`
- `main/services/background_service_manager.c`
- `main/ui/lvgl_task.c`
- `main/app/app_main.c`
- `main/CMakeLists.txt`
- `tests/main_paths.py`
- `tests/test_power_integration_source.py`
- `tests/test_safety_monitor_session_source.py`
- `docs/context/plans/completed/2026-05-12-apple-watch-like-boot-flow-plan.md`
- 执行的命令或动作：
- 用户确认上一轮危险识别页交互实验没问题。
- 冷启动日志 `board_logs/2026-05-13-safety-monitor-coldboot.log` 里 45s 内未出现自动 `background danger detection started` 或 `INFERENCE #`。
- 新增 `startup_readiness` 静态 EventGroup readiness 标志，由 `lvgl_task` 在 `boot_stage: ui_first_frame_ready` 边界置位。
- `background_service_manager` 删除固定 5s boot defer，改为等待 `startup_readiness_wait_ui_first_frame(portMAX_DELAY)` 后进入策略循环。

## 观测

- 关键日志/证据：
- `uv run python -m pytest tests/test_power_integration_source.py tests/test_safety_monitor_session_source.py tests/test_nonblocking_boot_source.py tests/test_danger_detection_controller_source.py`：17 passed。
- `idf.py build` 通过，`111.bin` 大小 `0x8d4f40`，factory 分区剩余 `0x12b0c0`（12%）。
- `idf.py -p COM3 flash` 通过。
- `board_logs/2026-05-13-startup-readiness-gate-coldboot.log` 中可见 `background_gate_wait: ui_first_frame_ready` 先出现，随后 `boot_stage: ui_first_frame_ready`，最后 `background_gate_ready: ui_first_frame_ready`。
- 同一 35s 冷启动日志未出现自动 `background danger detection started`、`INFERENCE #`、`Display flush failed`、`ESP_ERR_NO_MEM`、panic 或 Guru。
- 与预期不一致的点：
- 未记录。

## 结论

- 本次可以确认的事实：Safety Monitor 后台 manager 已不再依赖固定 5s 延迟；启动门控改为 UI 层真实发布的 `ui_first_frame_ready` readiness 标志，符合前台优先、后台重任务延后的框架方向。
- 仍然不能确认的事实：
- 未记录。

## 未验证风险

- 下一轮仍需补证据的边界：
- 若后续给 `安全监听` 增加 NVS 持久化，需要重新验证“重启后用户曾授权的后台 session 仍然等 UI 首帧 gate 后才启动”。
