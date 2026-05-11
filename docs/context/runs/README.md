---
id: context-runs-readme
tags: context, runs, episodic-memory, attempt-log, anti-repeat
summary: 说明 runs 目录用于记录单次实验、验证闭环，以及 agent 做过的修改、尝试和避免重复动作的证据。
last_reviewed: 2026-05-04
memory_type: episodic
scope: repo
owners: docs/context
triggers: run, validation, monitor, board-test, agent, 修改, 尝试, 避免重复, 做过什么
evidence_level: design
---

# Runs 目录说明

该目录用于记录单次 bring-up、联调、板测、日志抓取、验证闭环，以及 agent 已经做过的修改和尝试。

最重要的使用场景是避免重复动作：后续 agent 开工前应先按模块、文件名、错误码和症状检索本目录，确认是否已经有人试过同一路径。

```bash
uv run python scripts/context/query.py --scope runs --q "模块 文件 错误码 症状" --top 8
```

推荐记录的内容：

- 任务背景和目标
- 使用的固件/命令/环境前提
- 修改过的文件、owner 或输入条件
- 已尝试但不应直接重复的路径
- 关键日志、观测结果和结论
- 当前结果适用条件与未验证风险

快速记录一次尝试：

```bash
uv run python scripts/context/log_attempt.py --title "短标题" --status partial --record-because repeat-risk --changed path/to/file.c --tried "做过的动作" --avoid "不要重复的动作" --evidence "日志或验证证据" --next "下一步"
```

`--status` 表示本次尝试结果，会写入 frontmatter 的 `result` 字段；frontmatter 的 `status` 保留给记忆生命周期，默认应为 `active`。

推荐文件名：

- `YYYY-MM-DD-short-topic.md`
- `YYYY-MM-DD-attempt-short-topic.md`

适合进入本目录的典型材料：

- 某次 `idf.py monitor` 日志闭环
- 某次 SoftAP/BLE 配网真机联调结果
- 某次 ESP-DL 模型板测与资源占用记录
- 某次 agent 修改了哪些文件、验证了哪些命令、哪条修复路线被证伪
