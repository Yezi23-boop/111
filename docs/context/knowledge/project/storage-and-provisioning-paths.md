---
id: storage-and-provisioning-paths
tags: project, storage, sd, spiffs, wifi, provisioning, html
summary: 当前仓库的存储路径、SD 总线选择和 AP 配网页面嵌入方式摘要。
last_reviewed: 2026-04-25
memory_type: semantic
scope: repo
owners: components/sd_card/sd_manager.c, components/ap_portal_adapter/web, components/network_provisioning_adapter
triggers: storage, provisioning, paths, network_manager, network_provisioning_adapter, ap_portal_adapter
evidence_level: observed
---

# 存储与配网路径

## 存储路径现状

- SD 卡挂载点：`/sdcard`
- 危险样本录音路径：`/sdcard/danger_samples`（`danger_sample_recorder` 输出 WAV+JSON）
- MP3 示例路径：`/sdcard/mp3/qing.mp3`
- SD 卡初始化入口：`sd_manager_init()`

## SD 卡总线选择

- `components/sd_card/sd_manager.c` 显式把 SD 卡放到 `SPI3_HOST`
- 同文件注释说明这样做是为了避开屏幕使用的 `SPI2_HOST`
- 当前代码中的 SD SPI 引脚是：
  - `MOSI=1`
  - `MISO=3`
  - `CLK=2`
  - `CS=17`

## 为什么这点重要

- 屏幕 `CO5300` 已占用独立显示总线；SD 走 `SPI3_HOST` 能减少与显示刷新的直接冲突
- 若后续 SD 读写异常，不要先怀疑 LVGL 缓冲，先确认：
  - SD 卡是否成功挂载到 `/sdcard`
  - `SPI3_HOST` 是否被其他模块复用
  - `record/`、`mp3/` 目录是否存在

## 配网页面来源

- 当前自定义 AP 门户页面资源位于 `components/ap_portal_adapter/web/`
- `ap_portal_adapter` 负责最小 HTTPD、静态资源路由与 SoftAP API 壳：
  - `GET /`
  - `GET /app.js`
  - `GET /app.css`
  - `GET /api/status`
  - `POST /api/scan`
  - `POST /api/configure`
- SoftAP provisioning 底层内核已切到官方 `espressif/network_provisioning`
- 因此当前页面来源已经不是旧 `wifi_provision` 里的嵌入式 `apcfg.html`

## 配网运行路径

1. `app_main()` 启动后台 `network_service`
2. `network_service` 调用 `network_manager_start()`
3. `network_manager` 根据 recent Wi-Fi 决定：
   - 自动尝试 latest Wi-Fi
   - 或停在空闲态，等待用户进入 Wi-Fi 管理页显式选择配网方式
4. 当用户显式选择 `BLE Provision` 时：
   - `network_manager` 会先停止普通 BLE presence，再启动官方 BLE provisioning
5. 当用户显式选择 `AP Web Fallback` 时：
   - `network_provisioning_adapter` 启动官方 SoftAP provisioning manager
   - `ap_portal_adapter` 启动 HTTPD 与自定义网页
6. 当前网页侧可访问状态接口和占位 API：
   - `/api/status`
   - `/api/scan`
   - `/api/configure`
7. 但设备侧 `scan/configure` 仍未真正接通 provisioning 行为：
   - `/api/scan` 当前返回 `501 Not Implemented`
   - `/api/configure` 当前返回 `501 Not Implemented`
8. 因此当前 SoftAP 路径的真实状态是：
   - HTTPD 与网页资源已接好
   - SoftAP provisioning manager 生命周期已接好
   - 浏览器到设备侧的真实配置闭环仍待继续实现

## 排障建议

- AP 页面打不开：先查 `ap_portal_adapter` 是否成功启动，再查 `network_manager` 当前是否真的进入 `PROVISIONING_SOFTAP`
- 页面能打开但点击扫描/配置没有真正生效：先确认当前 `/api/scan`、`/api/configure` 仍是设备侧占位接口，不要误判成 Wi-Fi 驱动或 recent 逻辑故障
- 录音或 MP3 路径失败：先查 `/sdcard` 挂载和目录存在性，再查编解码与播放器逻辑

## 适用边界

- 本文基于 `components/sd_card/sd_manager.c`、`components/ap_portal_adapter`、`components/network_provisioning_adapter`、`components/network_manager`、`main/ui/lvgl_task.c` 和 `main/services/weather/weather_service.c` 的当前实现整理。
- 若后续把配网页面改为 SPIFFS、网络下载或外置文件系统，需同步更新本文。
