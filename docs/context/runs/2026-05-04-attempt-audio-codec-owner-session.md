---
id: attempt-2026-05-04-audio-codec-owner-session
tags: audio, codec, espdl, attempt-log
summary: audio-codec-owner-session；结果：partial。
last_reviewed: 2026-05-04
memory_type: episodic
scope: task
owners: components/audio_codec/audio_codec.c, components/audio_codec/include/audio_codec.h, components/espdl_inference/espdl_audio_runtime.cpp, components/traffic_inference/traffic_inference_realtime.cc, tests/test_audio_codec_port_source.py
triggers: audio_codec owner session input output lifecycle stop teardown
evidence_level: observed
---

# Attempt Log: audio-codec-owner-session

## 背景

- 本次要验证什么：记录 audio_codec 生命周期引用计数和 input/output owner session 的改动边界，避免后续 stop 时误拆全局 codec。
- 对应任务或计划：audio codec owner session lifecycle
- 结果状态：partial

## 环境

- 分支/工作区状态：未记录
- 设备/串口/板型：未涉及或未记录
- 关键前置条件：未记录

## 操作

- 修改过的文件或 owner：
- components/audio_codec/audio_codec.c
- components/audio_codec/include/audio_codec.h
- components/espdl_inference/espdl_audio_runtime.cpp
- components/traffic_inference/traffic_inference_realtime.cc
- tests/test_audio_codec_port_source.py
- 执行的命令或动作：
- 为 audio_codec 增加生命周期引用计数与 input/output owner session
- ESP-DL 实时运行时启动前申请录音 input session，退出后释放
- 旧 traffic 实时推理路径也迁到申请/释放 input session
- 已尝试但不应直接重复的路径：
- 不要让某个实时推理 stop 直接 teardown 全局 audio_codec
- 不要让 ESP-DL 和 legacy traffic 同时无 owner 地抢录音输入

## 观测

- 关键日志/证据：
- CHANGELOG 记录 ESP-DL runtime 和旧 traffic realtime 均已改为先申请录音 input session、退出后释放
- 与预期不一致的点：
- 未记录

## 结论

- 本次可以确认的事实：audio_codec 是共享底座，功能模块应申请 owner session，不应自行假设自己拥有全局 codec 生命周期。
- 仍然不能确认的事实：
- 多模块并发启动/停止下 owner session 仲裁仍需更多运行时验证

## 未验证风险

- 下一轮仍需补证据的边界：
- 新增音频输入消费者时，先接 owner session，再考虑 I2S/codec 初始化细节
