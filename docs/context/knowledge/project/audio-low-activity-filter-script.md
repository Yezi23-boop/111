---
id: audio-low-activity-filter-script
tags: project, audio, ffmpeg, dataset, tooling
summary: 使用 ffmpeg 解码后按静音占比、整体 RMS 和包络起伏系数筛选低活动音频的脚本入口与默认阈值。
last_reviewed: 2026-04-01
---

# 低活动音频筛选脚本

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
