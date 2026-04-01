---
id: wifi-provision-retry-reasons
tags: [project, wifi, provision, diagnostics, esp32-s3]
summary: 记录当前仓库 Wi-Fi 自动连接失败后常见断连 reason 码的含义，以及哪些告警是正常探测噪声。
last_reviewed: 2026-04-01
---

# Wi-Fi 自动连接重试诊断

- 当前仓库在 `wifi_provision_start_auto()` 下，会先尝试使用已保存凭据走 STA 自动连接。
- 若 `wifi_manager` 连续重试达到 `max_retry`，会回退到 `wifi_provision_start_apcfg()` 启动 AP 配网门户。
- 为了判断真实失败原因，`components/wifi_provision/src/wifi_driver/wifi_manager.c` 已增加 `WIFI_EVENT_STA_DISCONNECTED` 的 `reason` 日志。

# 当前已验证的 reason 码

- `reason=202`
  - `WIFI_REASON_AUTH_FAIL`
  - 含义：认证失败，常见于密码错误、AP 拒绝当前认证尝试。
- `reason=205`
  - `WIFI_REASON_CONNECTION_FAIL`
  - 含义：连接失败，常作为认证/握手失败后的汇总失败结果出现。
- `reason=15`
  - `WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT`
  - 含义：4-way handshake 超时，常见于密码错误、握手阶段兼容性或链路质量问题。

# 当前项目中的判读经验

- 若启动后先反复出现 `202 / 205 / 15`，最后才进入 AP 配网，而在网页重新输入凭据后立即连接成功，则优先怀疑：
  - NVS 中保存的旧密码已过期或输入错误
  - 不是 UI、LVGL 或字体问题
- 日志中若出现：
  - `wifi:state: assoc -> run`
  - 随后又 `run -> init`
  - 同时伴随 `reason=15`
  说明已经完成了关联，但 4-way 握手没有完成。

# 常见“看起来像告警，其实多为正常噪声”的日志

- `sdspi_transaction: cmd=52, R1 response: command not supported`
- `sdspi_transaction: cmd=5, R1 response: command not supported`
  - SD 卡 SPI 模式初始化阶段的兼容探测日志，后续若已 `SD卡挂载成功`，通常不是故障。
- `i2c.master: Please check pull-up resistances whether be connected properly`
  - ESP-IDF I2C 驱动的通用提醒；若后续 codec 设备初始化成功，一般不是当前主故障。
- `Password length matches WPA2 standards, authmode threshold changes from OPEN to WPA2`
  - Wi-Fi 栈根据密码长度自动把鉴权门槛从 OPEN 调整为 WPA2 的信息/提醒日志，不是错误。

# 当前建议

- 看到 `202 / 205 / 15` 组合时，优先先核对 NVS 中保存的凭据是否还是最新密码。
- 若网页重新输入同一 SSID 的新密码后能立刻成功，说明链路和驱动大概率没问题，问题主要在旧凭据。
