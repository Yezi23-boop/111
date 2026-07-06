---
id: 2026-07-05-attempt-audio-mic-test-ui
tags: context, runs, attempt-log, audio, microphone, danger-detection, ui, lvgl, freertos, sd-card
summary: 在危险识别页新增手动麦克风测试入口，后台录制原始 PCM 并输出串口指标与 SD WAV/JSON。
last_reviewed: 2026-07-05
status: completed
evidence_level: observed
---

# Attempt Log: audio-mic-test-ui

## Goal

- 用户怀疑真实麦克风链路异常，要求增加 UI 按键控制麦克风测试。
- 第一版在危险识别页提供“测麦克风”按钮；点击后录制 5 秒硬件原始 PCM，串口输出 RMS/peak/zero/clip，SD 保存 WAV/JSON。
- 测试不经过 ESP-DL、不触发危险告警、不联网，只验证 `audio_codec` 输入链路。

## Changes

- 新增 `audio_mic_test_service`：
  - `audio_mic_test_service_start()`
  - `audio_mic_test_service_get_snapshot()`
  - 内部创建一次性 FreeRTOS task，结束后 `vTaskDelete(NULL)`。
- 测试 task 运行时通过 `background_service_manager_set_foreground_audio_active(true, "mic_test")` 暂停 Safety Monitor，再申请 `AUDIO_CODEC_OWNER_AUDIO_RECORDER` input session。
- 录制参数为硬件原始输入：24 kHz、2ch、16-bit、`AUDIO_PLATFORM_ADC_CHANNEL_FORMAT="MR"`；默认增益 36 dB。
- 输出目录为 `/sdcard/mic_tests`，写出 `<timestamp>_mic_raw.wav` 和 `<timestamp>_mic_report.json`。
- UI 只调用 service API 和读取 snapshot；不直接调用 `audio_codec_read()`、SD manager 或 FreeRTOS task API。
- host preview mock 已补 `audio_mic_test_service_*` 桩，危险识别页可预览“测麦克风 / 未测试”行。

## Verification

- `uv run python -m pytest tests/test_audio_mic_test_service_source.py tests/test_danger_detection_controller_source.py tests/test_danger_detection_service_source.py tests/test_sd_manager_source.py tests/test_audio_codec_port_source.py`
  - 结果：`32 passed`
- `main/ui/agent_preview/scripts/build_apple_watch_s5_preview.ps1`
  - 结果：通过
- `main/ui/agent_preview/scripts/capture_apple_watch_s5_preview.ps1 -OpenDanger -OutputPath main/ui/agent_preview/artifacts/danger-mic-test-preview.png`
  - 结果：截图生成成功；危险识别页底部显示“安全监听 / 测麦克风 / 未测试”，无明显截断或重叠。
- `. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`
  - 结果：通过
  - `111.bin`：`0xac3070`
  - 最小 app 分区剩余：`0x33cf90` / 23%

## Notes

- 该入口用于硬件诊断，可长期保留；若后续产品化不需要，可单独删除 service 和 UI 按钮。
- 2026-07-05 用户真机点击两次后，录音链路能完整读取 `480000/480000` 字节并写出 WAV，但串口显示 `ch0_rms≈40~42`、`ch0_peak≈182~194`，用户回放反馈“几乎听不到声音”。该证据说明 `audio_codec -> I2S -> SD` 数据通路是活的，当前主要怀疑是输入幅度/通道映射/麦克风硬件路由问题，而不是 ESP-DL 模型阈值问题。
- 随后补强诊断输出：`MIC_TEST` 现在打印每个逻辑通道的 `rms/peak/zero_pct/clip/samples/role`，记录 `audio_codec_set_record_gain(36dB)` 的返回值，并在 M 通道无输入但其他通道有输入时给出 `通道不匹配`。下一次真机测试应重点看 `MIC_TEST: CH0 ... role=M` 与 `MIC_TEST: CH1 ... role=R` 哪个通道有明显峰值。
- ES7210 驱动当前增益上限约为 37.5dB；本仓库测试已使用 36dB，后续不要先假设“继续加到 48dB”能解决，应先验证通道和硬件输入。
