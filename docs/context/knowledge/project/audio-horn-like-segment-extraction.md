---
id: audio-horn-like-segment-extraction
tags: project, audio, ffmpeg, horn, dataset, tooling
summary: 使用 ffmpeg 解码并结合短时 RMS 与过零率启发式提取 1 秒喇叭样候选片段的脚本入口与默认输出。
last_reviewed: 2026-04-01
memory_type: semantic
scope: repo
owners: scripts/extract_horn_like_segments.py, tests/test_extract_horn_like_segments_script.py
triggers: audio, horn, like, segment, extraction
evidence_level: observed
---

# 喇叭样片段提取脚本

## 入口

- 脚本路径：`scripts/extract_horn_like_segments.py`
- 验证测试：`tests/test_extract_horn_like_segments_script.py`
- 运行方式：
  `uv run python scripts/extract_horn_like_segments.py --source-dir "<音频目录>"`

## 当前默认行为

- 先用 `ffmpeg` 将输入统一解码为 `16 kHz / mono / s16le`。
- 以 `20 ms` 窗、`10 ms` hop 计算短时特征。
- 默认使用以下启发式识别“像喇叭”的候选事件：
  - 帧 RMS 高于背景噪声地板的 `3.0` 倍
  - 峰值 RMS 不低于 `-28 dB`
  - 过零率位于 `0.015 ~ 0.18`
  - 事件持续时间位于 `80 ~ 700 ms`
  - 峰值之间最小间隔 `500 ms`
- 每个候选事件按峰值时间导出固定 `1s` 片段。
- 同一源文件可导出多段；相邻过近的候选会去重保留更强峰值。

## 输出

- 默认输出目录：`<source-dir>/_horn_like_segments`
- 默认报告：`<source-dir>/horn_like_segments_report.csv`
- 报告字段包括：
  - `source_file`
  - `clip_file`
  - `peak_time_s`
  - `clip_start_s`
  - `clip_end_s`
  - `peak_rms_db`
  - `peak_zcr`

## 适用边界

- 该脚本只做“像喇叭”的启发式候选提取，不等同于模型级语义判定。
- 若结果过多，优先收紧：
  - `--min-peak-rms-db`
  - `--prominence-factor`
  - `--max-zcr`
  - `--min-peak-gap-ms`
- 若结果过少，优先放宽：
  - `--min-peak-rms-db`
  - `--min-event-ms`
  - `--max-event-ms`
