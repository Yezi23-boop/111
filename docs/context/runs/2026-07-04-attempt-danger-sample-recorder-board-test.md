---
id: attempt-danger-sample-recorder-board-test
tags: context, runs, attempt-log, danger-detection, recorder, sd-card, board-test, freertos, kconfig
summary: 新增危险样本 recorder 默认关闭板端自测入口，并完成 COM3 真机 SD WAV/JSON 写入验证。
created: 2026-07-04
updated: 2026-07-04
last_reviewed: 2026-07-04
status: completed
evidence_level: verified
owners: main/features/danger_detection, main/app/app_main.c, main/Kconfig.projbuild
---

# Attempt Log: danger-sample-recorder-board-test

## 背景

用户没有危险测试音源，希望固件侧自动触发一次安全可控的 recorder/SD 闭环测试：启动后约 60 秒注入合成 PCM，模拟一次 `Alerting capture`，确认 `/sdcard/danger_samples` 能写出 WAV 和 JSON。

## 本轮修改

- 新增默认关闭 Kconfig：
  - `CONFIG_DANGER_SAMPLE_RECORDER_BOARD_TEST`
  - `CONFIG_DANGER_SAMPLE_RECORDER_BOARD_TEST_START_DELAY_MS=60000`
- 新增 `main/features/danger_detection/danger_sample_recorder_board_test.c/.h`。
- `app_main` deferred services 阶段调用 `danger_sample_recorder_board_test_start()`；正常固件中开关关闭，入口空返回。
- 测试任务行为：
  - 延迟 60 秒启动。
  - 调用 `danger_sample_recorder_init(NULL)`，已初始化则继续。
  - 使用 `danger_sample_recorder_get_pcm_callback()` 注入合成 16kHz mono int16 PCM。
  - 先喂 32000 样本，以 `window_end_sample_index=32000` 调用 `danger_sample_recorder_capture(1U, 0.95f, 32000ULL)`。
  - 再喂 16000 后置样本，等待 SD worker 写入。
  - 扫描 `/sdcard/danger_samples` 前后 `.wav/.json` 数量，只有两者都有新增才打印 `DANGER_SAMPLE_BOARD_TEST: DONE`。
- source test 锁定默认关闭、只走 recorder API、无网络/通知调用、机器可读日志和 SD 文件数量验证。

## 验证

- source tests：
  - `uv run python -m pytest tests/test_danger_sample_recorder_board_test_source.py tests/test_danger_sample_recorder_source.py`
  - 结果：`11 passed`
- 相关危险识别 source tests：
  - `uv run python -m pytest tests/test_espdl_single_model_runtime_source.py tests/test_danger_detection_service_source.py tests/test_sd_manager_source.py tests/test_danger_sample_recorder_source.py tests/test_danger_sample_recorder_board_test_source.py`
  - 结果：`31 passed`
- 正常固件构建：
  - `. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`
  - 结果：通过；`111.bin` `0xabff00`，最小 app 分区剩余 `0x340100` / 23%。
- 测试固件构建：
  - 使用独立 `build-danger-sample-test` 和临时 `build-danger-sample-test/sdkconfig` 打开测试开关。
  - 结果：通过；`111.bin` `0xac0810`，最小 app 分区剩余 `0x33f7f0` / 23%。
- COM3 真机板测：
  - `idf.py -B build-danger-sample-test ... -p COM3 app-flash` 成功。
  - 串口日志：`board_logs/2026-07-04-03-21-14-danger-sample-recorder-board-test.log`
  - 关键证据：
    - `DANGER_SAMPLE_BOARD_TEST: START delay_ms=60000 wav_before=3 json_before=3`
    - `DANGER_SAMPLE_BOARD_TEST: PCM_READY next_sample_index=32000`
    - `DANGER_SAMPLE_BOARD_TEST: CAPTURE_TRIGGERED window_end=32000 confidence=0.95`
    - 写出 `/sdcard/danger_samples/20260704/032222_1_95.wav`，32000 samples。
    - 写出 `/sdcard/danger_samples/20260704/032222_1_95.json`。
    - `DANGER_SAMPLE_BOARD_TEST: DONE wav_before=3 wav_after=4 json_before=3 json_after=4`
    - 未出现 `DANGER_SAMPLE_BOARD_TEST: FAIL`、`Guru Meditation` 或 `panic`。
- 收尾：
  - 已刷回默认关闭测试的正常固件：`idf.py -B build -p COM3 app-flash` 成功。
  - 根目录 `sdkconfig` 未保留测试开关改动；测试开关只在临时 build 的 sdkconfig 中打开过。

## 结论

板端无人值守测试入口已可验证 recorder + SD 写入闭环：无需测试音源、无需麦克风、无需联网、不会触发手机通知。COM3 实测已证明合成 PCM 触发后新增一组 2 秒 WAV/JSON。

## 剩余风险

- 该测试验证的是 recorder/SD 写入路径，不验证 ESP-DL 模型识别准确率，也不验证真实麦克风采样质量。
- 测试固件会在开机 60 秒后自动写一次 SD；必须保持 `CONFIG_DANGER_SAMPLE_RECORDER_BOARD_TEST` 默认关闭，只有专项板测时再临时打开。
