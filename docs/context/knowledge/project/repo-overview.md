---
id: project-repo-overview
tags: project, architecture, modules, lvgl, audio, wifi, esp32-s3
summary: 当前仓库的模块地图、启动链路和构建依赖摘要，便于定位 UI、音频和配网相关改动。
last_reviewed: 2026-04-21
memory_type: semantic
scope: repo
owners: main/app/app_main.c, main/services/network/network_service.c, components/network_manager
triggers: repo-overview, startup, owner, architecture, network-manager, provisioning
evidence_level: observed
---

# 当前仓库概览

## 项目定位

- 当前仓库是一个 `ESP32-S3 + ESP-IDF` 固件工程，核心能力覆盖 `LVGL` UI、`CO5300` 显示、`FT5x06` 触摸、音频播放、Wi-Fi/BLE 配网、时间天气、AI 对话、电源观测和存储访问。
- `main/idf_component.yml` 中当前 managed dependencies 包含：
  - `lvgl/lvgl 9.3.0`
  - `espressif/esp_lcd_co5300`
  - `espressif/esp_codec_dev`
  - `espressif/esp_audio_codec`
  - `espressif/esp_audio_effects`
  - `espressif/esp-sr`
  - `chmorgan/esp-audio-player`
  - `espressif/button`

## 启动链路

- 应用入口在 `main/app/app_main.c`。
- `app_main()` 先调用 `app/hardware_init.c` 中的 `hardware_init()`，再创建 `ui/lvgl_task.c` 中的 `lvgl_task`，随后启动：
  - `services/power_service.c`
  - `services/network_service.c`
  - `services/official_chat_service.c`
- `main/app/hardware_init.c` 当前负责初始化 `NVS`、音频 SPIFFS、SD 卡、音频编解码器、按键和 `board_power`，但不再同步初始化旧 `wifi_provision`，也不再阻塞等待 Wi-Fi 连接成功。
- `main/app/hardware_init.c` 还会初始化 `board_power`，并在启动时打印首帧 `Board power boot snapshot`。
- `main/services/network/network_service.c` 在 UI 启动后于后台继续执行：
  - `network_manager_start()`
  - STA 联网状态轮询
  - `api.tenclass.net` / `mqtt.xiaozhi.me` 服务就绪探测
- `main/services/power/power_service.c` 在后台轮询 `board_power` 快照，并以双缓冲方式向 UI 与上层服务发布稳定电源状态。
- `main/services/weather/weather_service.c` 当前仍保留时间天气任务实现，但正式入口里的任务创建保持注释状态。
- 当前仓库的正式启动模型已经从“联网成功后再进 UI”切换为“硬件 ready 后先起 UI，联网后台继续”，这样没网时也能先使用手表基础功能。

## 模块地图

