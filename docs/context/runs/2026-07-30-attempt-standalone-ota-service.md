---
id: attempt-standalone-ota-service
tags: run, ota, https, rollback, maintenance-session, esp32s3, com7
summary: 独立 HTTPS 双槽 OTA service、owner maintenance、manifest transport 与 early boot-check 的实现和 COM7 冷启动验证。
memory_type: run
scope: repo
evidence_level: observed
status: completed
last_reviewed: 2026-07-30
---

# Attempt Log: 独立 HTTPS 双槽 OTA service

## 结果

- 阶段 1–5 代码已落地：`ota_service`、全屏维护页、owner maintenance API、manifest 校验、`esp_https_ota` transport、Flash SHA-256 admission、rollback boot-check。
- `idf.py build` 通过；当前 `build/111.bin` 为 `0xAB5370`，仍小于 12 MiB OTA 槽。
- 聚焦测试 `tests/test_ota_service_source.py`、host OTA、官方聊天/网络/BLE/音频相关测试共 44 passed。
- COM7 `app-flash-monitor` 冷启动 30 秒通过：`ota_boot_check` 在 UI/后台服务前读取 `ota_0`；LittleFS `/resources` 正常；无 panic/Guru/Display flush/ESP_ERR_NO_MEM。
- 最终构建再次刷入 COM7 并采集 25 秒：`board_logs/2026-07-30-18-56-43-ota-final-coldboot.log`，`AGENT_SERIAL_MONITOR_PANIC_LOG_SEEN=0`。
- COM7 `otatool.py read_otadata` 前后均为 primary `OTA_SEQ=0x00000001`、secondary `0xffffffff`，本轮未写 OTA 数据选择分区。

## 关键边界

- `official_chat` OTA 业务未复用；独立 manifest 只接受 HTTPS、配置 CA、允许 host、有效系统时间、递增版本、槽大小内镜像和 64 位十六进制 SHA-256。
- `ota_transport` 独占 `esp_https_ota_*`；`finish()` 前同时检查 HTTP image size、实际接收字节、manifest size、备用槽 SHA-256，失败/取消走 abort。
- OTA task 只编排 owner API；聊天、网络 provisioning、录音和 MP3 自己停止并发布 snapshot，Wi-Fi STA 基础链路保留。
- 阶段 6 的真实下载/掉电矩阵仍需要 COM7 能访问的 HTTPS CA/manifest；当前设备日志显示 Wi-Fi SSID `li` 未连接，因此不能把静态 hook 或冷启动误当成故障闭环。

## 证据

- `board_logs/2026-07-30-18-49-57-ota-stages-5-6.log`
- `board_logs/2026-07-30-18-56-43-ota-final-coldboot.log`
- `docs/context/plans/active/2026-07-30-standalone-https-ota-maintenance-plan.md`
