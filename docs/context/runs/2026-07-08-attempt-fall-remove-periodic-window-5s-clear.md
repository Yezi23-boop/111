---
id: run-fall-remove-periodic-window-5s-clear-2026-07-08
tags: context, runs, attempt-log, imu, fall-detection, event-trigger, alert-clear, 5s-window, rf5s
summary: 删除 Fall RF5s 定期窗口推理路径，并将本地红屏/告警限制为 5 秒自动退出。
last_reviewed: 2026-07-08
memory_type: run
scope: imu
status: completed
owners: main/services/imu_service.c, main/services/fall_detection_service.c, tests/test_imu_service_source.py, tests/test_fall_detection_service_source.py, docs/context/plans/active/FALL_DETECTION_V1_111_DEPLOYMENT_DESIGN.md
evidence_level: build
---

# Attempt Log: Fall Remove Periodic Window + 5s Clear

## 背景

- 用户反馈手表只是背面朝上也触发跌倒，板端日志显示 `flags=0x00` 定期窗口给出高 `fall_prob` 并直接确认 FALL。
- 该路径绕过 V1 Event Trigger 设计；用户随后要求删除定期窗口，并确认本地告警/红屏只需要保持 5 秒。

## 改动

- `imu_service` 删除定期窗口构造与发布路径；没有活动 Event Trigger 时不再向 fall detection queue 发布窗口。
- `fall_detection_service` 拒绝 `trigger_flags == 0` 的非事件窗口，并要求事件窗口 `trigger_frame_index == IMU_SERVICE_EVENT_PRE_FRAMES`。
- 跌倒确认后记录 `last_alert_time_us`，`fall_detect` task 每 1 秒 queue timeout 检查自动 clear；每次推理后也检查一次，避免事件窗口持续到来时红屏超过 5 秒。
- 后续低风险事件窗口仍可提前 clear；App danger alert 上传和本机危险语音跳过策略保持不变。

## 验证

- `uv run python -m unittest tests.test_fall_detection_service_source tests.test_imu_service_source tests.test_fall_detection_inference_source`：16 tests passed。
- `git diff --check -- main/services/imu_service.c main/services/fall_detection_service.c tests/test_imu_service_source.py tests/test_fall_detection_service_source.py docs/context/plans/active/FALL_DETECTION_V1_111_DEPLOYMENT_DESIGN.md docs/context/CHANGELOG.md`：无 whitespace error，仅 LF/CRLF warning。
- `. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`：通过；`111.bin` size `0xad3d30`，最小 app 分区剩余 `0x32c2d0` / 23%。
- `uv run python scripts/context/validate_context.py --level standard --q "fall detection remove periodic window 5s auto clear" --brief`：索引 202 个文件，错误 0，警告 0。
- `scripts\board\agent_serial_monitor.ps1 -Port COM7 -Action app-flash-monitor -DurationSeconds 60 -Tag fall-no-periodic-5s-clear -QuietConsole`：采集完成，`panic_log_seen=0`，日志 `board_logs/2026-07-08-17-16-14-fall-no-periodic-5s-clear.log`。
- 板端日志显示模型 `tcn_v1_rf5s_6ch_5s` 加载为 `shape=[1, 1500]`、`threshold=0.85`，`dl::Model: Test Pass!`。
- 限定 fall/imu 日志统计：`定期窗口表=0`，`imu_service: .*flags=0x00 = 0`，`fall_detection: .*flags=0x00 = 0`；事件窗口和推理窗口均为非零 flags。
- 板端日志中多次确认后约 5 秒 clear：例如确认约 `15088ms` 后在 `20128ms` clear，确认约 `25598ms` 后在 `30648ms` clear，确认约 `47548ms` 后在 `52608ms` clear。

## 下一步

- 板端补采静止佩戴、背面朝上、快速翻腕和模拟跌倒日志，确认不再出现 `定期窗口表`，且本地红屏 5 秒后自动 clear。
- 继续实现 V1 post-check：低运动 + 姿态变化。