- `main/app`：应用入口与板级初始化。
- `main/services`：后台联网状态机、`official_chat` 生命周期服务与电源状态发布服务。
- `main/features/alerts`、`main/features/danger_detection`：业务特性与提醒链路。（`features/audio`、`features/weather` 已于 07-07 目录整理中收敛到 `services/`，目录已删除）
- `main/ui/generated`、`main/ui/custom`：GUI Guider 生成页面和自定义 UI 逻辑。
- `main/ui/lvgl_task.c`：LVGL 运行时任务和 UI 轮询接缝。
- `main/ui/ui_refresh_policy.c`：UI 活跃/空闲降频、idle dim 与强制高刷策略。
- `components/lvgl_port`：LVGL 显示与输入桥接层。
- `components/co5300_panel`：`CO5300` 面板驱动和 `TE` 同步处理。
- `components/touch_ft5x06`：`FT5x06` 触摸驱动。
- `components/audio_codec`：音频编解码器与 I2S 相关初始化封装。
- `components/mp3_player`：基于 `esp-audio-player` 的播放封装。
- `components/mp3_player` 当前保留为独立底层播放器组件，已不再由 `main/services/weather/weather_service.c` 在启动时隐式初始化。
- `components/wifi_control`：纯 `Wi-Fi STA runtime control`，负责初始化、连接、断开、自动重连和 IP 查询。
- `components/ble_control`：BLE 总开关偏好与 active 状态。
- `components/network_credentials`：最近成功连接的 Wi-Fi 列表。
- `components/network_manager`：当前正式网络统一门面，负责自动联网、断开、重新配网和 transport 选择。
- `components/network_provisioning_adapter`：官方 `network_provisioning` 底层适配层，统一承接 `BLE / SOFTAP` provisioning transport。
- `components/ap_portal_adapter`：自定义浏览器 AP 页面与 SoftAP HTTP API 桥接层。
- `components/axp2101`：只读第一阶段 PMIC 驱动，负责 AXP2101 探测、快照读取和 IRQ 原始状态访问。
- `components/pcf85063atl`：RTC 寄存器、时间和 countdown timer 的器件驱动。
- `components/qmi8658c`：QMI8658C 原始数据、WoM、CTRL9 和 AE 数据面的器件驱动。
- `components/sd_card`：SD 卡挂载和文件访问。
- `main/app/board_power.c`：把 AXP2101 快照转换成板级统一电源语义，并缓存最近一次成功状态。
- `main/app/board_imu.c`：持有 QMI8658C 地址、INT1 GPIO、安装方向和抬腕阈值等板级事实。
- `main/services/sensors/imu_service.c`：长期管理 WoM、原始六轴动作窗口、最终姿态与抬腕结果。
- `main/services/weather/weather_service.c`：时间天气相关应用逻辑；当前配网与联网主链路已经收敛到 `network_manager + network_provisioning_adapter + ap_portal_adapter`。
- `components/ap_portal_adapter/web/` 当前承载自定义 AP 门户网页资源；`components/sd_card/sd_manager.c` 把 SD 卡挂载到 `/sdcard` 并显式放到 `SPI3_HOST` 以避开屏幕的 `SPI2_HOST`。

## 板级与总线要点

- 板级核心器件可从原理图看到 `ESP32-S3R8`、`AXP2101`、`QMI8658C`、`PCF85063ATL`、`GD25Q256`、`ES8311`，代码中还显式接入了 `ES7210` 录音 ADC。
- 显示走独立 `QSPI`，默认引脚见 `components/co5300_panel/co5300_panel_defaults.h`：`PCLK=11`、`CS=12`、`D0..D3=4/5/6/7`、`RST=8`、`TE=13`。
- 触摸与音频控制共用 `I2C` 总线，当前代码使用 `GPIO14/15`；同一总线上原理图还能看到 `QMI8658C` 和 `PCF85063ATL`。
- 音频数据面使用 `I2S0`，当前代码使用 `GPIO16/41/45/40/42/46` 这一组时钟、数据和功放控制引脚。
- 原理图里的 `AXP2101`、`QMI8658C`、`PCF85063ATL` 已能确认存在；其中：
  - `AXP2101` 已有本地驱动接入，并通过 `board_power + power_service` 接入主流程
  - `PCF85063ATL` 已通过 `wakeup_evidence_service` 建立 RTC timer 与 `RTC_INT(GPIO39)` 运行态证据
  - `QMI8658C` 已通过 `board_imu + imu_service` 建立原始数据、WoM、AE 和抬腕证据链；当前样板 `QMI_INT1(GPIO21)` 已确定物理通路浮空/开路，正式 service 使用 20 ms WoM 轮询降级，真实 IRQ 仍需硬件修复
  - `AXP_IRQ/EXIO5` 最终 MCU 映射和真实 ESP sleep 唤醒仍未闭环

## 排查优先级建议

- 显示/触摸问题先看 `components/lvgl_port`、`components/co5300_panel`、`components/touch_ft5x06` 和 `main/ui`。
- 音频播放问题先看 `components/audio_codec`、`components/mp3_player` 和存储路径；麦克风录音链路看 `main/services/audio_diag/audio_mic_test_service.c`。
- 配网/联网问题先看 `main/app/hardware_init.c`、`main/services/network/network_service.c`、`components/network_manager`、`components/network_provisioning_adapter`、`components/ap_portal_adapter` 和 `components/wifi_control`。
- 电源/低功耗问题先看 `components/axp2101`、`main/app/board_power.c`、`main/services/power/power_service.c` 和 `main/ui/ui_refresh_policy.c`。

## 约束提示

- 当前仓库已有未提交固件改动，新增上下文或规则文件时应避免触碰 `main/`、`components/` 下的现有源码修改。
- 显示、触摸、音频和配网改动都属于典型嵌入式多模块任务，默认应先做最小复现、保留日志证据，再决定是否优化。
