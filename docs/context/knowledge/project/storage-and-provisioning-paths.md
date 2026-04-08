---
id: storage-and-provisioning-paths
tags: project, storage, sd, spiffs, wifi, provisioning, html
summary: 当前仓库的存储路径、SD 总线选择和 AP 配网页面嵌入方式摘要。
last_reviewed: 2026-04-08
---

# 存储与配网路径

## 存储路径现状

- SD 卡挂载点：`/sdcard`
- 录音文件路径：`/sdcard/record/<timestamp>.wav`
- MP3 示例路径：`/sdcard/mp3/qing.mp3`
- 音频应用初始化入口：`audio_app_init()`
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

- `components/wifi_provision/CMakeLists.txt` 使用 `EMBED_TXTFILES "html/apcfg.html"`
- `wifi_provision.c` 通过 `_binary_apcfg_html_start` 直接引用嵌入的 HTML 内容
- `wifi_provision_start_apcfg()` 会启动 AP 和 WebSocket 服务器，把这份 HTML 作为配网页面提供给用户

## 配网运行路径

1. `hardware_init()` 调用 `button_init()`
2. `GPIO10` 按键回调触发 `wifi_provision_start_apcfg()`
3. `wifi_manager_ap()` 启动热点
4. `ws_server_start()` 提供嵌入式 HTML 页面
5. 用户在页面提交 SSID/密码
6. `wifi_manager_connect()` 切回 STA 连接目标 Wi-Fi

## 排障建议

- AP 页面打不开：先查 `wifi_provision` 组件是否成功注册和启动，再查 `apcfg.html` 是否被正确嵌入
- 能开 AP 但配网后不上线：先查 `wifi_manager_connect()`、重试次数和 `IP_EVENT_STA_GOT_IP`
- 录音或 MP3 路径失败：先查 `/sdcard` 挂载和目录存在性，再查编解码与播放器逻辑

## 适用边界

- 本文基于 `components/sd_card/sd_manager.c`、`components/wifi_provision/CMakeLists.txt`、`components/wifi_provision/src/wifi_provision.c`、`main/ui/lvgl_task.c` 和 `main/features/weather/time_weather.c` 的当前实现整理。
- 若后续把配网页面改为 SPIFFS、网络下载或外置文件系统，需同步更新本文。
