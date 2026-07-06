---
title: "Attempt Log: board_imu hardware facts and imu_service profile split"
id: run-2026-07-07-board-imu-hw-profile-split
date: 2026-07-07
tags: context, runs, attempt-log, imu, board_imu, qmi8658c, bsp, service-profile, architecture
summary: 将 board_imu 收窄为板级硬件事实，WoM/窗口/姿态阈值迁到 imu_service 本地 profile，保持 driver/BSP/service 解耦。
last_reviewed: 2026-07-07
status: completed
evidence: source-test, build, context-validation
owners: main/app/board_imu.c, main/app/board_imu.h, main/services/imu_service.c, tests/test_imu_service_source.py
---

# Attempt Log: board_imu hardware facts and imu_service profile split

## 背景

用户质疑 `board_imu` 是否应并入 `qmi8658c driver`。查阅主流嵌入式分层后，结论是：QMI8658C driver 应只负责芯片协议、寄存器、I2C 和物理量输出；板级地址、GPIO、安装方向应由 BSP/board 层持有；WoM 阈值、采样窗口和抬腕判定阈值属于当前服务策略，不应混在 board facts 里。

## 决策

- 不删除 `board_imu`，也不把它放进 `qmi8658c driver`。
- `board_imu_config_t` 只保存当前 PCB 的硬件事实：
  - QMI8658C 7-bit I2C 地址。
  - QMI_INT1 GPIO。
  - IMU 安装方向中的表盘法向轴。
- `imu_service` 新增本地 `imu_service_profile_t`，保存第一版运行策略：
  - WoM 阈值 / blanking / accel FS / ODR。
  - 运动窗口采样帧数 / 间隔 / accel+gyro FS / ODR。
  - 终点姿态 norm、稳定性和表盘法向阈值。
- `qmi8658c driver` 继续只暴露芯片级 `qmi8658c_init_bus()`、`qmi8658c_config()`、`qmi8658c_read()` 等 API，不消费板级 GPIO 或产品阈值。

## 修改摘要

- `main/app/board_imu.h/.c`
  - 删除 WoM、motion window、final pose threshold 等策略字段。
  - 注释改为“当前板的 IMU 布线和安装方向”。
- `main/services/imu_service.c`
  - 新增 `imu_service_profile_t` 与 `k_imu_service_profile`。
  - `imu_service_validate_board_config()` 只校验硬件事实。
  - 新增 `imu_service_validate_profile()` 校验策略窗口与阈值。
  - 所有 `wom_*` / `motion_*` / `final_*` 策略读取改为 profile。
- `tests/test_imu_service_source.py`
  - 锁定 `board_imu` 不再包含策略字段。
  - 锁定 service profile 持有当前默认值。

## 验证

- `uv run python -m unittest tests.test_imu_service_source tests.test_qmi8658c_source tests.test_imu_motion_source tests.test_extract_imu_raise_samples_script`
  - 23 passed。
- `git diff --check -- . ':!managed_components'`
  - 通过；仅有工作区 LF/CRLF warning。
- `& D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`
  - 通过。
  - `111.bin` size `0xac3230`。
  - 最小 app 分区剩余 `0x33cdd0` / 23%。
- `uv run python scripts/context/validate_context.py --level standard --q "board_imu hardware facts imu_service profile qmi8658c driver bsp split" --brief`
  - 通过；错误 0，警告 0。

## 后续注意

- 如果后续做 50Hz / 200 帧 fall window，窗口采样率和窗口长度应进入 fall/imu service profile，不应进入 `qmi8658c driver`。
- 如果后续换板或 IMU 贴装方向变化，才修改 `board_imu`。
