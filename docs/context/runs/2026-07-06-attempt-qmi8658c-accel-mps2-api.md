---
id: attempt-2026-07-06-qmi8658c-accel-mps2-api
tags: context, runs, attempt-log, qmi8658c, imu, fall-detection, driver, units
summary: 按用户决策把 QMI8658C driver 扩展为可直接输出三轴加速度 m/s^2，后续 fall_detection 不再自行从 raw LSB 换算物理单位。
created: 2026-07-06
last_reviewed: 2026-07-06
status: completed
evidence_level: source-test
owners: components/qmi8658c, main/services/imu_service.c, docs/context/plans/active/2026-06-05-imu-runtime-framework-plan.md
record_because: route-choice, evidence
---

# Attempt Log: QMI8658C accel m/s^2 API

## 背景

在摔倒检测 `.espdl` 接入计划中，需要给模型输入 50Hz / 4s / 600 点三轴加速度窗口。模型训练数据口径为物理加速度，固件侧需要避免把 QMI8658C raw LSB、`1g=1024` 的 `imu_motion_sample_t` 或 mg norm 混给模型。

用户明确决策：修改为 **QMI8658C driver 统一输出 `m/s^2`，外部只需要拿就行**。

## 本次改动

- `components/qmi8658c/include/qmi8658c.h`
  - 新增 `qmi8658c_accel_mps2_t`。
  - 新增 `qmi8658c_read_accel_mps2()`。
- `components/qmi8658c/qmi8658c.c`
  - 记录最近一次配置的 `accel_fs`。
  - 按 QMI8658C 常见量程表换算：`0=16384`、`1=8192`、`2=4096`、`3=2048 LSB/g`。
  - 使用 `9.80665f` 把 raw LSB 转成 `m/s^2`。
  - `qmi8658c_configure()` 和 `qmi8658c_configure_wake_on_motion()` 会拒绝没有 m/s^2 换算表的 `accel_fs` 编码。
- `tests/test_qmi8658c_source.py`
  - 锁定 m/s^2 API、重力常量、量程表和配置校验。
- fall detection active plan
  - 将 raw-to-physical owner 从 `board_imu/imu_service` 改为 QMI8658C driver。

## 保留边界

- `qmi8658c_read_raw()` 暂时保留，供现有抬腕日志、raw gyro、姿态稳定性和排障使用。
- QMI8658C driver 不处理板级安装方向；轴向映射仍属于 `board_imu` / `imu_service`。
- 现有 `imu_motion_sample_t` 仍是 `1g=1024` 的整数口径，不等于 `m/s^2`。

## 验证

- `uv run python -m unittest tests.test_qmi8658c_source tests.test_imu_service_source tests.test_imu_motion_source`
  - `21 passed`
- `git diff --check`
  - 仅 CRLF warning，无 whitespace error。
- `. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`
  - build 通过，`111.bin` `0xac3230`，最小 app 分区剩余 `0x33cdd0` / 23%。
- 串口探测只看到 `COM1`，未看到常用板端 `COM3/COM7`，本轮未执行 `app-flash`。

## 后续

- fall window 实现时优先调用 `qmi8658c_read_accel_mps2()` 或在 `imu_service` 内复用同一 driver 物理量口径。
- 若后续要删除 `board_imu_config_t.accel_lsb_per_g`，必须先迁移现有 `imu_service_accel_norm_mg()` 和 `imu_service_normalize_accel()`，不要在本次摔倒检测接入里顺手大改。
