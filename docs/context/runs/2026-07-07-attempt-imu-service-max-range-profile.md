---
title: "Attempt Log: IMU service maximum accel and gyro range profile"
id: run-2026-07-07-imu-service-max-range-profile
date: 2026-07-07
tags: context, runs, attempt-log, imu, qmi8658c, accel-range, gyro-range, fall-detection, service-profile
summary: 按用户要求将 imu_service 的 WoM/运动窗口加速度量程改为最大 ±16g，运动窗口陀螺仪量程改为最大 ±2048 dps。
last_reviewed: 2026-07-07
status: completed
evidence: source-test, build, context-validation
owners: main/services/imu_service.c, tests/test_imu_service_source.py
---

# IMU service 最大量程 profile

## 背景

用户要求“全都用最大量程，加速度和角速度都是”。当前 QMI8658C driver 的量程换算表支持：

- 加速度：`0=±2g`、`1=±4g`、`2=±8g`、`3=±16g`。
- 陀螺仪：`0=±16 dps` 到 `7=±2048 dps`。

为后续摔倒/冲击/快速旋转采样保留动态范围，本轮只修改 `imu_service` 的运行 profile，不改 driver public API、board facts、ODR、WoM 阈值或窗口长度。

## 修改

- `main/services/imu_service.c`
  - `wom_accel_fs_code: 0 -> 3`，WoM 阶段使用最大 `±16g`。
  - `motion_accel_fs_code: 0 -> 3`，运动窗口加速度使用最大 `±16g`。
  - `motion_gyro_fs_code: 4 -> 7`，运动窗口角速度使用最大 `±2048 dps`。
  - 保持 `wom_threshold_mg=120`、`motion_sample_count=16`、`motion_sample_period_ms=40`、ODR 编码不变。
- `tests/test_imu_service_source.py`
  - 锁定上述最大量程编码，防止后续无意回退。

## 取舍

- 优点：跌落、撞击、快速翻转不容易因 `±2g` 或较小 gyro 量程饱和，日志和未来模型窗口保留更完整峰值。
- 代价：同样 16-bit raw 下，单位分辨率降低；低幅动作细节会比小量程粗。
- 当前按用户决策优先动态范围，后续如果训练模型明确需要更高低幅分辨率，再按数据分布重新评估量程。

## 验证

- `uv run python -m unittest tests.test_imu_service_source tests.test_qmi8658c_source tests.test_imu_motion_source tests.test_extract_imu_raise_samples_script`
  - 23 passed。
- `& D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`
  - 通过。
  - `111.bin` size `0xac3230`。
  - app 分区剩余 `0x33cdd0` / 23%。
- `& D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py -p COM7 app-flash`
  - 通过。
  - 写入 `0x10000 111.bin`，数据 hash 校验通过。
  - 本轮未启动 IMU service 做串口量程实测，因为默认 `app_main.c` 中 `imu_service_start()` 仍处于临时关闭状态。
- context standard 校验：
  - `uv run python scripts/context/validate_context.py --level standard --q "imu service maximum accel gyro range qmi8658c" --brief`
  - 错误 0，警告 0。
