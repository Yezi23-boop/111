---
id: esp32-s3-amoled-206-board-hardware-map
tags: esp32-s3, schematic, hardware, display, touch, audio, power
summary: 基于原理图和现有代码整理的 ESP32-S3-Touch-AMOLED-2.06 板级器件与关键网络映射。
last_reviewed: 2026-06-04
memory_type: semantic
scope: board
owners: components/co5300_panel, components/touch_ft5x06, components/audio_codec, components/axp2101
triggers: amoled, 206, board, hardware, map
evidence_level: observed
---

# 板级器件与关键网络映射

## 原理图文件位置

- 当前视觉复核文件：`C:\Users\ye\Desktop\esp32s3手表项目手册\ESP32-S3-Touch-AMOLED-2.06.pdf`
- 2026-06-04 检查时，仓库根目录未发现同名 PDF；板级网络判断以当前视觉复核文件第 1 页为准。

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
- `QMI_INT1 -> GPIO21` 为直接网络，中间没有外部上拉、下拉、串联电阻、RC、复用器或测试点
- `QMI_INT2 -> TP15`，未接入 ESP32；`TP15` 是 QMI 中断输出的板上观测点
- `SDCS -> GPIO17`
- `MOTOR -> GPIO18`
- `USB_N -> GPIO19`、`USB_P -> GPIO20`
- `U0TXD -> GPIO43`、`U0RXD -> GPIO44`
- `GPIO10 <- SYS_OUT`
  - 该信号来自 `PWRON` 按键链路经 `BSS138` 与上拉网络变换后的镜像，不是 `PWRON` 原始网络本身
- `AXP2101 IRQ -> AXP_IRQ / EXIO5`
  - 该信号通过 `RP5 10k` 上拉到 `VCC_RTC`，应按 active-low/open-drain 输入建模
  - 当前 PDF 的 ESP32 GPIO 汇总表没有列出 `AXP_IRQ` 或 `EXIO5`，因此不能从这份原理图推断它已接到某个 ESP32-S3 GPIO
- `AXP2101 PWROK -> CHIP_PU`
  - `PWROK` 属于硬件级 enable/reset 链路，不应在应用层当普通 GPIO 状态脚读写
- `AXP2101 RTCLDO -> VCC_RTC`，`VBACKUP -> VBAT2`
  - RTC 保活与低功耗设计需要把 `VCC_RTC/VBAT2` 一起纳入电源域模型

## 共享资源提示

- `GPIO14/15` 在代码中同时承担触摸和音频控制的 `I2C`，原理图上同一组 `ESP32_SCL/SDA` 还连接了 `QMI8658C` 与 `PCF85063ATL`。
- 共享 I2C 在 codec 区域由 `R23/R49` 两个 `2.2k` 电阻上拉到 `VCC3V3`。
- `TP_INT -> GPIO38` 属于触摸中断，不是 QMI 中断；排查 QMI 时应使用 `GPIO21` 或临时把输出切到 `INT2/TP15`。
- QMI8658C 的 `VDD/VDDIO/CS` 接 `VCC3V3`，使用 I2C 模式；原理图标注地址 `0x6B`，但 `SA0` 接地与 Rev0.6 手册地址描述存在矛盾，当前地址以板级实测为准。
- 原理图标称 `QMI_INT1 -> GPIO21`，但 2026-06-04 当前样板的确定性板测显示 GPIO21 可被内部下拉改变，且不跟随三次真实 `STATUSINT.INT1` WoM 事件；应将该样板视为 INT1 物理通路浮空/开路，不能把原理图网络名当作导通证据。
- 当前样板正式固件会检测该不一致并使用 20 ms `STATUS1.WoM` 轮询降级；若需要真实硬件 IRQ，应测量/修复 U5 INT1 到 GPIO21 的通断，或使用 `INT2/TP15` 飞线。
- 显示使用独立 `QSPI` 和 `TE` 信号，不与触摸/音频复用总线，但会与 UI 刷新时序强相关。
- 电源由 `AXP2101` 集中管理，后续若做低功耗、背光、唤醒或 RTC 保活，需要优先从 PMIC 视角建模。
- `GPIO10` 虽然当前软件作为配网键使用，但从原理图看它实际接的是 `SYS_OUT`，与 `PWRON` 物理按键链路相关联，后续做电源键语义时不能把它当完全独立的普通按键。
- `AXP_IRQ/EXIO5` 不是当前已确认的 MCU GPIO；在没有 PCB 网表、更多板级资料或实测导通证据前，禁止把它配置为 `gpio_isr_handler_add()` 或 ESP sleep wakeup source。

## 适用边界

- 本文基于原理图 PDF 的 Poppler 渲染截图、文本提取结果与当前代码交叉整理。
- 第 1 页是电路原理图，可用于网络连接判断；第 2/3 页更接近 PCB 丝印/布局参考，只能辅助定位器件，不应用来单独推断网络连接。
- 若后续要确认 `EXIO5` 最终是否接入 MCU，需要 PCB 网表、更多板级资料，或用万用表/逻辑分析仪从 `AXP_IRQ/RP5` 到 ESP32-S3 引脚做实测闭环。
