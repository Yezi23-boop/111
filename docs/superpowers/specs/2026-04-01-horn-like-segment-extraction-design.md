# Horn-Like Segment Extraction Design

## Goal

针对 `C:\Users\ye\Desktop\stdio\esp32_horn` 中剩余音频，提取每个源音频里“像喇叭声”的候选片段，并导出为独立的 `1s` wav 文件；同一个源文件允许导出多段。

## Approach

- 保持原始音频不变，新建独立输出目录与 `csv` 报告。
- 使用 `ffmpeg` 将输入统一解码为 `16 kHz / mono / s16le`。
- 用 Python 做短时窗分析，结合以下启发式识别候选事件：
  - 帧 RMS 相对背景明显抬升
  - 过零率落在更接近“有音高、短促突发”的区间，而不是宽带噪声
  - 候选事件持续时间有限，避免把整段长背景声都切出来
- 每个候选事件取峰值时间点，向前后扩展，导出固定 `1s` 片段。
- 对时间上过近的候选峰值做去重，避免重复切出几乎相同的片段。

## Outputs

- 脚本：`scripts/extract_horn_like_segments.py`
- 测试：`tests/test_extract_horn_like_segments_script.py`
- 输出目录：默认 `<source-dir>/_horn_like_segments`
- 报告：默认 `<source-dir>/horn_like_segments_report.csv`

## Constraints

- 这是启发式“像喇叭”的候选提取，不是模型级语义保证。
- 默认优先保证批量可跑、结果可审阅、参数可调。
- 原始输入文件不移动、不删除、不覆盖。
