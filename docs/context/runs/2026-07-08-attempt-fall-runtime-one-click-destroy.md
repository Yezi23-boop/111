---
id: run-fall-runtime-one-click-destroy-2026-07-08
tags: context, runs, attempt-log, imu, fall-detection, lifecycle, psram, freertos, esp-dl
summary: 为 Fall RF5s 运行时新增对外一键销毁入口，释放模型 runner 与 PSRAM 窗口缓冲，同时保持 imu_service 后台采样。
last_reviewed: 2026-07-08
memory_type: run
scope: imu
status: completed
owners: main/services/fall_detection_service.c, main/services/fall_detection_service.h, tests/test_fall_detection_service_source.py, docs/context/plans/active/FALL_DETECTION_V1_111_DEPLOYMENT_DESIGN.md
evidence_level: build
---

# Attempt Log: Fall Runtime One-Click Destroy

## 背景

- 用户希望后续可在危险警告 UI 中控制跌倒模型，避免 RF5s 模型运行时长期占用 RAM。
- 用户明确补充：`imu_service` 是后台服务，只是可以在危险页面开关；因此本轮只做 fall 模型消费者的销毁入口，不停止 IMU 后台采样。

## 改动

- 新增 `fall_detection_service_destroy()` 对外 API，作为 UI/service 的一键销毁入口。
- 销毁入口先调用 `imu_service_set_window_queue(NULL)` 断开 fall 模型窗口消费者，保留 `imu_service` 50Hz 后台采样。
- 运行中的 `fall_detect` task 进入 `FALL_DETECTION_SERVICE_STATE_STOPPING`，在安全点退出循环，清除 fall 本地告警，再释放 ESP-DL runner、static queue、queue storage、current window 和 model input PSRAM 缓冲。
- 若 start 在 buffer/queue/model/self-test/task 创建阶段失败，统一复用资源释放逻辑，避免 runner 或 PSRAM 缓冲残留。
- `k_alert_receive_timeout_ticks` 从 1000ms 调整为 250ms，作为销毁请求的最大轮询响应延迟；5 秒本地告警自动 clear 逻辑保持不变。

## 验证

- `uv run python -m unittest tests.test_fall_detection_service_source`：8 tests passed。
- `uv run python -m unittest tests.test_fall_detection_service_source tests.test_fall_detection_inference_source tests.test_imu_service_source`：17 tests passed。
- `git diff --check -- main/services/fall_detection_service.c main/services/fall_detection_service.h tests/test_fall_detection_service_source.py`：无 whitespace error，仅 LF/CRLF warning。
- `. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`：通过；`111.bin` size `0xace0b0`，最小 app 分区剩余 `0x331f50` / 23%。
- `uv run python scripts/context/validate_context.py --level standard --q "fall detection runtime destroy psram queue imu_service" --brief`：索引 204 个文件，错误 0，警告 0。

## 风险与下一步

- 本轮只新增服务层销毁入口，尚未把危险警告页 UI 开关接到该 API。
- 需要板端补采启用模型 -> 调用销毁 -> 再次启动模型的 RAM 和日志闭环，确认 `STOPPING -> STOPPED`、`窗口队列已清除`、`已销毁` 日志和 internal/PSRAM 水位符合预期。
- 后续 UI 接入时，UI 只能调用 `fall_detection_service_destroy()` 表达销毁意图，不能直接释放 runner、删除 task 或停止 `imu_service`。
