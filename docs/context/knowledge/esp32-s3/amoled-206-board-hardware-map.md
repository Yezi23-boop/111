---
id: esp32-s3-amoled-206-board-hardware-map
tags: esp32-s3, schematic, hardware, display, touch, audio, power
summary: 基于原理图和现有代码整理的 ESP32-S3-Touch-AMOLED-2.06 板级器件与关键网络映射。
last_reviewed: 2026-04-09
memory_type: semantic
scope: board
owners: components/co5300_panel, components/touch_ft5x06, components/audio_codec, components/axp2101
triggers: amoled, 206, board, hardware, map
evidence_level: observed
---

# 板级器件与关键网络映射

## 原理图文件位置

- 仓库路径：`D:\esp32S3\111\ESP32-S3-Touch-AMOLED-2.06.pdf`
- 当前上下文默认以仓库内这份 PDF 为准。

## 核心器件

- 主控：`ESP32-S3R8`
- 电源管理：`AXP2101`
- 实时时钟：`PCF85063ATL`
- 传感器：`QMI8658C`
- 外部 Flash：`GD25Q256EYIGR`
- 显示：`CO5300` AMOLED 面板
- 触摸：代码按 `FT5x06/FT3168` 兼容控制器接入
- 音频：代码同时初始化 `ES8311` 播放 codec 和 `ES7210` 录音 ADC

## 关键网络

- 显示 `QSPI` 网络：`LCD_CS`、`QSPI_SIO0..3`、`QSPI_SCL`、`LCD_RESET`、`LCD_TE`
- 触摸网络：`TP_SCL`、`TP_SDA`、`TP_INT`、`TP_RESET`
- 音频网络：`MCLK`、`SCLK`、`LRCK`、`ASDOUT`、`DSDIN`、`PA_OUTL+/-`
- 电源/控制网络：`PWRON`、`PWROK`、`AXP_IRQ`、`VBUS`、`VBAT1/2`
- 存储/扩展网络：`MOSI`、`MISO`、`SCK`、`SDCS`

## 代码中已确认的引脚映射

- 显示 `QSPI`：`PCLK=11`、`CS=12`、`D0=4`、`D1=5`、`D2=6`、`D3=7`、`RST=8`、`TE=13`
- 触摸 `I2C`：`SCL=14`、`SDA=15`、`INT=38`、`RST=9`
- 音频 `I2S/I2C`：`I2C SCL=14`、`I2C SDA=15`、`MCLK=16`、`SCLK=41`、`LRCK=45`、`PA=46`
- 软件按键：当前 `hardware_init.c` 用 `GPIO10` 触发 AP 配网

## 原理图页可直接确认的补充映射

- 共享 `I2C`：`GPIO14 -> ESP32_SCL`、`GPIO15 -> ESP32_SDA`
- `RTC_INT -> GPIO39`
- `QMI_INT1 -> GPIO21`
- `SDCS -> GPIO17`
- `MOTOR -> GPIO18`
- `USB_N -> GPIO19`、`USB_P -> GPIO20`
- `U0TXD -> GPIO43`、`U0RXD -> GPIO44`
- `GPIO10 <- SYS_OUT`
  - 该信号来自 `PWRON` 按键链路经 `BSS138` 与上拉网络变换后的镜像，不是 `PWRON` 原始网络本身

## 共享资源提示

- `GPIO14/15` 在代码中同时承担触摸和音频控制的 `I2C`，原理图上同一组 `ESP32_SCL/SDA` 还连接了 `QMI8658C` 与 `PCF85063ATL`。
- 显示使用独立 `QSPI` 和 `TE` 信号，不与触摸/音频复用总线，但会与 UI 刷新时序强相关。
- 电源由 `AXP2101` 集中管理，后续若做低功耗、背光、唤醒或 RTC 保活，需要优先从 PMIC 视角建模。
- `GPIO10` 虽然当前软件作为配网键使用，但从原理图看它实际接的是 `SYS_OUT`，与 `PWRON` 物理按键链路相关联，后续做电源键语义时不能把它当完全独立的普通按键。

## 适用边界

- 本文基于仓库内该原理图 PDF 的文本提取结果与当前代码交叉整理。
- 原理图在当前环境无法做 Poppler 渲染校验，因此若后续涉及丝印、页号定位或封装方向，请再做一次视觉复核。
