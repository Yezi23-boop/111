---
id: audio-codec-address-clock-baseline
tags: esp32-s3, audio, codec, es8311, es7210, i2c, i2s, clock
summary: ES8311 与 ES7210 在当前项目中的地址编码、时钟能力和代码映射基线。
last_reviewed: 2026-03-11
memory_type: semantic
scope: board
owners: components/audio_codec, main/services/audio_diag/audio_mic_test_service.c
triggers: audio, codec, address, clock, baseline
evidence_level: observed
status: active
---

# 音频 Codec 地址与时钟基线

## ES8311

- Datasheet 确认：
  - 控制接口为 `I2C`
  - 采样率支持 `8 kHz` 到 `96 kHz`
  - 芯片地址编码为 `0011 00x`，其中 `x = CE`
- 对应 7 位地址：
  - `CE=0 -> 0x18`
  - `CE=1 -> 0x19`
- 当前代码在 `audio_codec.c` 里使用 `0x30`
  - 这是 `0x18` 左移后的 8 位写地址形式

## ES7210

- Datasheet 确认：
  - 控制接口为标准 `I2C`
  - 采样率支持 `8 kHz` 到 `100 kHz`
  - 芯片地址编码为 `1000 0 AD1 AD0`
- 对应 7 位地址范围：
  - `0x40` 到 `0x43`
- 当前代码在 `audio_codec.c` 里使用 `0x80`
  - 这是 `0x40` 左移后的 8 位写地址形式

## 当前项目代码映射

- 控制总线：`GPIO14/15` 的共享 `I2C`
- 数据总线：`I2S0`
- 当前代码采样基线：
  - `AUDIO_DEFAULT_SAMPLE_RATE = 48000`
  - `AUDIO_DEFAULT_BITS_PER_SAMPLE = 16`
  - `AUDIO_DEFAULT_CHANNELS = 2`

## 为什么这张卡重要

- 当前代码里使用的是 8 位写地址风格，而 `i2c_manager_scan()` 打印的是 7 位地址
- 因此排障时要注意：
  - 扫描日志里的 `0x18` 对应代码里的 `0x30`
  - 扫描日志里的 `0x40` 对应代码里的 `0x80`
- 如果把 7 位地址和 8 位地址混用，很容易误判成设备不存在

## 时钟与采样率边界

- `ES8311`/`ES7210` datasheet 都支持更宽采样率范围，但当前项目统一按 `48 kHz / 16 bit` 初始化
- 这意味着后续若改采样率，不只是改 codec，还要联动检查：
  - `I2S` 时钟配置
  - 播放器/录音任务
  - SD/文件路径中的录音格式假设
  - UI 或业务层对音频链路的预期

## 当前排障建议

1. 先看 `i2c_manager_scan()` 是否有 `0x18` 和 `0x40`
2. 再看 `audio_codec_init()` 是否成功创建 `I2S` 和 codec 设备
3. 若需要改采样率，优先先做单向链路验证，不要同时改录音和播放

## 适用边界

- 本文只固化地址和时钟基线，不替代完整 codec bring-up 手册。
- 若后续硬件绑脚导致 `CE/AD0/AD1` 变化，需要同步更新地址映射。
