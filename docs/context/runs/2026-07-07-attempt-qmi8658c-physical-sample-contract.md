---
title: "Attempt Log: QMI8658C physical sample contract"
id: run-2026-07-07-qmi8658c-physical-sample-contract
date: 2026-07-07
tags: context, runs, attempt-log, qmi8658c, imu, board_imu, imu_service, units, framework
summary: 将 QMI8658C 对外采样契约收口为物理量，迁移 board_imu/imu_service 到 physical_6axis、accel_mg、gyro_mdps。
last_reviewed: 2026-07-07
status: completed
evidence: source-test
owners: components/qmi8658c, main/app/board_imu.c, main/services/imu_service.c, scripts/extract_imu_raise_samples.py
---

# Attempt Log: QMI8658C physical sample contract

## 背景

Framework review 明确提出：IMU 对上层只需要输出物理量，没必要再输出 raw 或其它寄存器单位。前一轮已经补齐加速度 `m/s^2` 与陀螺仪 `deg/s` 换算，但 `board_imu` 和 `imu_service` 仍在使用 `qmi8658c_raw_sample_t`、`accel_lsb_per_g` 与 raw 日志字段。

## 决策

- `components/qmi8658c` 内部继续保留 raw register decode，用于一次 I2C 窗口读取与量程换算。
- `qmi8658c.h` 不再暴露 raw sample 或 raw-to-gyro conversion API；对外采样契约为：
  - `qmi8658c_accel_mps2_t`
  - `qmi8658c_gyro_dps_t`
  - `qmi8658c_sample_t`
  - `qmi8658c_read_sample()`
- `board_imu` 不再保存 `accel_lsb_per_g`；表盘法向阈值从 raw LSB 改为 `face_axis_threshold_mg=-397`。
- `imu_service` 不再调用 `qmi8658c_read_raw()`，snapshot 不再输出 raw sample；WoM 触发帧记录物理加速度，运动窗口与终点姿态读取物理六轴。
- 日志从 `source=raw_motion` / `mode=raw_6axis` 改为 `source=physical_6axis` / `mode=physical_6axis`，字段使用 `accel_mg` 与 `gyro_mdps`，避免浮点 printf 与 raw LSB 误读。
- 用户复查指出 `dps` / `radps` 两套角速度结构体重复；本轮按“只输出业务需要的物理量”继续收口，删除公开 `qmi8658c_gyro_radps_t` 与 `qmi8658c_read_gyro_radps()`，第一版只保留 `deg/s`。

## 修改摘要

- `components/qmi8658c/include/qmi8658c.h`
  - 移除公开 `qmi8658c_raw_sample_t`、`qmi8658c_read_raw()`、`qmi8658c_convert_raw_gyro_*()`、`qmi8658c_gyro_radps_t` 和 `qmi8658c_read_gyro_radps()`。
  - 新增公开 `qmi8658c_sample_t` 与 `qmi8658c_read_sample()`。
- `components/qmi8658c/qmi8658c.c`
  - 将 `qmi8658c_read_raw()` 与 raw conversion helper 改为 `static` 内部实现。
  - `qmi8658c_read_sample()` 一次读取完整寄存器窗口，再输出 `m/s^2` 与 `deg/s`。
- `main/app/board_imu.*`
  - 删除 `accel_lsb_per_g`。
  - 将 `face_axis_threshold_raw=-6500` 等价迁移为 `face_axis_threshold_mg=-397`。
- `main/services/imu_service.*`
  - snapshot 改为 `last_wom_accel_mps2` 与 `last_final_sample`。
  - `imu_motion` 归一化从 `m/s^2 -> mg -> 1g=1024` 计算，不再依赖 QMI raw LSB。
  - final pose norm、stability、face axis 均从物理加速度计算。
- `scripts/extract_imu_raise_samples.py`
  - 改解析 `physical_6axis`、`accel_mg`、`gyro_mdps` 日志字段。

## 验证

- `uv run python -m unittest tests.test_qmi8658c_source tests.test_imu_service_source tests.test_imu_motion_source tests.test_extract_imu_raise_samples_script`
  - 23 passed。
- `git diff --check -- . ':!managed_components'`
  - 通过；仅有工作区 LF/CRLF warning。
- `& D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`
  - 通过。
  - `111.bin` size `0xac3230`。
  - 最小 app 分区剩余 `0x33cdd0` / 23%。
- `uv run python scripts/context/validate_context.py --level standard --q "qmi8658c physical sample contract board_imu imu_service physical_6axis raw sample" --brief`
  - 通过；错误 0，警告 0。

## 后续注意

- 历史 context 中提到 `raw_motion`、`raw_gyro` 的内容只代表旧固件证据，不应作为新采样契约继续使用。
- 后续 fall detection 的 50Hz/200 帧窗口应直接使用物理六轴样本，再在模型输入层做 `accX/accY/accZ/gyroX/gyroY/gyroZ` 命名和轴向映射。
