---
title: "Attempt Log: QMI8658C concise public API"
id: run-2026-07-07-qmi8658c-concise-public-api
date: 2026-07-07
tags: context, runs, attempt-log, qmi8658c, imu, driver, api, naming, physical-units
summary: 将 QMI8658C public header 从带单位字段名和分散 read helper 收口为简约物理六轴采样 API。
last_reviewed: 2026-07-07
status: completed
evidence: source-test, build, context-validation
owners: components/qmi8658c, main/services/imu_service.c, main/services/imu_service.h
---

# Attempt Log: QMI8658C concise public API

## 背景

上一轮已经把 QMI8658C driver 对外采样契约从 raw register 收口为物理量，但 public header 仍保留较长命名：`qmi8658c_accel_mps2_t`、`qmi8658c_gyro_dps_t`、`qmi8658c_read_accel_mps2()`、`qmi8658c_read_gyro_dps()` 和 `qmi8658c_read_sample()`。用户确认第一版采用“单位由类型注释表达，字段只用 `x/y/z`”，且不保留旧 API 兼容层。

## 决策

- public API 只保留完整物理六轴读取：`qmi8658c_read(qmi8658c_sample_t *sample)`。
- `qmi8658c_accel_t` 表示 `m/s^2`，`qmi8658c_gyro_t` 表示 `deg/s`；字段统一为 `x/y/z`。
- `qmi8658c_bus_t.addr` 表示 7-bit I2C 地址；板级 `board_imu` 仍可保留 `qmi_i2c_addr_7bit` 作为 board fact 命名。
- `STATUS1` / `STATUSINT` 对外改用语义名 `qmi8658c_wom_status_t` / `qmi8658c_int_status_t`。
- raw sample、raw decode、raw-to-physical conversion helper 继续只留在 `qmi8658c.c` 内部，不暴露到 header。

## 修改摘要

- `components/qmi8658c/include/qmi8658c.h`
  - `qmi8658c_identity_t` -> `qmi8658c_info_t`。
  - `qmi8658c_bus_config_t` -> `qmi8658c_bus_t`，字段 `addr`。
  - `qmi8658c_accel_mps2_t` -> `qmi8658c_accel_t`，字段 `x/y/z`。
  - `qmi8658c_gyro_dps_t` -> `qmi8658c_gyro_t`，字段 `x/y/z`。
  - `qmi8658c_read_sample()` -> `qmi8658c_read()`。
  - 删除公开单独 accel / gyro read API。
- `components/qmi8658c/qmi8658c.c`
  - 对齐新 public 函数名：`qmi8658c_init_bus()`、`qmi8658c_config()`、`qmi8658c_read()`、`qmi8658c_read_wom()`、`qmi8658c_read_int()`、`qmi8658c_enable_wom()`、`qmi8658c_disable_wom()`。
  - 删除未公开的外部 `qmi8658c_read_accel()` / `qmi8658c_read_gyro()` 残留，只保留 static conversion helper。
- `main/services/imu_service.*`
  - 改用 `sample.accel.x` / `sample.gyro.x` 等新字段。
  - WoM 触发帧通过 `qmi8658c_read()` 获取完整六轴样本，再只缓存 `last_wom_accel`。
  - snapshot 状态改为 `last_wom_status`，避免继续暴露寄存器名式字段。
- source tests
  - 锁定 header 不再出现 `_mps2` / `_dps` 字段名和单独 accel / gyro read API。

## 验证

- `uv run python -m unittest tests.test_qmi8658c_source tests.test_imu_service_source tests.test_imu_motion_source tests.test_extract_imu_raise_samples_script`
  - 23 passed。
- `git diff --check -- . ':!managed_components'`
  - 通过；仅有工作区 LF/CRLF warning。
- `& D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`
  - 通过。
  - `111.bin` size `0xac3230`。
  - 最小 app 分区剩余 `0x33cdd0` / 23%。
- `uv run python scripts/context/validate_context.py --level standard --q "qmi8658c concise naming physical sample" --brief`
  - 通过；错误 0，警告 0。

## 后续注意

- 这是 breaking rename；当前仓库内已同步迁移，不提供旧 API 兼容 wrapper。
- 后续 fall window 或板级封装应使用 `qmi8658c_sample_t` 的 `accel/gyro` 字段，并在 board/service 层处理轴向映射，不回到 raw 或旧 `_mps2/_dps` 字段名。
