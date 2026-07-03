---
id: attempt-danger-sample-recorder-board-test-cleanup
tags: context, runs, attempt-log, danger-detection, recorder, sd-card, board-test, cleanup, freertos, kconfig
summary: COM3 板端自测通过后，删除危险样本 recorder 临时板端自测代码，保留正式 recorder 功能与板测证据。
created: 2026-07-04
updated: 2026-07-04
last_reviewed: 2026-07-04
status: completed
evidence_level: verified
owners: main/features/danger_detection, main/app/app_main.c, main/Kconfig.projbuild
---

# Attempt Log: danger-sample-recorder-board-test-cleanup

## 背景

上一轮已通过临时测试固件在 COM3 验证危险样本 recorder/SD 写入闭环：

- 合成 PCM 触发 `danger_sample_recorder_capture(1U, 0.95f, 32000ULL)`。
- `/sdcard/danger_samples/20260704/032222_1_95.wav/.json` 写入成功。
- 文件数量从 `wav_before=3/json_before=3` 增加到 `wav_after=4/json_after=4`。
- 未出现 `DANGER_SAMPLE_BOARD_TEST: FAIL`、`Guru Meditation` 或 `panic`。

用户要求测试没问题后删除测试相关代码，避免正常固件长期保留开机自动写 SD 的入口。

## 本轮修改

- 删除临时板端自测模块：
  - `main/features/danger_detection/danger_sample_recorder_board_test.c`
  - `main/features/danger_detection/danger_sample_recorder_board_test.h`
- 删除 `CONFIG_DANGER_SAMPLE_RECORDER_BOARD_TEST` 和 `CONFIG_DANGER_SAMPLE_RECORDER_BOARD_TEST_START_DELAY_MS`。
- 从 `app_main` deferred services 删除 `danger_sample_recorder_board_test_start()` 调用和 include。
- 从 `main/CMakeLists.txt` 删除自测源文件。
- 删除对应 source test `tests/test_danger_sample_recorder_board_test_source.py` 和 `tests/main_paths.py` 中的自测路径常量。
- 保留正式 recorder、ESP-DL PCM tap、Alerting capture、SD WAV/JSON 写入和 recorder source tests。

## 验证

- 残留检查：
  - `rg -n "danger_sample_recorder_board_test|DANGER_SAMPLE_RECORDER_BOARD_TEST|DANGER_SAMPLE_BOARD_TEST" main tests`
  - 结果：`main/` 和 `tests/` 无残留匹配。
- source tests：
  - `uv run python -m pytest tests/test_espdl_single_model_runtime_source.py tests/test_danger_detection_service_source.py tests/test_sd_manager_source.py tests/test_danger_sample_recorder_source.py`
  - 结果：`26 passed`
- 构建：
  - `. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`
  - 结果：通过；`111.bin` `0xabfea0`，最小 app 分区剩余 `0x340160` / 23%。
- context 校验：
  - `uv run python scripts/context/validate_context.py --level standard --q "danger sample recorder board test cleanup" --brief`
  - 结果：索引 175 个文件，检查 172 个文件，`错误: 0，警告: 0`。

## 结论

板端自测代码已作为临时验证入口移除。当前正常固件不会在开机 60 秒后自动注入 PCM 或写 SD；危险样本正式保存链路仍只随危险识别 `Alerting` 触发。
