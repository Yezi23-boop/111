---
id: display-touch-audio-bus-map
tags: project, display, touch, audio, bus, i2c, i2s, qspi
summary: 当前仓库显示、触摸、音频与配网模块对应的总线、引脚和排障入口图。
last_reviewed: 2026-08-07
memory_type: semantic
scope: repo
owners: components/lvgl_port, components/co5300_panel, components/touch_ft5x06, components/audio_codec
triggers: display, touch, audio, bus, map
evidence_level: observed
status: active
---

# 显示、触摸、音频与总线映射

## 软件模块到硬件的归属

- `components/co5300_panel`：负责 `CO5300` 面板初始化、`QSPI` 传输和 `TE` 同步。
- `components/lvgl_port`：负责 LVGL 显示缓冲、刷新回调和触摸输入注册。
- `components/touch_ft5x06`：负责 `FT5x06/FT3168` 兼容触摸控制器的 `I2C` 读写与复位。
- `components/audio_codec`：负责 `ES8311 + ES7210` 的控制面 `I2C`、数据面 `I2S0` 和功放控制。
- `components/mp3_player`：负责播放器封装。
- `components/network_manager`：负责正式联网门面、自动连接、断开与重新配网。
- `components/network_provisioning_adapter`：负责官方 provisioning 内核与 `BLE / SOFTAP` transport。
- `components/ap_portal_adapter`：负责自定义 AP 门户页面和 HTTP API。
- `main/app/hardware_init.c`：负责把存储、音频、按键和基础硬件初始化串起来；联网主链路已经下沉到后台 `network_service + network_manager`。

## 总线映射

### 1. 显示总线

- 类型：`QSPI`
- 代码入口：`components/co5300_panel/co5300_panel_defaults.h`
- 当前引脚：`11/12/4/5/6/7/8/13`
- 重点风险：
  - 分辨率和颜色格式不一致会直接表现为花屏或颜色错误
  - `TE` 当前默认关闭，若启用需同时检查 `lvgl_port` 的等待点和超时统计

### 2. 触摸与控制面 I2C 总线

- 类型：共享 `I2C`
- 当前代码引脚：`SCL=14`、`SDA=15`
- 代码使用方：`touch_ft5x06`、`audio_codec`、`i2c_manager`
- 原理图同总线器件：触摸控制器、`QMI8658C`、`PCF85063ATL`
- 重点风险：
  - `I2C` 故障会同时影响触摸、codec 控制、IMU 和 RTC
  - 触摸和音频初始化都依赖 `i2c_manager`，排查时要先确认总线而不是只盯单个外设
  - `FT3168` datasheet 说明其在 `Monitor/Sleep` 模式下对多从机总线访问顺序敏感，后续若接入更多从设备，偶发触摸失联要优先怀疑共享总线状态机

### 3. 音频数据面

- 类型：`I2S0`
- 代码入口：`components/audio_codec/audio_codec.c`
- 当前代码引脚：`MCLK=16`、`SCLK=41`、`LRCK=45`、数据线使用 `40/42`、功放控制 `46`
- 重点风险：
  - 初始化顺序错误会导致 codec 正常、播放器仍无声
  - `audio_codec.h` 的宏名与 `audio_codec.c` 中个别注释对 `ASDOUT/DSDIN` 的文字描述存在不一致，重构或改针脚前应先以实际引脚宏和硬件验证为准
  - `ES8311` / `ES7210` 在代码里使用的是 8 位写地址风格，和 `i2c_manager_scan()` 的 7 位扫描结果不能直接混读

## 当前排障路径建议

1. 显示不亮或撕裂：先看 `co5300_panel` 和 `lvgl_port`，确认 `QSPI`、分辨率、颜色格式和 `TE` 行为。
2. 触摸无响应：先看共享 `I2C` 是否正常，再看 `FT5x06` 复位、坐标映射和中断/轮询。
3. 音频异常：先看 `audio_codec_init()` 的 `I2C`、`I2S`、功放和采样率，再看 `mp3_player` 与存储路径。
4. 配网卡死：先看 `network_manager` 当前状态、`network_provisioning_adapter` transport 生命周期和 `ap_portal_adapter` 页面/API，再看后台联网是否卡在重试或 service-ready 探测。
