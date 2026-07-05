---
id: 2026-07-05-attempt-danger-sensitivity-mode
tags: context, runs, attempt-log, danger-detection, sensitivity-mode, espdl, ui, lvgl
summary: 新增危险识别三档灵敏度，UI 只展示保守/标准/敏感，固件内部映射 ESP-DL 单窗 danger 阈值。
last_reviewed: 2026-07-05
status: completed
evidence_level: observed
---

# Attempt Log: danger-sensitivity-mode

## Goal

- 新增用户级 `sensitivity_mode`：保守 / 标准 / 敏感。
- 第一版直接映射 ESP-DL 单窗 danger 概率阈值：
  - 保守：`0.95`
  - 标准：`0.90`
  - 敏感：`0.85`
- UI 只展示中文模式，不暴露原始阈值数字。

## Changes

- `danger_detection_service` 持有当前 sensitivity mode，提供 setter/getter，并发布包含 `single_window_threshold` 的 policy profile。
- `espdl_model_runner` 增加原子 threshold 和 `espdl_model_runner_set_threshold()`；`espdl_audio_runtime_set_danger_threshold()` 支持运行前保存和运行中下发。
- 危险识别页增加三段式灵敏度控件；点击后由 controller 调用 `danger_detection_service_set_sensitivity_mode()`。
- 切换灵敏度时会 reset ESP-DL 后处理窗口计数，避免继承旧模式的连续窗口状态。
- host preview 增加 `-OpenDanger`，用于直接截图危险识别页。

## Verification

- `uv run python -m pytest tests/test_danger_detection_service_source.py tests/test_espdl_single_model_runtime_source.py tests/test_danger_detection_controller_source.py`
  - 结果：`24 passed`
- `. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`
  - 结果：通过
  - `111.bin`：`0xac1d80`
  - 最小 app 分区剩余：`0x33e280` / 23%
- `main/ui/agent_preview/scripts/capture_apple_watch_s5_preview.ps1 -OpenDanger -OutputPath main/ui/agent_preview/artifacts/danger-sensitivity-preview.png`
  - 结果：截图生成成功，三段控件无明显重叠。

## Notes

- 默认 `标准=0.90`，保持原行为。
- 三档只影响 ESP-DL 单模型后端；Edge Impulse 旧后端暂不接入。
- 第一版不做 NVS 持久化，重启后回到标准模式。
