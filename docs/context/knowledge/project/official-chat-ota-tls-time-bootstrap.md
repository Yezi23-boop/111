---
id: official-chat-ota-tls-time-bootstrap
tags: [project, official-chat, ota, tls, sntp, time]
summary: official_chat 的 OTA/激活 HTTPS 请求会先于正式时间同步链路发生；若系统仍停留在冷启动时间，TLS 证书有效期校验会失败，因此需要在首次 OTA HTTPS 前先确认系统时间有效并输出 TLS 诊断日志。
last_reviewed: 2026-06-01
memory_type: semantic
scope: repo
owners: main/services/official_chat_service.c, main/services/network/network_service.c, components/official_chat
triggers: official, chat, ota, tls, time, bootstrap, 时间, 授时, 迁移
evidence_level: observed
route_area: "Official Chat"
---

# official_chat OTA TLS 首次授时约束

## 结论

- 当前仓库的正式启动入口还没有把首轮时间同步重新并回主链路。
- `official_chat` 的 OTA 版本检查却会在激活阶段第一时间发起 HTTPS 请求。
- 如果此时系统时间仍停留在冷启动默认值，`mbedtls` 证书有效期校验会失败，表面症状通常是：
  - `mbedtls_ssl_handshake returned -0x2700`
  - `official_ota: version check request failed: ESP_ERR_HTTP_CONNECT`
- 因此 OTA 首次 HTTPS 请求前，必须先确认系统时间进入 TLS 可用区间；否则即使 Wi-Fi、DHCP、DNS 都正常，也会在证书校验阶段失败。

## 当前仓库中的真实触发条件

- `main/app/app_main.c`
  - 正式入口当前只启动：
    - `lvgl_task`
    - `network_service`
    - `official_chat_service`
  - `time_and_weather` 任务创建保持注释。
- `docs/context/knowledge/project/startup-init-and-blocking-chain.md`
  - 已明确记录“时间同步链路尚未重新并回正式入口”。
- `components/official_chat/ota.cc`
  - `CheckVersion()` 会先发 OTA 版本查询 HTTPS 请求。
  - 旧实现只有在请求成功拿到响应后，才通过 `server_time` 调 `settimeofday()`。

这意味着：

- 首次 HTTPS 如果依赖有效系统时间才能成功；
- 而有效系统时间又依赖这次 HTTPS 成功后的 `server_time`；
- 二者形成了冷启动死锁。

## 已落地修复

- 在 `components/official_chat/ota.cc` 中，为所有 HTTPS JSON 请求增加了“先校验系统时间”的前置步骤。
- 若时间无效：
  - 复用仓库既有 NTP 服务器集合启动或重启 `SNTP`
  - 在有限时间窗口内等待首次授时
  - 成功后再继续 HTTPS 请求
  - 超时则直接返回错误，并打印当前时间、SNTP 状态和 reachability
- 同时为 HTTP/TLS 失败补充了额外诊断：
  - `errno`
  - `esp_http_client_get_and_clear_last_tls_error()` 返回值
  - `mbedtls` 原始错误码
  - `TLS flags`
  - 当前 UTC 时间快照
- `server_time.timestamp` 是 Unix epoch 毫秒，属于 UTC 绝对时间：
  - `official_chat` 只负责解析并转交给 `system_time` owner。
  - `timezone_offset` 只能用于显示本地时间，不能加到 epoch 后再写回系统时间或 RTC。
  - 若东八区把 `timezone_offset` 再加进 epoch，会导致系统时间和 RTC 被写快 8 小时。

## 日志判读

- 若看到：
  - `system time invalid before HTTPS request`
  - `SNTP started for OTA TLS bootstrap`
  - `time snapshot stage=after_sntp_sync`
  - 随后 OTA 请求成功
  - 说明根因就是冷启动时间无效。
- 若看到：
  - `http tls diagnostics stage=open`
  - `tls_code=-9984`（即 `-0x2700`）
  - 说明 TLS 失败发生在握手阶段，优先检查系统时间与 CA 信任链。
- 若看到：
  - `system time still invalid after SNTP wait`
  - 说明当前板端即使联网成功，NTP 仍未在超时时间内完成首次授时，需要继续排查 SNTP 服务器可达性或系统网络策略。

## 验证方法

1. 冷启动设备，确保之前未保留可信 RTC 时间。
2. 完成配网并进入 `official_chat` 激活阶段。
3. 观察是否先出现时间快照与 SNTP bootstrap 日志，再继续 OTA HTTPS。
4. 若激活成功，应不再出现首轮 `mbedtls_ssl_handshake returned -0x2700`。

## 证据文件

- `D:\esp32S3\111\components\official_chat\ota.cc`
- `D:\esp32S3\111\main\app\app_main.c`
- `D:\esp32S3\111\main\features\weather\time_weather.c`
- `D:\esp32S3\111\components\system_time\system_time.c`
- `D:\esp32S3\111\main\services\system_time_service.c`
- `D:\esp32S3\111\docs\context\knowledge\project\startup-init-and-blocking-chain.md`
