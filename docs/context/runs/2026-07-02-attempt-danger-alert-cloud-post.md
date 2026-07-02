---
id: attempt-danger-alert-cloud-post
tags: context, runs, attempt-log, danger-detection, memory-watch, watch-endpoint, background-https, freertos
summary: 为危险识别 Alerting 首次确认接入 watch endpoint `/v1/watch/alerts` HTTPS POST，并记录验证、worker owner 迁移与 token 鉴权收口。
date: 2026-07-02
last_reviewed: 2026-07-03
memory_type: episodic
scope: repo
owners: main/features/danger_detection/danger_detection_service.c, main/services/watch_endpoint_service.c, main/services/memory_watch_service.c, main/services/memory_watch_voice_client.c
status: completed
evidence_level: observed
---

# Attempt Log: Danger Alert Cloud POST

## 目标

用户要求先改 ESP32 手表端：危险识别进入 `Alerting` 时，通过 HTTPS POST 把告警发到云服务器，先不做 `/v1/watch/alerts` 设备 token 鉴权，优先跑通流程。

目标链路：

```text
ESP32 watch Alerting
  -> https://watch.934000.xyz/v1/watch/alerts
  -> Cloudflare Tunnel
  -> watch_voice_endpoint
  -> Android App WSS
  -> 手机通知栏
```

## 改动

- `watch_endpoint_service` 新增中性危险告警 facade，`danger_detection_service` 只依赖 `watch_endpoint_service_post_danger_alert()`，避免把危险识别业务命名绑定到 Hermes / Memory Watch。
- `memory_watch_voice_client` 新增 `memory_watch_voice_client_post_danger_alert()`，POST JSON 到 `/v1/watch/alerts`。
- `background_https_gate` 新增 `BACKGROUND_HTTPS_GATE_REASON_MEMORY_WATCH_ALERT`，告警 POST 使用后台 HTTPS gate 串行化。
- `watch_endpoint_service` 接管危险告警单槽 worker queue/task：创建 `watch_alert` PSRAM worker，8 秒短超时异步发送告警。
- `memory_watch_service` 不再持有 `mw_alert` worker，也不再暴露 `memory_watch_service_post_danger_alert()`；它只继续持有 endpoint 配置/NVS，并通过 `memory_watch_service_copy_endpoint_config()` 提供只读配置快照。
- `danger_detection_service` 在 Edge Impulse raise 与 ESP-DL 连续窗口确认进入 `Alerting` 时通过中性 watch endpoint service 投递云端告警；ESP-DL 路径仍只在首次确认时发送，不对每个推理窗口重复 POST。
- 2026-07-03 更新：server `POST /v1/watch/alerts` 接入 `_require_device()`，按 body 中 `device_id` 校验 `Authorization: Bearer <device_token>`；未授权请求不广播到 Android App。

## 重要边界

- 服务器已校验 device token；固件侧必须配置与 server `WATCH_DEVICE_TOKENS` 匹配的 `device_token`，否则 `/v1/watch/alerts` 返回 401/403，Android App 不收到告警。
- HTTPS 不在 ESP-DL 推理回调内同步执行；回调只投递 `watch_endpoint_service` 的 FreeRTOS queue，避免 TLS 请求阻塞音频/推理路径。
- endpoint 未配置、网络未 ready、worker queue 已满时，只记录 dispatch skipped，不影响本地屏幕/音频危险告警。

## 验证

- Source tests:

```powershell
uv run python -m unittest tests.test_watch_endpoint_service_source tests.test_memory_watch_service_source tests.test_danger_detection_service_source tests.test_memory_watch_voice_client_source tests.test_background_https_gate_source
```

结果：`44 tests OK`。

- 窄构建验证：

```powershell
. D:\esp-idf\v5.5.3\esp-idf\export.ps1
ninja -C build __idf_main
```

结果：`esp-idf\main\libmain.a` 链接成功，新增 C 代码已通过 main 组件编译。

- 完整构建：

```powershell
. D:\esp-idf\v5.5.3\esp-idf\export.ps1
idf.py build
```

结果：2026-07-02 最新复测已通过，生成 `build/111.bin`，大小 `0xabde50`，最小 app 分区剩余 `0x3421b0`（23%）。历史失败点是 `littlefs-python create ... resources.bin` 报 `LittleFSError -28: LFS_ERR_NOSPC`；该问题已由表盘资源迁出 `resources/` 后解除。

- 真机端到端：

```text
ESP32 watch Alerting
  -> watch_endpoint_service
  -> HTTPS POST /v1/watch/alerts
  -> watch_voice_endpoint
  -> Android App WSS
  -> 手机通知栏
```

结果：2026-07-03 用户反馈“真机测试没问题”，确认危险 `Alerting` 到 Android 手机通知栏的第一版链路已跑通。该证据来自用户真机确认；本记录未保存串口原始日志、设备 token 或手机截图。

- `/v1/watch/alerts` 鉴权：

```powershell
.\.venv\Scripts\python.exe -m pytest
docker compose -f server/watch_voice_endpoint/compose.local.yml up -d --build
```

结果：server tests `144 passed`；公网无 token POST `https://watch.934000.xyz/v1/watch/alerts` 返回 `401 missing_bearer_token`；使用容器 `WATCH_DEVICE_TOKENS` 中的合法 token 做公网 smoke 返回 `HTTP 200 ok=True`。验证过程未打印真实 token。

## 后续

- 若真机开启鉴权后收不到手机通知，优先检查手表端 endpoint 配置中的 `device_token` 是否与 server `WATCH_DEVICE_TOKENS` 匹配。
- Android App 后续可继续补断线重连、最近连接时间和前台服务状态提示。
