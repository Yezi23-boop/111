---
id: attempt-2026-07-08-fall-event2s-cnn-recall90-deploy
tags: context, runs, attempt-log, imu, fall-detection, esp-dl, event2s, cnn, recall90, model-deployment
summary: 部署 Fall 2s/6ch CNN recall90 调试模型，并把固件事件窗口与输入契约同步为 100 帧 / [1,600]。
last_reviewed: 2026-07-08
memory_type: episodic
scope: task
status: active
result: partial
owners: components/fall_detection_inference, main/services/fall_detection_service.c, main/services/imu_service.h, docs/context/plans/active/FALL_DETECTION_V1_111_DEPLOYMENT_DESIGN.md
triggers: fall event2s cnn recall90 deploy, weda_v3_event2s_6ch_cnn_c24_k3_pool10_e500_recall90
evidence_level: observed
record_reasons: evidence, plan-decision
force_reason:
---

# Attempt Log: Fall Event2s CNN Recall90 Deploy

## 背景

- 本次要验证什么：将训练 run `weda_v3_event2s_6ch_cnn_c24_k3_pool10_e500_recall90` 的 `.espdl` 部署到 `111` 固件，并同步 2s/6ch 输入契约。
- 对应任务或计划：`FALL_DETECTION_V1_111_DEPLOYMENT_DESIGN`。
- 结果状态：partial；源码与构建已通过，当前机器没有 ESP32-S3 串口，板端日志未采。
- 长期记录理由：模型部署路线、阈值取舍和 5s -> 2s 窗口偏离需要跨会话保留。

## 操作

- 复制 `model.espdl` 为 `components/fall_detection_inference/models/esp32s3/cnn_v1_recall90_6ch_2s_with_test.espdl`。
- 删除旧 RF5s `.espdl` 和 meta，保持模型目录只保留当前模型资产。
- 更新 CMake embed、extern symbol、`k_model_name`、模型 meta、SHA256 和阈值。
- 将 `FALL_MODEL_INPUT_ELEMENTS` 改为 `600U`，`FALL_MODEL_THRESHOLD_DEFAULT` 改为 `0.30f`。
- 将 `IMU_SERVICE_EVENT_PRE_FRAMES/POST_FRAMES` 改为 `35/65`，窗口总长为 50Hz / 2s / 100 帧。
- 保持 `fall_detection_fill_model_input()` 的 6ch 映射不变：`[+imuX,+imuY,-imuZ,+gyroX,+gyroY,-gyroZ]`，gyro 继续从 `deg/s` 转 `rad/s`。

## 观测

- 训练 manifest：`input_shape=[1,600]`，ESP-DL SHA256=`cbe18c7e089bac506ff5229f2ed8c4b728148df5902dce9527aad3a315504684`。
- 训练 config：`family=cnn`、`seq_len=100`、`input_channels=6`、`input_elements=600`、`pool_stride=10`、`fall_threshold=0.3`、`threshold_min_recall=0.9`。
- 数据集 manifest：2s window，event 前 35 帧 + 后 65 帧。
- `metrics.validation_selected_threshold`：threshold `0.30`，验证集 `fall_recall=1.0`、`fall_precision=0.9615`、ADL false positives=1/245。
- `metrics.test_at_validation_selected_threshold`：测试集 `fall_recall=0.9333`、`fall_precision=0.7292`、ADL false positives=26/712。
- `uv run python -m unittest tests.test_fall_detection_inference_source tests.test_fall_detection_service_source tests.test_imu_service_source`：17 tests passed。
- `git diff --check`：无 whitespace error，仅 LF/CRLF warning。
- `idf.py build`：通过，`111.bin` `0xac65e0`，app free `0x339a20`/23%。

## 结论

- 可以确认：固件仓库已切到 2s/6ch CNN recall90 模型，输入长度、事件窗口帧数、阈值、模型 SHA 和 CMake embed 已一致。
- 可以确认：当前 `gyro` 输入模型前仍转换为 `rad/s`，没有退回 `deg/s` 直接入模。
- 不能确认：板端 `Model::test()`、ESP-DL runtime RAM、静止 ADL、翻腕/拍桌误报、模拟 FALL 召回。

## 未验证风险

- 该模型为 recall90 调试路线，阈值 `0.30` 明显更激进；测试集 ADL false positives=26/712，板端误报风险高于 RF5s。
- 当前只改事件窗口为 2s，post-check 仍未实现；背面朝上等静态姿态主要依赖 Event Trigger 门控，动态误报还需要 post-check。
- 当前机器只发现 `COM1`，未执行 `scripts/board/agent_serial_monitor.ps1 -Action app-flash-monitor`；接板后应补采日志。

## 下一步

- 接板后运行 `scripts/board/agent_serial_monitor.ps1 -Action app-flash-monitor -DurationSeconds 70 -Tag fall-event2s-cnn-recall90`。
- 检查日志里是否出现 `cnn_v1_recall90_6ch_2s`、`shape=[1, 600]`、`threshold=0.30`、`dl::Model: Test Pass!`、ESP-DL memory usage，以及静止 ADL / 模拟 FALL 行为。
