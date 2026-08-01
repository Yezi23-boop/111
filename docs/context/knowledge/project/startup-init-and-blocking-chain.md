---
id: startup-init-and-blocking-chain
tags: project, startup, init, wifi, blocking, lvgl, audio
summary: 当前仓库从上电到 UI 启动的初始化顺序、后台联网分工和首启风险摘要。
last_reviewed: 2026-04-25
memory_type: semantic
scope: repo
owners: main/app/app_main.c, main/app/hardware_init.c, main/services/network/network_service.c, main/services/power/power_service.c
triggers: startup, init, and, blocking, chain
evidence_level: observed
---

# 启动与阻塞链路

## 主链路

1. `main/app/app_main.c` 进入 `app_main()`
2. 调用 `main/app/hardware_init.c` 中的 `hardware_init()`
3. 在 `hardware_init()` 中依次执行：
   - `hardware_nvs_init()`
   - `sd_manager_init()`
   - `audio_codec_init()`
   - `board_power_init()`
   - `button_init()`
4. `hardware_init()` 返回 `ESP_OK` 后，`app_main()` 会继续：
   - 创建 `main/ui/lvgl_task.c` 中的 `lvgl_task`
   - 启动 `main/services/network/network_service.c` 后台任务
   - 调用 `main/services/official_chat_service.c` 完成 service 初始化
5. `time_and_weather` 任务实现仍保留在 `main/services/weather/weather_service.c`，但正式入口中的任务创建当前保持注释。

## 当前阻塞点

- `hardware_init()` 已不再等待 `WIFI_CONNECT_BIT`，UI 与后台联网已解耦。
- 当前主要等待点转移到后台：
  - `network_service` 的 STA 联网轮询
  - `api.tenclass.net` / `mqtt.xiaozhi.me` 的 DNS / 服务就绪探测
- `app_main()` 在 `hardware_init()` 失败时仍只打印错误，不会自动重启或降级进入离线 UI。

## Wi-Fi 相关前提

- 当前正式网络启动链已收敛为：
  - `network_service_start()`
  - `network_manager_start()`
- `network_manager_start()` 内部会初始化并编排：
  - `wifi_control`
  - `ble_control`
  - `network_credentials`
  - `network_provisioning_adapter`
- 若设备存在 recent Wi-Fi 记录，会先尝试最新一条。
- 若 latest 连接失败，系统会停在合法空闲态，等待用户进入 Wi-Fi 管理页点击 `BLE Provision` 或 `AP Web Fallback`。
- 若没有 recent 且默认 transport 为 `BLE`，但 BLE 总开关已关闭，则会停在合法空闲态，而不是直接启动失败。

## 首启与离线风险

- 首启、清空凭据或凭据失效时，系统会先进入 UI，并等待用户从 Wi-Fi 管理页显式选择 `BLE Provision` 或 `AP Web Fallback`。
- 当前默认配网入口已经回收到 UI，不再依赖 `GPIO10` 的单击/多击配网映射。
- 因为 `time_and_weather` 任务创建当前仍注释，时间同步链路尚未重新并回正式入口。

## 调试建议

- 想确认是不是卡在联网，先观察 `HARDWARE_INIT`、`NETWORK_SERVICE` 和 `wifi_mgr` 日志，而不是先查 LVGL。
- 若是首启问题，优先验证：
  - `network_manager` 当前是否进入 `IDLE / PROVISIONING_BLE / PROVISIONING_SOFTAP / CONNECTING`
  - `ap_portal_adapter` 的页面与 `/api/status` 是否可访问
  - `wifi_control` 的重试日志是否符合预期
- 若希望时间同步等后处理也并回正式入口，下一步应单独评估 `time_and_weather` 的恢复时机和阻塞边界。

## 适用边界

- 本文基于 `main/app/app_main.c`、`main/app/hardware_init.c`、`main/services/network/network_service.c`、`components/network_manager`、`components/network_provisioning_adapter`、`components/wifi_control` 的当前实现整理。
- 若后续把 UI 提前启动、增加离线模式或修改配网策略，需要同步更新本文。
