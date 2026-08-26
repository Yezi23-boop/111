---
id: attempt-2026-06-01-wifi-autoconnect-retry-fix
title: Wi-Fi 上电 latest 自动连接重试修复验证
date: 2026-06-01
result: success
summary: 修复并验证上电 latest Wi-Fi 首次失败后不再被 suppress 窗口吞掉，断连进入自动重试；最终正常固件已刷回并验证自动重试后进入 SERVICE_READY。
last_reviewed: 2026-06-01
memory_type: episodic
scope: repo
owners: components/wifi_control, components/network_manager, main/services/network_service.c
triggers: Wi-Fi autoconnect, latest Wi-Fi, retry, suppress, pre_disconnect, wifi_control, network_manager
evidence_level: observed
tags: attempt, wifi, retry, network-manager, wifi-control, board-validation
record_because: 用户观察到上电自动连接有发起但失败后不重试，手动 Use Saved Wi-Fi 再发起一次才成功。
---

## 背景

修复“上电 latest Wi-Fi 自动连接已经发起，但首次失败后没有形成有效重试；用户进入 Wi-Fi 页面点击 Use Saved Wi-Fi 后再发起一次才成功”的问题。

## 环境

- 仓库：`D:\esp32S3\111`
- 目标：ESP32-S3 / ESP-IDF 5.5.3
- 串口：`COM3`
- 相关 owner：`components/wifi_control`、`components/network_manager`、`main/services/network_service.c`

## 根因判断

`network_service_start()` 会通过 `network_manager_start()` 读取 latest Wi-Fi 并调用 `wifi_control_connect()`，所以问题不是“没有上电自动连接”。旧实现中 `wifi_control_connect()` 无条件先调用 `wifi_control_request_disconnect(true)`，即使冷启动时并没有旧 STA 连接，也会短暂创建 disconnect suppress 窗口。若首次真实断连/认证失败事件落在这个窗口内，就可能被当作显式切换清理事件处理，导致不进入自动重试。

## 操作

- `wifi_control_connect()` 只在当前已经连接或正在连接时才执行“重连前断开”。
- 冷启动、已断开或失败态发起连接时清空 suppress 标记，保证首次真实失败进入自动重试分支。
- 连接日志新增 `pre_disconnect=<0|1>`。
- 断连日志新增 `reason/suppress/auto_reconnect/retry`。

## 观测

Source/build/context:

- `uv run python -m unittest tests.test_wifi_control_source tests.test_wifi_network_runtime_source tests.test_network_manager_source tests.test_network_service_wifi_management_source tests.test_nonblocking_boot_source` 通过。
- `uv run python scripts/context/validate_context.py --level standard --q "Wi-Fi 上电自动连接 重试 suppress pre_disconnect wifi_control network_manager" --brief` 通过。
- `idf.py build` 通过，最终正常固件 `111.bin` 大小 `0x97e050`，app 分区剩余 `0x81fb0`。

Board evidence:

- 正常固件第一次上板：`board_logs/2026-06-01-13-46-32-wifi-autoconnect-retry-pre-disconnect-evidence.log`
  - `connect request: ssid=li pre_disconnect=0`
  - `network state: OFFLINE -> CONNECTING`
- 临时诊断固件只在 RAM 中替换 latest 密码，不写 NVS，用于强制失败：`board_logs/2026-06-01-13-52-00-wifi-autoconnect-forced-failure-retry-evidence.log`
  - `pre_disconnect=0`
  - `reason=15 suppress=0 auto_reconnect=1 retry=0`
  - 后续连续自动重试到 `(5/5)`，最后 `CONNECTING -> OFFLINE`
  - 该诊断补丁随后已撤销。
- 最终正常固件刷回后：`board_logs/2026-06-01-13-55-44-wifi-autoconnect-final-normal-restore.log`
  - `connect request: ssid=li pre_disconnect=0`
  - 首次断连 `reason=201 suppress=0 auto_reconnect=1 retry=0`
  - 自动重试后 `wifi:connected with li`
  - 获取 IP `192.168.104.11`
  - `CONNECTING -> WIFI_READY`
  - `WIFI_READY -> SERVICE_READY`
  - fatal 扫描无 Guru/panic/NO_MEM/watchdog/abort。

## 结论

上电 latest Wi-Fi 会自动发起连接；首次失败不再被 suppress 窗口吞掉，会进入自动重试；最终正常固件已刷回板子并验证自动重试后成功联网。

## 未验证风险

- 若后续发现连接切换场景异常，最小回退点是 `components/wifi_control/src/wifi_control.c` 中 `wifi_control_runtime_needs_disconnect_before_connect()` 与 `wifi_control_connect()` 的条件断开逻辑。
- 不建议回退断连日志字段，因为它们是后续定位 reason/suppress/retry 的关键证据。
