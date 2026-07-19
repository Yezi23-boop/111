---
id: attempt-2026-07-19-fall-jerk-trigger-tighten
tags: context, runs, attempt-log, imu, fall-detection, event-trigger, jerk, threshold, source-tests
summary: 收紧 Fall 2s CNN 事件触发入口，禁止 jerk 单独触发事件窗口，并同步当前阈值 source tests。
last_reviewed: 2026-07-19
memory_type: episodic
scope: task
status: active
result: success
owners: main/services/sensors/imu_service.c, components/fall_detection_inference/include/fall_model_runner.h, docs/context/plans/active/FALL_DETECTION_V1_111_DEPLOYMENT_DESIGN.md
triggers: fall small wrist rotation false trigger, jerk trigger, FALL_MODEL_THRESHOLD_DEFAULT 0.60
evidence_level: build
record_reasons: evidence, plan-decision
force_reason:
---

# Attempt Log: Fall Jerk Trigger Tighten

## 背景

- 用户反馈小幅手腕旋转仍容易触发 Fall 事件链路。
- 当前固件已是 2s/6ch CNN 模型，`FALL_MODEL_THRESHOLD_DEFAULT=0.60f`，事件阈值已被调高到 `25/5/10/5`。
- 本轮目标是先修复明显误触发入口和测试漂移，不实现完整 post-check。

## 操作

- `imu_service_event_flags()` 保留 `ACC_HIGH` 和 `GYRO_JERK` 触发路径。
- `JERK_HIGH` 不再只靠 `jerk_mps2_per_frame > 10.0f` 单独触发；现在还必须满足 `acc_norm_mps2 > 16.0f` 或 `gyro_norm_radps > 3.0f`。
- 同步 `tests/test_imu_service_source.py` 的事件阈值断言。
- 同步 `tests/test_fall_detection_inference_source.py` 的模型阈值断言为 `0.60f`。

## 观测

- `uv run python -m unittest tests.test_fall_detection_inference_source tests.test_imu_service_source tests.test_fall_detection_service_source`：17 tests passed。
- `git diff --check -- main/services/sensors/imu_service.c tests/test_imu_service_source.py tests/test_fall_detection_inference_source.py`：无 whitespace error，仅 LF/CRLF warning。
- `idf.py build`：通过，`111.bin=0xac6640`，app free `0x3399c0`/23%。

## 结论

- 可以确认：当前固件不会再因为单帧 jerk 超阈值就发布 Fall 事件窗口。
- 可以确认：source tests 已恢复到当前实际阈值契约，避免继续断言旧 `0.30` / `15,2.5,3.5`。
- 不能确认：板端小幅旋转误触发是否完全消失；仍需串口按 `flags` 分类采样。

## 下一步

- 接板后采集静止佩戴、小幅旋转、敲击/拍桌、快速翻腕和模拟跌倒日志，重点看 `事件触发表 flags`、`acc_norm`、`gyro_norm`、`jerk` 和 `fall_prob`。
- 继续实现 post-check：模型只产生 candidate，必须通过事件后低运动和姿态变化后才确认告警。
