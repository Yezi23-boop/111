---
id: audio-sample-extraction-scripts
tags: knowledge, audio, ffmpeg, sample-extraction, danger-detection, scripts
summary: 危险音频样本预处理脚本合并卡：ffmpeg 解码 + 短时 RMS/过零率提取喇叭样候选片段，以及按静音占比/RMS/包络起伏过滤低活动音频的脚本入口与默认阈值。
status: active
last_reviewed: 2026-08-06
memory_type: semantic
scope: repo
owners: components/espdl_inference, scripts
triggers: ffmpeg, audio, sample, extraction, horn, 喇叭样, 低活动, 静音, rms, 过零率
evidence_level: observed
---

# 音频样本预处理脚本（合并卡）

> 2026-08-06 由 audio-horn-like-segment-extraction 与 audio-low-activity-filter-script 两张碎片合并。

## audio-horn-like-segment-extraction


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



## audio-low-activity-filter-script


## 入口

- 脚本路径：`scripts/analyze_low_activity_audio.py`
- 验证测试：`tests/test_low_activity_audio_script.py`
- 运行方式：
  `uv run python scripts/analyze_low_activity_audio.py --source-dir "<音频目录>"`

## 当前脚本行为

- 用 `ffmpeg` 将输入音频统一解码为 `16 kHz / mono / s16le`。
- 按 `100 ms` 帧计算 RMS。
- 用以下三个指标联合判定“低活动音频”：
  - `overall_rms_db <= -22 dB`
  - `silence_ratio >= 0.35`
  - `envelope_cv <= 0.60`
- 默认输出：
  - `low_activity_report.csv`
  - `low_activity_selected.txt`
- 可选把入选文件复制到单独目录：`--copy-selected-to "<目录>"`

## 适用边界

- 该脚本只按“静音多、整体能量低、帧间起伏小”筛选，不判断语义是不是 `car_horn`。
- 更适合先剔除空白片段、弱信号片段、长静音片段，不适合作为最终语义标注依据。
- 若筛得过严或过松，优先调整：
  - `--max-rms-db`
  - `--min-silence-ratio`
  - `--max-envelope-cv`


