---
id: run-fall-event-trigger-5s-window-2026-07-08
tags: context, runs, attempt-log, imu, fall-detection, event-trigger, 5s-window, psram, esp-dl
summary: Fall Detection V1 第一阶段实现 Event Trigger 与 5 秒事件窗口，不替换旧 3ch ESP-DL 模型资产。
last_reviewed: 2026-07-08
memory_type: run
scope: imu
status: completed
owners: main/services/imu_service.c, main/services/imu_service.h, main/services/fall_detection_service.c, docs/context/plans/active/FALL_DETECTION_V1_111_DEPLOYMENT_DESIGN.md
evidence_level: build
---

# Attempt Log: Fall Event Trigger + 5s Window

## 背景

- 用户要求按 `FALL_DETECTION_V1_111_DEPLOYMENT_DESIGN.md` 先实现 Event Trigger + 5s 事件窗口，不动模型资产。
- 旧固件链路是 `imu_service` 每约 2 秒发布任意 4 秒 / 200 帧加速度窗口，`fall_detection_service` 直接送旧 3ch / `[1,600]` 模型；静止姿态也会被连续送入模型。

## 改动

- `imu_service` 保持 50Hz 采样 owner，新增逐帧 `acc_norm / gyro_norm / jerk_norm` 事件触发。
- 触发阈值使用 active plan 初始值：`A_high=21.57m/s^2`、`G_high=3.84rad/s`、`J_high=5.39m/s^2/frame`，以及 `gyro_norm > 3.84rad/s && jerk > 2.45m/s^2/frame`。
- `imu_service` 只在事件触发并收满 5 秒窗口后发布 `imu_service_accel_window_t`，窗口为 250 帧，包含 75 帧 pre-event 与 175 帧 post/event-side payload。
- 事件窗口 payload 保留 acc `m/s^2` 与 gyro `deg/s`，并记录 trigger flags、trigger frame、trigger time 和触发帧派生特征。
- `fall_detection_service` 暂不替换 `.espdl`，继续用旧 3ch / `[1,600]` 模型；它只消费事件窗口，并按 legacy 200 帧加速度输入兼容运行。

## 验证

- `uv run python -m unittest tests.test_imu_service_source tests.test_fall_detection_service_source`：13 tests passed。
- `git diff --check -- main/services/imu_service.c main/services/imu_service.h main/services/fall_detection_service.c tests/test_imu_service_source.py tests/test_fall_detection_service_source.py`：无 whitespace error，仅 LF/CRLF warning。
- `. "$env:IDF_PATH\export.ps1"; idf.py build`：通过；`111.bin` size `0xae0bd0`，最小 app 分区剩余 `0x31f430` / 22%。

## 下一步

- 接入 V1 6ch 输入构造：生成 `[1,1500]`，gyro 从 `deg/s` 转换为 `rad/s`。
- 替换 V1 6ch `.espdl` 资产和 smoke/test values。
- 接入 post-check：低运动 + 姿态变化。
