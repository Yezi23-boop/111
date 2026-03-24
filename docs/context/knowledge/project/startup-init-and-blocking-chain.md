---
id: startup-init-and-blocking-chain
tags: project, startup, init, wifi, blocking, lvgl, audio
summary: 当前仓库从上电到 UI 启动的初始化顺序、阻塞点和首启风险摘要。
last_reviewed: 2026-03-11
---

# 启动与阻塞链路

## 主链路

1. `main/111.c` 进入 `app_main()`
2. 调用 `hardware_init()`
3. 在 `hardware_init()` 中依次执行：
   - `hardware_nvs_init()`
   - `audio_app_init()`
   - `sd_manager_init()`
   - `audio_codec_init()`
   - `button_init()`
   - `wifi_provision_init()`
4. `hardware_init()` 调用 `xEventGroupWaitBits(..., portMAX_DELAY)` 等待 Wi-Fi 连接成功
5. 只有 `hardware_init()` 返回 `ESP_OK` 后，才会创建：
   - `lvgl_task`
   - `time_and_weather`

## 当前阻塞点

- `hardware_init()` 会永久等待 `WIFI_CONNECT_BIT`。
- 这意味着在 Wi-Fi 未连上前，UI 任务、时间天气任务和后续页面逻辑都不会启动。
- `app_main()` 在 `hardware_init()` 失败时只打印错误，不会自动重启或降级进入离线 UI。

## Wi-Fi 相关前提

- `wifi_provision_init()` 内部会调用 `wifi_manager_init()`。
- `wifi_manager_init()` 会进入 `STA` 模式并 `esp_wifi_start()`。
- `wifi_manager.c` 的事件处理里在 `WIFI_EVENT_STA_START` 时立即调用 `esp_wifi_connect()`。
- 若设备已有可用 STA 配置，可能直接联网；若没有可用配置，则通常需要通过按钮触发 `wifi_provision_start_apcfg()` 进入 AP 配网。

## 首启与离线风险

- 首启、清空凭据或凭据失效时，系统可能长时间停留在 `hardware_init()` 等待阶段。
- 当前配网入口依赖 `GPIO10` 按键事件，若用户未触发或按键路径异常，界面不会启动。
- 因为 `time_and_weather` 任务在联网后才创建，当前时间同步和播放器初始化也被串在联网之后。

## 调试建议

- 想确认是不是卡在联网，先观察 `HARDWARE_INIT` 和 `wifi_mgr` 日志，而不是先查 LVGL。
- 若是首启问题，优先验证：
  - `GPIO10` 按键是否能触发 AP 配网
  - `wifi_provision` 的 HTML 页面是否能打开
  - `WIFI_EVENT_STA_START` 后是否有重试日志
- 若希望离线也能进 UI，后续需要把“UI 启动”与“Wi-Fi 成功”解耦。

## 适用边界

- 本文基于 `main/111.c`、`main/hardware_init.c`、`components/wifi_provision/src/wifi_provision.c` 和 `components/wifi_provision/src/wifi_driver/wifi_manager.c` 的当前实现整理。
- 若后续把 UI 提前启动、增加离线模式或修改配网策略，需要同步更新本文。
