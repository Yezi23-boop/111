---
id: 2026-05-15-attempt-ui-refresh-policy-activity-snapshot
tags: [run, watch, resource-management, ui, power-policy, ui-refresh-policy]
summary: 为 ui_refresh_policy 增加只读 activity snapshot，先发布 UI 活跃度事实，不让 power_policy 接管 UI 刷新链路。
result: success
status: active
last_reviewed: 2026-05-15
memory_type: episodic
scope: repo
owners: main/ui/ui_refresh_policy.*, tests/test_ui_refresh_policy_source.py
triggers: ui_refresh_policy, activity_snapshot, power_policy, idle_dim, watch_resource_framework
evidence_level: verified
---

# Attempt Log: ui-refresh-policy-activity-snapshot

## Goal

执行手表资源框架 Gate 4：让 `ui_refresh_policy` 先发布只读 activity snapshot，供后续 `power_policy` 评估整机 `ACTIVE / IDLE_DIM / STANDBY` 时读取事实；本轮不改变现有亮度、idle-dim、LVGL 主循环延时和 BLE provisioning throttle 行为。

## Changes

- `main/ui/ui_refresh_policy.h` 新增：
  - `ui_refresh_policy_activity_state_t`
  - `ui_refresh_policy_throttle_mode_t`
  - `ui_refresh_policy_activity_snapshot_t`
  - `ui_refresh_policy_get_activity_snapshot(...)`
- `main/ui/ui_refresh_policy.c` 新增只读 getter：
  - 复制 cached activity state、throttle mode、force-active、idle-dim、亮度目标、最近触摸时间和 idle 时长。
  - 不调用 `ui_refresh_policy_poll()`。
  - 不调用 `co5300_panel_set_brightness_percent()`。
  - 不依赖 `power_policy`。
- `tests/test_ui_refresh_policy_source.py` 增加 source test，锁定 snapshot 是只读事实出口。

## Evidence

- `uv run python -m pytest tests/test_ui_refresh_policy_source.py`：6 passed。
- `uv run python -m pytest tests/test_ui_refresh_policy_source.py tests/test_power_integration_source.py`：12 passed。
- `uv run python scripts/context/validate_context.py --level standard --q "ui_refresh_policy activity snapshot power_policy gate" --brief`：错误 0，警告 0。
- `. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`：通过；`111.bin` 大小 `0x8d5bf0`，factory 剩余 `0x12a410`（12%）。

## Notes

- 这是资源框架的小 gate，不是 UI 刷新链路重构。
- `power_policy` 仍未读取该 snapshot；下一步应先做低风险消费评估或观测，不直接接管屏幕策略。
