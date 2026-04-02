# Horn-Like Segment Extraction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为当前 `esp32_horn` 数据目录提取每个音频中的 `1s` 喇叭样候选片段，并保留可追溯报告。

**Architecture:** 新增一个独立 Python 脚本，复用 `ffmpeg` 解码输入，再用短时能量和过零率启发式提取峰值事件，最后把每个事件切成 `1s` wav。通过端到端测试验证“一个源文件可切多段，且每段长度固定为 1 秒”。

**Tech Stack:** Python 3.11, `uv`, `ffmpeg`, `wave`, `csv`, `unittest`

---

### Task 1: 先定义脚本行为

**Files:**
- Create: `D:\esp32S3\111\tests\test_extract_horn_like_segments_script.py`

- [ ] 写失败测试，覆盖“单文件两段喇叭样事件 -> 导出两个 1s 片段”
- [ ] 运行 `uv run python -m unittest tests.test_extract_horn_like_segments_script` 确认先失败

### Task 2: 实现最小脚本

**Files:**
- Create: `D:\esp32S3\111\scripts\extract_horn_like_segments.py`
- Modify: `D:\esp32S3\111\docs\context\knowledge\project\audio-low-activity-filter-script.md`
- Modify: `D:\esp32S3\111\docs\context\CHANGELOG.md`

- [ ] 实现 `ffmpeg` 解码、短时窗特征、候选峰值合并、`1s` wav 导出和 `csv` 报告
- [ ] 运行测试确认通过

### Task 3: 在真实目录执行

**Files:**
- Output: `C:\Users\ye\Desktop\stdio\esp32_horn\_horn_like_segments`
- Output: `C:\Users\ye\Desktop\stdio\esp32_horn\horn_like_segments_report.csv`

- [ ] 运行真实批量提取命令
- [ ] 检查导出片段数量与报告结构
- [ ] 汇总路径、数量和可调参数
