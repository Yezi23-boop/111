---
id: run-imu-default-off-quiet-periodic-logs-2026-07-29
tags: context, runs, attempt-log, imu, fall-detection, startup, quiet-logs, freertos, esp-dl
summary: 将 IMU/Fall 后台链路改为开机默认关闭，并把 IMU、RTC wakeup 和 sleep dry-run 周期日志降到 DEBUG，减少默认 RAM 占用和串口刷屏。
last_reviewed: 2026-07-29
memory_type: run
scope: imu
status: completed
owners: main/app/app_main.c, components/qmi8658c/qmi8658c.c, main/services/sensors/imu_service.c, main/services/power/wakeup_evidence_service.c, main/services/power/sleep_coordinator.c, docs/context/plans/active/2026-06-05-imu-runtime-framework-plan.md, docs/context/plans/active/FALL_DETECTION_V1_111_DEPLOYMENT_DESIGN.md
evidence_level: build
---

# Attempt Log: IMU Default Off + Quiet Periodic Logs

## 背景

- 用户希望关闭串口中周期性刷屏的 `rtc_int_sample`、`dry_run`、`原始表` 和 `采样表` 日志。
- 用户同时要求 IMU 默认关闭，避免未调试跌倒检测时持续采样、占用共享 I2C、PSRAM 和串口输出。
- Fall runtime 依赖 `imu_service` 投递窗口；当 IMU 默认关闭时，Fall runtime 若仍默认启动只会加载模型并占 RAM，因此本轮随 IMU 一起默认关闭。

## 改动

- `main/app/app_main.c`：用 `kMotionServicesEnabledByDefault` 选择启动或销毁路径；默认分支调用 `fall_detection_service_destroy()` 和 `imu_service_destroy()`，再打印 `boot_stage: imu_service_disabled_by_default` 与 `fall_detection_disabled_by_default`。
- `main/services/sensors/imu_service.c/.h`：新增 `imu_service_destroy()`；停止请求通过 task notification 传给 owner task，由 owner task 移除 GPIO ISR、释放任务/PSRAM 运行时资源并回到 `STOPPED`，销毁期间拒绝重复 start。
- `components/qmi8658c/qmi8658c.c`：将周期 `原始表` 从 `ESP_LOGI` 降为 `ESP_LOGD`。
- `main/services/sensors/imu_service.c`：将周期 `采样表` 从 `ESP_LOGI` 降为 `ESP_LOGD`。
- `main/services/power/wakeup_evidence_service.c`：将周期 `rtc_int_sample` 从 `ESP_LOGI` 降为 `ESP_LOGD`。
- `main/services/power/sleep_coordinator.c`：将周期 `dry_run` 从 `ESP_LOGI` 降为 `ESP_LOGD`。

## 验证

- `uv run python -m unittest tests.test_imu_service_source tests.test_fall_detection_service_source tests.test_qmi8658c_source tests.test_wakeup_evidence_source tests.test_power_integration_source`：44 tests passed。
- `git diff --check -- main/app/app_main.c components/qmi8658c/qmi8658c.c main/services/sensors/imu_service.c main/services/power/wakeup_evidence_service.c main/services/power/sleep_coordinator.c tests/test_imu_service_source.py tests/test_fall_detection_service_source.py tests/test_qmi8658c_source.py tests/test_wakeup_evidence_source.py tests/test_power_integration_source.py`：无 whitespace error，仅 LF/CRLF warning。
- `. D:/esp-idf/v5.5.3/esp-idf/export.ps1; idf.py build`：生命周期改造后通过；`111.bin` size `0xabf3e0`，最小 app 分区剩余 `0x340c20` / 23%。

## 风险与下一步

- 本轮只关闭默认启动和 INFO 刷屏，没有删除调试日志；需要观察 IMU 时可打开受控启动块或临时提高对应 tag 的 log level。
- 动态关闭是异步的：调用 destroy 后应等待快照回到 `STOPPED`，再重新 start；Fall 应先销毁，再销毁 IMU。
- COM7 `app-flash-monitor` 已完成刷写并采集到 `boot_stage: imu_service_disabled_by_default`、`fall_detection_disabled_by_default` 和 `startup_sequence_done`；采集工具自身在超时后未生成 summary，但截取日志截至 9 秒未见 panic、Fall 模型加载或 IMU 采样日志，完整 start -> destroy -> start 仍需专门运行时入口覆盖。
