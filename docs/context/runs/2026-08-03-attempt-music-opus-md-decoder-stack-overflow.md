---
id: attempt-2026-08-03-music-opus-md-decoder-stack-overflow
tags: music, opus, stack-overflow
summary: music-opus-md-decoder-stack-overflow；结果：partial。
last_reviewed: 2026-08-03
memory_type: episodic
scope: task
result: partial
owners: main/services/music/music_stream_player.cc, tests/test_music_service_source.py
triggers: micro-decoder md_decoder stack overflow Opus music_stream_player
evidence_level: observed
record_reasons: error-signature, evidence
force_reason: 
---

# Attempt Log: music-opus-md-decoder-stack-overflow

## 背景

- 本次要验证什么：验证 Ogg/Opus 的 micro-decoder decoder task 栈容量
- 对应任务或计划：docs/context/plans/active/2026-08-03-online-music-micro-decoder-migration-plan.md
- 结果状态：partial
- 长期记录理由：error-signature, evidence

## 环境

- 分支/工作区状态：未记录
- 设备/串口/板型：COM7 ESP32-S3
- 关键前置条件：未记录

## 操作

- 修改过的文件或 owner：
- main/services/music/music_stream_player.cc
- tests/test_music_service_source.py
- 执行的命令或动作：
- 真实 Ogg/Opus 起播
- idf.py build && idf.py -p COM7 app-flash
- 已尝试但不应直接重复的路径：
- 不要把 8 KiB MP3 decoder 栈继续用于 Opus

## 观测

- 关键日志/证据：
- 用户串口日志：md_decoder 在 decoded Opus PCM 后 stack overflow
- board_logs/2026-08-03-01-27-16-music-opus-stack-fix.log 启动回归无 panic
- 与预期不一致的点：
- 未记录

## 结论

- 本次可以确认的事实：Opus 解码超过 8 KiB PSRAM decoder 栈；已提升到 16 KiB，待真实起播读取历史栈余量。
- 仍然不能确认的事实：
- 16 KiB 的实际历史栈余量待下次点歌采集

## 未验证风险

- 下一轮仍需补证据的边界：
- 点播一次歌曲并确认 md_decoder stack free 日志
