---
id: startup-init-and-blocking-chain
tags: project, startup, init, wifi, blocking, lvgl, audio
summary: 当前仓库从上电到 UI 启动的初始化顺序、后台联网分工和首启风险摘要。
last_reviewed: 2026-04-08
---

# 启动与阻塞链路

## 主链路

1. `main/app/app_main.c` 进入 `app_main()`
2. 调用 `main/app/hardware_init.c` 中的 `hardware_init()`
3. 在 `hardware_init()` 中依次执行：
   - `hardware_nvs_init()`
   - `audio_app_init()`
   - `sd_manager_init()`
   - `audio_codec_init()`
   - `button_init()`
   - `wifi_provision_init()`
4. `hardware_init()` 返回 `ESP_OK` 后，`app_main()` 会继续：
   - 创建 `main/ui/lvgl_task.c` 中的 `lvgl_task`
   - 启动 `main/services/network_service.c` 后台任务
   - 调用 `main/services/official_chat_service.c` 完成 service 初始化
5. `time_and_weather` 任务实现仍保留在 `main/features/weather/time_weather.c`，但正式入口中的任务创建当前保持注释。

## 当前阻塞点

- `hardware_init()` 已不再等待 `WIFI_CONNECT_BIT`，UI 与后台联网已解耦。
- 当前主要等待点转移到后台：
  - `network_service` 的 STA 联网轮询
  - `api.tenclass.net` / `mqtt.xiaozhi.me` 的 DNS / 服务就绪探测
- `app_main()` 在 `hardware_init()` 失败时仍只打印错误，不会自动重启或降级进入离线 UI。

## Wi-Fi 相关前提

- `wifi_provision_init()` 内部会调用 `wifi_manager_init()`。
- `wifi_manager_init()` 会进入 `STA` 模式并 `esp_wifi_start()`。
- `wifi_manager.c` 的事件处理里在 `WIFI_EVENT_STA_START` 时立即调用 `esp_wifi_connect()`。
- 若设备已有可用 STA 配置，可能直接联网；若没有可用配置，则通常需要通过按钮触发 `wifi_provision_start_apcfg()` 进入 AP 配网。

## 首启与离线风险

- 首启、清空凭据或凭据失效时，系统会先进入 UI，再由 `network_service` 留在 BLE 配网、AP 门户或后台连接阶段。
- 当前配网入口依赖 `GPIO10` 按键事件，若用户未触发或按键路径异常，联网状态会停留在后台 service 层。
- 因为 `time_and_weather` 任务创建当前仍注释，时间同步链路尚未重新并回正式入口。

## 调试建议

- 想确认是不是卡在联网，先观察 `HARDWARE_INIT`、`NETWORK_SERVICE` 和 `wifi_mgr` 日志，而不是先查 LVGL。
- 若是首启问题，优先验证：
  - `GPIO10` 按键是否能触发 AP 配网
  - `wifi_provision` 的 HTML 页面是否能打开
  - `WIFI_EVENT_STA_START` 后是否有重试日志
- 若希望时间同步等后处理也并回正式入口，下一步应单独评估 `time_and_weather` 的恢复时机和阻塞边界。

## 适用边界

- 本文基于 `main/app/app_main.c`、`main/app/hardware_init.c`、`main/services/network_service.c`、`components/wifi_provision/src/wifi_provision.c` 和 `components/wifi_provision/src/wifi_driver/wifi_manager.c` 的当前实现整理。
- 若后续把 UI 提前启动、增加离线模式或修改配网策略，需要同步更新本文。
