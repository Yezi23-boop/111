---
id: 2026-07-05-attempt-danger-alert-initial-haptic
tags: context, runs, attempt-log, danger-detection, haptic, ds2413, app-alert-manager, freertos
summary: 为危险 Alerting 补首次强震 owner，使用短生命周期 FreeRTOS task 异步驱动 DS2413 马达。
last_reviewed: 2026-07-05
status: completed
record_because: owner-architecture, freertos, hardware, evidence
changed: main/features/alerts/haptic_alert_player.c, main/features/alerts/app_alert_manager.c, main/app/board_ds2413_motor.c, docs/context/knowledge/project/hearing-assist-danger-alert-firmware-mapping.md
evidence_level: source-test
---

# Attempt Log: Danger Alert Initial Haptic

## 目标

- 为 hearing-assist 危险提醒补齐首次危险强震。
- 保持 owner 边界：`board_ds2413_motor` 只做硬件开关，`haptic_alert_player` 做震动模式和异步执行，`app_alert_manager` 做 P0 提醒编排。
- 不修改 `board_ds2413_motor_pulse()` 的同步语义，不在危险识别回调里阻塞等待马达震完。

## 实现

- 新增 `haptic_alert_player`：
  - `haptic_alert_player_init()`
  - `haptic_alert_player_play_initial_danger_once()`
  - 短生命周期 FreeRTOS task 执行 `220ms on -> 90ms off -> 220ms on`。
  - `playing` + critical section 去重，任务退出前兜底 `board_ds2413_motor_set_enabled(false)`。
- `app_alert_manager`：
  - init 阶段初始化 haptic player。
  - 新 danger raise 时触发 haptic；同源 active 重复告警不重复震动。
  - haptic 失败只 warning，不阻断 overlay/audio/云端告警/recorder。
- `power_policy`：
  - haptic player 只读取 `power_policy_get_budget().haptic_alert_allowed`，不写 policy，不直接控制预算。

## 验证

- `uv run python -m pytest tests/test_haptic_alert_player_source.py tests/test_power_integration_source.py tests/test_danger_detection_service_source.py tests/test_board_ds2413_motor_source.py`：36 passed。
- 首次 `idf.py build` 在增量重配阶段暴露既有 CMake requirements 解析抖动；未改 `components/espdl_inference/CMakeLists.txt`，执行 `idf.py reconfigure` 后配置通过。
- `. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`：通过，`111.bin` `0xac1380`，最小 app 分区剩余 `0x33ec80`/23%。
- `uv run python scripts/context/validate_context.py --level standard --q "danger alert haptic ds2413 app_alert_manager" --brief`：错误 0，警告 0。

## 后续

- 真机验收首次 Alerting 是否产生两段强震。
- 后续再单独做持续提醒 `realert_rule`、用户通知模式和事件记录。
