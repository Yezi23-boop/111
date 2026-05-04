---
id: attempt-2026-05-04-espdl-danger-single-active-threshold
tags: espdl, danger-detection, model, attempt-log
summary: espdl-danger-single-active-threshold；结果：partial。
last_reviewed: 2026-05-04
memory_type: episodic
scope: task
owners: components/espdl_inference, main/features/danger_detection/danger_detection_service.c, main/ui/custom/danger_detection_controller.c, docs/context/knowledge/project/espdl-danger-model-plan-anchor.md
triggers: ESP-DL DS-CNN-tiny V3.3 threshold 0.80 dual runner
evidence_level: observed
---

# Attempt Log: espdl-danger-single-active-threshold

## 背景

- 本次要验证什么：记录危险声音主工程从双模型尝试收敛到单 active DS-CNN-tiny 和 0.80 阈值的路线，避免后续重复并行 V3.2/V3.3。
- 对应任务或计划：ESP-DL danger detection active model and threshold
- 结果状态：partial

## 环境

- 分支/工作区状态：未记录
- 设备/串口/板型：未涉及或未记录
- 关键前置条件：未记录

## 操作

- 修改过的文件或 owner：
- components/espdl_inference
- main/features/danger_detection/danger_detection_service.c
- main/ui/custom/danger_detection_controller.c
- docs/context/knowledge/project/espdl-danger-model-plan-anchor.md
- 执行的命令或动作：
- 将主工程危险识别从 V3.2/V3.3 双模型并行收敛为单 active V3.3 DS-CNN-tiny
- 部署侧 danger 阈值从 0.40 收紧到 0.80，并加入连续窗口确认、hold/cooldown 清除
- 把 active danger 定义固定为 siren / horn / alarm，glass_break/crash/impact 只放 extended challenger
- 已尝试但不应直接重复的路径：
- 不要默认恢复 dual runner 或同时嵌入 V3.2/V3.3 模型
- 不要把训练阈值和部署后处理阈值混为一谈
- 不要把中文喊话、人声语义危险识别并入当前 active 主线

## 观测

- 关键日志/证据：
- DS-CNN-tiny 版本 edge_mix_teacher_dscnn_tiny_1s_int8input_v20260503 样板板测通过
- CHANGELOG 记录 total internal 5644B、PSRAM 137416B、flash 22368B、推理约 61.77ms
- 与预期不一致的点：
- 未记录

## 结论

- 本次可以确认的事实：当前 active 是单模型 DS-CNN-tiny INT8 + threshold 0.80；后续 V3.4 是 challenger，不自动替换 active。
- 仍然不能确认的事实：
- 真实人声、喇叭和警笛现场样本下 0.80 阈值仍需重新扫

## 未验证风险

- 下一轮仍需补证据的边界：
- 新 student 模型必须报告 threshold=0.80 下指标，并单独统计 hard negative 误报率
