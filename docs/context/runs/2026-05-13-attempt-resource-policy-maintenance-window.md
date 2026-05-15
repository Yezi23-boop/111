---
id: attempt-2026-05-13-resource-policy-maintenance-window
tags: watch, resource-management, power-policy, maintenance, safety-monitor
summary: resource-policy-maintenance-window；结果：partial。
last_reviewed: 2026-05-13
memory_type: episodic
scope: task
status: active
result: partial
owners: main/services/power_policy.c, main/services/power_policy.h, tests/test_power_integration_source.py, docs/context/plans/completed/2026-05-12-watch-resource-framework-plan.md
triggers: MAINTENANCE maintenance_window power_policy Safety Monitor
evidence_level: verified
record_reasons: owner-architecture, framework-constraint, evidence
force_reason:
---

# Attempt Log: resource-policy-maintenance-window

## 背景

- 本次要验证什么：按手表整体资源框架补一个高压维护窗口的薄请求入口，让后续 OTA、模型替换、模型验证和日志导出可以先经过 `power_policy` 预算，而不是各自直接与 Safety Monitor 并发抢资源。
- 对应任务或计划：watch-resource-framework-plan-20260512
- 结果状态：partial
- 长期记录理由：owner-architecture, framework-constraint, evidence

## 环境

- 分支/工作区状态：未记录
- 设备/串口/板型：COM3，ESP32-S3 手表板
- 关键前置条件：`export.ps1` 可用，资源框架 Phase 1 已落地

## 操作

- 修改过的文件或 owner：
- `main/services/power_policy.[ch]`
- `tests/test_power_integration_source.py`
- `docs/context/plans/completed/2026-05-12-watch-resource-framework-plan.md`
- 执行的命令或动作：
- 新增 `power_policy_set_maintenance_window(bool active, const char *reason)`。
- 维护窗口只记录策略请求和日志 `maintenance_window_enter/exit`，不直接启动 OTA、模型验证、日志导出，也不直接调用 Safety Monitor runtime。
- `power_policy_get_budget()` 在维护窗口下发布 `POWER_POLICY_STATE_MAINTENANCE`，并设置 `danger_detection_allowed=false`，让 `background_service_manager` 通过既有预算路径暂停 Safety Monitor。
- 低电量预警优先于维护窗口，避免在低电量保护预算下进入高压维护。

## 观测

- 关键日志/证据：
- `uv run python -m pytest tests/test_power_integration_source.py tests/test_audio_codec_port_source.py tests/test_safety_monitor_session_source.py tests/test_danger_detection_controller_source.py`：18 passed。
- `git diff --check` 无 whitespace 错误；仅出现本仓库已有 LF/CRLF 提示。
- `uv run python scripts/context/validate_context.py --level standard --q "危险识别 资源框架 power_policy maintenance_window audio_codec session snapshot" --brief`：错误 0，警告 0。
- `idf.py build` 通过，`111.bin` 大小 `0x8d5270`，factory 分区剩余 `0x12ad90`（12%）。
- `idf.py -p COM3 flash` 通过。
- 冷启动日志 `board_logs/2026-05-13-resource-framework-coldboot-2026-05-13-134359.log`：可见 `background_gate_wait: ui_first_frame_ready`、`boot_stage: ui_first_frame_ready`、`background_gate_ready: ui_first_frame_ready`；40s 日志未检出自动 `background danger detection started`、`INFERENCE #`、`Model::test`、`Display flush failed`、`ESP_ERR_NO_MEM`、panic、Guru 或 `LoadProhibited`。
- 与预期不一致的点：
- 未记录。

## 结论

- 本次可以确认的事实：源码层已经具备 `MAINTENANCE` 策略请求入口；`power_policy` 仍只发布预算，不拥有具体维护任务或危险识别 runtime；本轮烧录后的冷启动未发现启动阶段资源回归。
- 仍然不能确认的事实：
- 尚未接入真实 OTA、模型验证或日志导出任务，因此还没有板端维护窗口 enter/exit 场景日志。

## 未验证风险

- 下一轮仍需补证据的边界：
- 当后续某个维护任务接入该接口时，需要确认 `maintenance_window_enter` 后 Safety Monitor 停止，维护任务结束后 `maintenance_window_exit` 触发 Safety Monitor 按用户开关和资源预算恢复。
