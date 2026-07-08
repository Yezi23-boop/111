---
id: attempt-2026-07-08-fall-rf5s-model-deploy
tags: imu, fall-detection, esp-dl, rf5s, board-test
summary: fall-rf5s-model-deploy；结果：success。
last_reviewed: 2026-07-08
memory_type: episodic
scope: task
status: active
result: success
owners: components/fall_detection_inference, main/services/fall_detection_service.c, docs/context/plans/active/FALL_DETECTION_V1_111_DEPLOYMENT_DESIGN.md
triggers: fall-rf5s-model-deploy
evidence_level: observed
record_reasons: evidence, plan-decision
force_reason: 
---

# Attempt Log: fall-rf5s-model-deploy

## 背景

- 本次要验证什么：部署 RF5s 6ch/5s Fall V1 ESP-DL 模型并完成构建与板端自测
- 对应任务或计划：FALL_DETECTION_V1_111_DEPLOYMENT_DESIGN
- 结果状态：success
- 长期记录理由：evidence, plan-decision

## 环境

- 分支/工作区状态：未记录
- 设备/串口/板型：ESP32-S3 COM7
- 关键前置条件：未记录

## 操作

- 修改过的文件或 owner：
- components/fall_detection_inference
- main/services/fall_detection_service.c
- docs/context/plans/active/FALL_DETECTION_V1_111_DEPLOYMENT_DESIGN.md
- 执行的命令或动作：
- 复制 model.espdl 为 tcn_v1_rf5s_6ch_5s_with_test.espdl，更新 CMake、extern symbol、k_model_name、FALL_MODEL_THRESHOLD_DEFAULT=0.85
- uv run python -m unittest tests.test_fall_detection_inference_source tests.test_fall_detection_service_source tests.test_imu_service_source
- idf.py build
- scripts/board/agent_serial_monitor.ps1 -Port COM7 -Action app-flash-monitor -DurationSeconds 70 -Tag fall-rf5s-deploy
- 已尝试但不应直接重复的路径：
- 未记录

## 观测

- 关键日志/证据：
- RF5s SHA256=105b389d696c649114fd4fa520ab57cc626d772489ed31df9281b9d40d8df0ca，与训练 manifest 一致
- board_logs/2026-07-08-16-39-22-fall-rf5s-deploy.log: model loaded shape=[1,1500], threshold=0.85; dl::Model: Test Pass!
- 板端静止窗口判定 ADL，fall_prob=0.0000，infer_ms 约 8.9~10.9ms
- 与预期不一致的点：
- 未记录

## 结论

- 本次可以确认的事实：RF5s retry 模型已嵌入并刷入 COM7，内嵌测试通过；当前还未做人工模拟 FALL 动作验收
- 仍然不能确认的事实：
- 未记录

## 未验证风险

- 下一轮仍需补证据的边界：
- 继续做 post-check 或采集静止/佩戴/模拟跌倒完整验收日志
