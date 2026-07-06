---
id: attempt-2026-07-07-imu-service-50hz-sampling
tags: context, runs, attempt-log, imu, qmi8658c, imu_sensor, imu_service, 50hz, board-test, com7
summary: 将 IMU 主线推进到默认启动的 50Hz 周期采样，并用当前 COM7 板闭环验证 200 帧窗口可填满。
created: 2026-07-07
last_reviewed: 2026-07-07
status: completed
owners: components/qmi8658c, components/imu_sensor, main/services/imu_service.c, main/app/board_imu.c
---

# Attempt Log: IMU service 50Hz sampling

## 背景

用户确认第一版先做稳定 50Hz 采样，而不是 FIFO Watermark 或 WoM。当前架构保持：

```text
imu_service -> imu_sensor -> qmi8658c
           -> board_imu 只提供板级事实
```

`qmi8658c` 仍只负责芯片寄存器、物理量换算和统一配置；`imu_service` 负责 FreeRTOS task、GPIO21 ISR、50Hz 节拍和 200 帧环形缓冲。

## 本轮变更

- `imu_service` 使用 `vTaskDelayUntil()` 以 20ms 周期调用 `imu_sensor_read()`。
- `imu_service` 内维护 200 帧 / 4 秒环形缓冲，并在 snapshot 发布 `sampling_active`、`sample_count`、`sample_error_count`、`sample_window_ready`、`last_sample_interval_us`。
- `qmi8658c_read_raw()` 按当前已验证读法读取：`STATUS0 -> TIMESTAMP -> TEMP -> AX_L 12 bytes`，避免从 `TEMP_L` 连续 14 字节读导致窗口对齐不稳。
- `qmi8658c_config()` 对齐当前板可用的 Waveshare 初始化口径：`CTRL1=0x60`、`CTRL5=0x03`，量程仍为 `accel_fs=3` / `gyro_fs=7`。
- `app_main` 默认启动 IMU service，开机即可看到 probe/config/GPIO21 ISR/50Hz sampling 日志。

## 板端证据

- source tests：`uv run python -m unittest tests.test_imu_sensor_source tests.test_imu_service_source tests.test_qmi8658c_source tests.test_imu_motion_source tests.test_extract_imu_raise_samples_script`，24 tests passed。
- whitespace：`git diff --check -- . ':!managed_components'` 仅 LF/CRLF warning，无 whitespace error。
- build：`idf.py build` 通过，`111.bin` `0xac7780`，最小 app 分区 `0xe00000`，剩余 `0x338880` / 23%。
- flash：`idf.py -p COM7 app-flash` 成功，写入 `11302784` bytes，hash verified。
- monitor：`scripts/board/agent_serial_monitor.ps1 -Port COM7 -Mode monitor -DurationSec 35` 成功采集：
  - log: `board_logs/2026-07-07-06-11-28-serial.log`
  - summary: `board_logs/2026-07-07-06-11-28-serial.summary.json`
  - `panic_log_seen=0`
  - 启动日志出现 `imu_service: started: sampling_50hz`、`probe: present=1 who_am_i=0x05 revision_id=0x7b`、`configured registers: ctrl1=0x60 ctrl2=0x33 ctrl3=0x73 ctrl5=0x03 ctrl7=0x03`、`sampling_started: rate_hz=50 window_frames=200`
  - `sample_50hz` 从 `count=1` 持续到 `count=1350`；`count=200` 后 `window_ready=1`
  - 静止样本从之前异常的约 8.8g 修正为接近 1g 量级，例如 `accel_mg=(1,35,-1102)`；移动时 raw/gyro 会明显变化。

## 结论

- 当前固件已经具备可运行的 50Hz IMU 周期采样主线。
- 200 帧窗口能在约 4 秒后填满，后续可以在该 ring buffer 基础上接 fall service 的完整窗口副本投递。
- 本轮没有启用 QMI8658C 芯片侧 WoM/INT 事件源；GPIO21 ISR 仍只作为 service 层 ESP32 GPIO 中断入口。
- `last_sample_interval_us` 会受 I2C 分段读取、日志和系统负载影响出现抖动；从 `sample_50hz count` 的 1 秒递增节奏看，长期采样速率符合 50Hz 主线预期。
