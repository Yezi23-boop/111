---
id: attempt-2026-07-07-qmi8658c-gyro-physical-units
tags: context, runs, attempt-log, qmi8658c, imu, gyro, driver, units
summary: 在 QMI8658C driver 层补齐陀螺仪 raw 到 deg/s 与 rad/s 的物理量换算 API，供后续 board_imu 和 fall window 复用。
created: 2026-07-07
last_reviewed: 2026-07-07
status: completed
evidence_level: source-test
owners: components/qmi8658c, main/app/board_imu.c, main/services/imu_service.c
record_because: route-choice, evidence
---

# Attempt Log: QMI8658C gyro physical units

## 背景

用户要求先补 driver 级 gyro `dps/radps` 换算。上一轮已将加速度 raw-to-physical 收口到 QMI8658C driver；陀螺仪仍只有 raw，后续若 fall window 需要 `gyroX/Y/Z`，不应在 `board_imu` 或 `imu_service` 里重复维护陀螺仪量程表。

## 本次改动

- `components/qmi8658c/include/qmi8658c.h`
  - 新增 `qmi8658c_gyro_dps_t` 与 `qmi8658c_gyro_radps_t`。
  - 新增 `qmi8658c_convert_raw_gyro_dps()` 与 `qmi8658c_convert_raw_gyro_radps()`，用于已读取 raw 六轴样本后的无 I2C 换算。
  - 新增 `qmi8658c_read_gyro_dps()` 与 `qmi8658c_read_gyro_radps()`。
- `components/qmi8658c/qmi8658c.c`
  - 记录最近一次 `gyro_fs` 配置。
  - 按 QMI8658C 陀螺仪量程表换算：`0..7 = ±16/32/64/128/256/512/1024/2048 dps`，对应 `2048/1024/512/256/128/64/32/16 LSB/dps`。
  - 使用 `0.017453292519943295f` 完成 `deg/s -> rad/s`。

## 保留边界

- driver 只表达芯片寄存器坐标系，不做板级安装方向、轴向重映射、滤波或姿态语义。
- `qmi8658c_read_raw()` 继续保留，供现有抬腕日志、raw CSV 和排障使用。
- 当前 `imu_service` 尚未改用 gyro 物理量；后续 board layer 收口时再迁移调用。

## 验证

- `uv run python -m unittest tests.test_qmi8658c_source tests.test_imu_service_source tests.test_imu_motion_source`
  - `22 passed`
- `git diff --check`
  - 仅 CRLF warning，无 whitespace error。
- `. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`
  - build 通过，`111.bin` `0xac3230`，最小 app 分区剩余 `0x33cdd0` / 23%。
- `uv run python scripts/context/validate_context.py --level standard --q "qmi8658c gyro dps radps driver units board imu" --brief`
  - context check `错误: 0，警告: 0`。

## 后续

- `board_imu` 封装时优先使用 `qmi8658c_convert_raw_gyro_dps()` / `qmi8658c_convert_raw_gyro_radps()`，避免同一窗口重复 I2C 读取。
- 若 fall model 第一版只吃 `accX/accY/accZ`，gyro API 作为六轴扩展地基保留，不强行接入模型输入。
