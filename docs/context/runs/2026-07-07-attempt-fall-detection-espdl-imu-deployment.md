---
id: attempt-2026-07-07-fall-detection-espdl-imu-deployment
tags: context, runs, attempt-log, imu, fall-detection, esp-dl, model-deployment, board-test, com7
summary: 将自训练 ESP-DL IMU 跌倒模型嵌入当前固件，通过 50Hz/200 帧窗口队列完成板端日志闭环。
created: 2026-07-07
last_reviewed: 2026-07-07
status: completed
owners: components/fall_detection_inference, main/services/fall_detection_service.c, main/services/imu_service.c, docs/context/plans/active/2026-06-05-imu-runtime-framework-plan.md
---

# Attempt Log: ESP-DL IMU fall deployment

## 背景

用户确认训练仓库 `D:\esp32S3\imu` 负责模型训练和导出，当前固件仓库 `D:\esp32S3\111` 只负责部署已验证可被 ESP-DL loader 加载的资产。本轮目标是把自训练模型接到现有 50Hz IMU 采样主线，先做串口日志闭环，不接 UI、不触发告警。

部署资产：

- 源文件：`D:\esp32S3\imu\self_trained_cnn_transformer_v1\runs\cnn_c24_pool225_do015_e80\with_test_values\model_with_test.espdl`
- 仓库内嵌入文件：`components/fall_detection_inference/models/esp32s3/cnn_c24_pool225_do015_e80_with_test.espdl`
- SHA256：`10526143f02d047b0e5b2c29f29802396171998cfb4071cb54d7858375a98d54`

## 模型契约

- 输入：`FLOAT [1,600]`
- 窗口：50Hz × 4s = 200 帧
- 通道：`accX/accY/accZ`
- 单位：`m/s^2`
- 展开顺序：按帧交错，`accX0,accY0,accZ0,accX1,accY1,accZ1...`
- 轴向映射：`accX=-chip_accel.x`、`accY=+chip_accel.y`、`accZ=-chip_accel.z`
- 输出：`[ADL,FALL]`
- 后处理：模型内已有 Softmax，板端只读取概率；默认 `FALL` 阈值 `0.80`

## 本轮变更

- 新增 `components/fall_detection_inference`，用 `target_add_aligned_binary_data()` 嵌入 `.espdl`，封装 `fall_model_runner_create/self_test/run/destroy`。
- runner 校验 input shape `[1,600]`、input dtype `FLOAT`、output size 2、label order `[ADL,FALL]`，并调用 `Model::test()` 运行内嵌测试向量。
- `imu_service` 扩展完整窗口出口：内部维护 50Hz/200 帧 ring buffer，ring 满后每 50 帧通过 queue length 1 + `xQueueOverwrite()` 发布完整加速度窗口副本。
- 新增 `main/services/fall_detection_service.c/.h`：加载模型、创建窗口队列、注册给 `imu_service`、消费窗口、做轴向重映射和 flatten、推理并发布只读 snapshot。
- `app_main` 的 deferred services 阶段先启动 `imu_service_start()`，再启动 `fall_detection_service_start()`。
- 第一版只输出 `fall_window_result` 日志和 snapshot，不接 UI、不触发告警。

## 验证

- source tests：
  `uv run python -m unittest tests.test_qmi8658c_source tests.test_imu_service_source tests.test_imu_motion_source tests.test_extract_imu_raise_samples_script tests.test_fall_detection_inference_source tests.test_fall_detection_service_source`
  - 结果：29 tests passed。
- whitespace：
  `git diff --check -- . ':!managed_components'`
  - 结果：无 whitespace error，仅 LF/CRLF warning。
- build：
  `& D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`
  - 结果：通过，`111.bin` size `0xae3f90`，app partition free `0x31c070`，约 22%。
- app-flash：
  `& D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py -p COM7 app-flash`
  - 结果：写入 app 并 hash verified。
- serial monitor：
  `scripts\board\agent_serial_monitor.ps1 -Port COM7 -DurationSeconds 25 -Action monitor -Tag fall-detection-espdl -StreamConsole`
  - log：`board_logs/2026-07-07-06-39-36-fall-detection-espdl.log`
  - summary：`board_logs/2026-07-07-06-39-36-fall-detection-espdl.summary.json`
  - `panic_log_seen=false`
  - monitor 到 25 秒观察窗口后正常收尾，summary `status=captured`、`capture_stop_reason=duration_elapsed`；`exit_code=1` 来自观察窗口到时，不表示固件 panic。

## 板端证据

关键日志：

- `fall_model_runner: [cnn_c24_pool225_do015_e80] model loaded: input=float exp=0 shape=[1, 600], output=float exp=0 shape=[1, 2], threshold=0.80`
- `dl::Model: Test Pass!`
- `fall_model_runner: [cnn_c24_pool225_do015_e80] ESP-DL test vector passed`
- `imu_service: sampling_started: rate_hz=50 window_frames=200`
- `imu_service: window_published: sequence=1 source_sample_count=200 ... frames=200`
- `fall_detection: fall_window_result: sequence=1 source_sample_count=200 label=ADL(0) confidence=0.8520 adl_prob=0.8520 fall_prob=0.1480 threshold=0.80 infer_ms=14.31`

25 秒采集中观察到窗口推理持续到 sequence 13，推理耗时约 14-30ms。当前静止/常规姿态样本输出 ADL，未见 panic/watchdog。

## 结论

- 自训练 `.espdl` 模型已能在当前固件中加载、通过 `Model::test()`，并消费 IMU 50Hz/4 秒窗口运行推理。
- `fall_detection_service` 没有直接调用 `qmi8658c_*` 或 `imu_sensor_read()`，保持 `imu_service -> window queue -> fall_detection_service -> fall_model_runner` 的调用方向。
- 当前只完成部署和日志闭环；真实 FALL/ADL 动作阈值、UI 告警、误报抑制和离线 replay/evaluator 需要后续独立推进。
