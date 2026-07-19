---
id: attempt-fall-candidate-post-check
tags: context, runs, attempt-log, imu, fall-detection, candidate, post-check, freertos, psram
summary: 将 Fall 2s CNN 从报警决策者改为候选生成器，新增 6s 长事件窗口和 post-check 严格确认。
last_reviewed: 2026-07-19
status: completed
owners: main/services/sensors/imu_service.c, main/services/sensors/imu_service.h, main/services/fall_detection_service.c, components/fall_detection_inference/include/fall_model_runner.h, docs/context/plans/active/FALL_DETECTION_V1_111_DEPLOYMENT_DESIGN.md
record_because: route-choice, evidence, owner-architecture
---

# Attempt Log: Fall Candidate + Post-check

## 背景

- 用户选择严格确认路线：`Event Trigger -> 2s CNN candidate -> post-check -> CONFIRMED`。
- 旧实现中 `fall_prob >= FALL_MODEL_THRESHOLD_DEFAULT` 会直接进入 `CONFIRMED`，模型仍是报警决策者。
- 当前 2s CNN 窗口不足以计算事件后 2~5s 低运动统计，因此需要把 `imu_service` 发布窗口扩为 post-check 可用的长窗口，同时保持模型输入仍为 `[1,600]`。

## 本轮改动

- `imu_service` 事件窗口改为 50Hz / 6s / 300 帧：事件前 50 帧、事件后 250 帧，`trigger_frame_index=50`。
- `fall_detection_fill_model_input()` 改为从长窗口中提取 `[trigger-35, trigger+64]` 的 100 帧子窗口，模型输入仍为 2s / 6ch / `[1,600]`，gyro 继续从 `deg/s` 转 `rad/s`。
- `fall_detection_service` 新增 post-check：
  - `pre_gravity`: 窗口帧 `[0..49]`
  - `post_gravity`: 窗口帧 `[150..199]`
  - `low_motion`: 窗口帧 `[150..299]`
  - 普通确认：`fall_prob>=0.60 && low_motion && posture_change`
  - 强置信兜底：`fall_prob>=0.90 && low_motion`
- `fall_prob>=0.60` 现在只生成 `candidate` 日志；post-check 失败不红屏、不上传。
- 进一步优化：新增 `fall_detection_validate_window_contract()`，在推理前显式校验基础窗口契约、模型子窗口边界和 post-check 区间边界；异常时打印 `错误=0x%02x`，避免后续改常量时静默越界或误用数据。
- 进一步优化：`post检查表` 增加触发帧 `trigger_acc/trigger_gyro/trigger_jerk`，板端可直接把候选结果、post-check 结果和触发强度放在同一行排查。

## 验证

- `uv run python -m unittest tests.test_fall_detection_inference_source tests.test_imu_service_source tests.test_fall_detection_service_source`：17 passed。
- `git diff --check -- main/services/sensors/imu_service.c main/services/sensors/imu_service.h main/services/fall_detection_service.c components/fall_detection_inference/include/fall_model_runner.h tests/test_imu_service_source.py tests/test_fall_detection_service_source.py tests/test_fall_detection_inference_source.py docs/context/INDEX.agent.md docs/context/CHANGELOG.md docs/context/plans/active/FALL_DETECTION_V1_111_DEPLOYMENT_DESIGN.md docs/context/runs/2026-07-19-attempt-fall-candidate-post-check.md`：无 whitespace error，仅 LF/CRLF warning。
- `. D:/esp-idf/v5.5.3/esp-idf/export.ps1; idf.py build`：通过，`111.bin=0xac6e80`，app free `0x339180`/23%。
- 进一步优化后复测：source tests 17 passed；`git diff --check -- main/services/fall_detection_service.c tests/test_fall_detection_service_source.py` 无 whitespace error，仅 LF/CRLF warning；`idf.py build` 通过，`111.bin=0xac6f40`，app free `0x3390c0`/23%；context standard 错误 0、警告 0。
- `uv run python scripts/context/validate_context.py --level standard --q "fall model candidate post-check low motion posture change" --brief`：错误 0，警告 0。

## 后续

- 板端仍需采集静止佩戴、平放、小幅旋转、快速翻腕、拍桌/撞表、快速坐下和模拟跌倒日志。
- 重点验证 ADL 可出现 candidate 但 post-check 失败时不红屏、不上传；模拟跌倒应通过 `low_motion` 与姿态变化确认。
