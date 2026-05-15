---
id: 2026-05-15-attempt-power-policy-ui-activity-budget
tags: [run, watch, resource-management, power-policy, ui-refresh-policy, idle-dim]
summary: power_policy 只读消费 ui_refresh_policy activity snapshot，将普通运行态预算细分为 ACTIVE / IDLE_DIM。
result: success
status: active
last_reviewed: 2026-05-15
memory_type: episodic
scope: repo
owners: main/services/power_policy.*, tests/test_power_integration_source.py
triggers: power_policy, ui_refresh_policy, activity_snapshot, idle_dim, watch_resource_framework
evidence_level: verified
---

# Attempt Log: power-policy-ui-activity-budget

## Goal

执行手表资源框架下一步：让 `power_policy` 只读消费 `ui_refresh_policy` activity snapshot，在普通运行态发布更准确的 `ACTIVE / IDLE_DIM` 预算；本轮不改变 UI 刷新链路、不控制亮度、不进入 `STANDBY / sleep`。

## Changes

- `main/services/power_policy.c` 引入 `ui_refresh_policy_get_activity_snapshot(...)`：
  - snapshot 未初始化或读取失败时，维持普通 `ACTIVE` fallback。
  - `UI_REFRESH_POLICY_ACTIVITY_IDLE_DIM` 下将普通预算细分为 `POWER_POLICY_STATE_IDLE_DIM`。
  - `IDLE_DIM` 只关闭 `ui_high_refresh_allowed`，不关闭 `danger_detection_allowed`。
- `CHARGING / LOW_BATTERY_WARN / MAINTENANCE` 继续作为更高优先级预算状态：
  - `CHARGING` 不被 UI idle 改写成 `IDLE_DIM`。
  - `LOW_BATTERY_WARN` 与 `MAINTENANCE` 继续覆盖普通 UI activity。
- `policy_state_change` 日志补充 `ui_high_refresh` 字段。
- `tests/test_power_integration_source.py` 增加 source test，锁定 `power_policy` 不反向调用 UI poll、亮度、LVGL 或面板接口。

## Evidence

- `uv run python -m pytest tests/test_power_integration_source.py tests/test_ui_refresh_policy_source.py tests/test_safety_monitor_session_source.py`：通过。
- `uv run python scripts/context/validate_context.py --level standard --q "power_policy ui_refresh_policy activity snapshot ACTIVE IDLE_DIM" --brief`：通过。
- `. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`：通过。

## Notes

- 这是资源框架的小 gate，不是低功耗 sleep 接入。
- `ui_refresh_policy` 仍是亮度、dim 和 LVGL 延时策略 owner；`power_policy` 只读它发布的 UI 活跃度事实。
- 下一步应补低电量预警与 UI activity 并发场景日志，确认 Safety Monitor 不因预算变化反复启停。
