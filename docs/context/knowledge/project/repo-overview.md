---
id: project-repo-overview
tags: project, architecture, modules, lvgl, audio, wifi, esp32-s3
summary: 当前仓库的模块地图、启动链路和构建依赖摘要，便于定位 UI、音频和配网相关改动。
last_reviewed: 2026-03-11
---

# 当前仓库概览

## 项目定位

- 当前仓库是一个 `ESP32-S3 + ESP-IDF` 固件工程，核心能力覆盖 `LVGL` UI、`CO5300` 显示、`FT5x06` 触摸、音频播放、Wi-Fi 配网、时间天气页面和存储访问。
- `main/idf_component.yml` 中当前依赖包含 `lvgl 9.2.2`、`espressif/esp_lcd_co5300`、`esp_codec_dev`、`chmorgan/esp-audio-player` 和 `espressif/button`。

## 启动链路

- 应用入口在 `main/111.c`。
- `app_main()` 先调用 `hardware_init()`，再创建 `lvgl_task` 和 `time_and_weather` 任务。
- `main/hardware_init.c` 当前负责初始化 `NVS`、音频 SPIFFS、SD 卡、音频编解码器、按键和 `wifi_provision`，并阻塞等待 Wi-Fi 连接事件。
- `hardware_init()` 未返回前，UI 与时间天气任务都不会启动；首启或无有效 Wi-Fi 时，实际可用性高度依赖配网触发路径。

## 模块地图

- `main/ui/generated`、`main/ui/custom`：GUI Guider 生成页面和自定义 UI 逻辑。
- `components/lvgl_port`：LVGL 显示与输入桥接层。
- `components/co5300_panel`：`CO5300` 面板驱动和 `TE` 同步处理。
- `components/touch_ft5x06`：`FT5x06` 触摸驱动。
- `components/audio_codec`：音频编解码器与 I2S 相关初始化封装。
- `components/mp3_player`：基于 `esp-audio-player` 的播放封装。
- `components/wifi_provision`：按钮触发、AP 配网页面和连接状态回调。
- `components/sd_card`：SD 卡挂载和文件访问。
- `main/time_weather.c`、`main/audio_app.c`：时间天气和音频初始化相关应用逻辑；配网与联网主链路在 `components/wifi_provision` 与 `main/hardware_init.c`。
- `components/wifi_provision/CMakeLists.txt` 把 `html/apcfg.html` 直接嵌入固件；`components/sd_card/sd_manager.c` 把 SD 卡挂载到 `/sdcard` 并显式放到 `SPI3_HOST` 以避开屏幕的 `SPI2_HOST`。

## 板级与总线要点

- 板级核心器件可从原理图看到 `ESP32-S3R8`、`AXP2101`、`QMI8658C`、`PCF85063ATL`、`GD25Q256`、`ES8311`，代码中还显式接入了 `ES7210` 录音 ADC。
- 显示走独立 `QSPI`，默认引脚见 `components/co5300_panel/co5300_panel_defaults.h`：`PCLK=11`、`CS=12`、`D0..D3=4/5/6/7`、`RST=8`、`TE=13`。
- 触摸与音频控制共用 `I2C` 总线，当前代码使用 `GPIO14/15`；同一总线上原理图还能看到 `QMI8658C` 和 `PCF85063ATL`。
- 音频数据面使用 `I2S0`，当前代码使用 `GPIO16/41/45/40/42/46` 这一组时钟、数据和功放控制引脚。
- 原理图里的 `AXP2101`、`QMI8658C`、`PCF85063ATL` 已能确认存在，但当前 `main/components` 代码中未发现对应驱动接入或上层业务使用。

## 排查优先级建议

- 显示/触摸问题先看 `components/lvgl_port`、`components/co5300_panel`、`components/touch_ft5x06` 和 `main/ui`。
- 音频播放问题先看 `main/audio_app.c`、`components/audio_codec`、`components/mp3_player` 和存储路径。
- 配网/联网问题先看 `main/hardware_init.c`、`components/wifi_provision` 和 `components/wifi_provision/src/wifi_driver/wifi_manager.c`。

## 约束提示

- 当前仓库已有未提交固件改动，新增上下文或规则文件时应避免触碰 `main/`、`components/` 下的现有源码修改。
- 显示、触摸、音频和配网改动都属于典型嵌入式多模块任务，默认应先做最小复现、保留日志证据，再决定是否优化。
