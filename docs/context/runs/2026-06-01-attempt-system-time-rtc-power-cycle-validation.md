---
id: attempt-2026-06-01-system-time-rtc-power-cycle-validation
date: 2026-06-01
result: success
summary: 上板验证 system_time 的 RTC bootstrap、SNTP 写回 RTC，以及真实断电重启后的 RTC 保持。
last_reviewed: 2026-06-01
memory_type: episodic
scope: repo
owners: components/system_time, main/services/system_time_service.c, components/pcf85063atl
triggers: system_time, RTC bootstrap, SNTP writeback, PCF85063ATL, power cycle, board validation
evidence_level: observed
tags: attempt, system-time, rtc, pcf85063atl, sntp, board-validation, power-cycle
record_because: 首次用 COM3 上板日志闭环 system_time owner 的 RTC bootstrap、SNTP writeback 和断电后 RTC 保持。
---

# System Time / RTC 断电保持上板验证

## 背景

本轮目标是验证统一时间 owner 的板端行为，而不是继续改代码。重点证据包括：

- 冷启动时能从 `PCF85063ATL` 读取有效 RTC 时间。
- `system_time_service` 能在联网前用 RTC bootstrap 系统时间。
- 网络 ready 后能通过 SNTP 同步，并写回 RTC。
- 物理断开 USB 和电池后重新上电，RTC 仍保持 `os=0`，并继续用于 bootstrap。

## 环境

- 仓库：`D:\esp32S3\111`
- 目标：ESP32-S3 / ESP-IDF 5.5.3
- 串口：`COM3`
- 烧录方式：`idf.py -p COM3 app-flash`
- RTC：`PCF85063ATL`，I2C 地址 `0x51`

## 操作

阶段 A：写入当前 app 并观察联网校时。

- 执行 `idf.py -p COM3 app-flash`，app hash verified。
- 再执行 `idf.py -p COM3 app-flash monitor`，采集首帧启动、RTC bootstrap、Wi-Fi ready、SNTP sync 和 RTC writeback。
- 日志保存到 `board_logs/2026-06-01-01-16-23-system-time-stage-a-app-flash-monitor.log`。

阶段 B：真实断电保持观察。

- 断开 USB 和电池，等待约 10 秒。
- 重新接回电源，等待 `COM3` 重新枚举。
- 执行 `idf.py -p COM3 monitor` 采集重新上电后的首帧启动日志。
- 日志保存到 `board_logs/2026-06-01-01-20-14-system-time-stage-b-after-power-cycle-monitor.log`。

## 观测

阶段 A 关键日志：

```text
rst:0x15 (USB_UART_CHIP_RESET),boot:0x2b (SPI_FAST_FLASH_BOOT)
Board power boot snapshot: available=1 stale=0 ext=1 bat=1 chg=0 dchg=0 vbat=4119mV vsys=4341mV soc=100%
wakeup_evidence: rtc_time_snapshot: os=0 26-06-01 weekday=1 01:18:13
system_time: rtc bootstrap ok epoch=1780247893
system_time_srv: rtc bootstrap done
NETWORK_SERVICE: network state: WIFI_READY -> SERVICE_READY (critical hosts resolved)
system_time_srv: network time sync scheduled
system_time: SNTP started server0=ntp1.aliyun.com
system_time: system time applied source=2 epoch=1780247898 rtc_writeback=1
system_time: sntp sync ok source=SNTP
system_time_srv: network time sync done
```

阶段 B 关键日志：

```text
rst:0x15 (USB_UART_CHIP_RESET),boot:0x2b (SPI_FAST_FLASH_BOOT)
Board power boot snapshot: available=1 stale=0 ext=1 bat=1 chg=1 dchg=0 vbat=4202mV vsys=4414mV soc=100%
wakeup_evidence: rtc_time_snapshot: os=0 26-06-01 weekday=1 01:20:19
system_time: rtc bootstrap ok epoch=1780248019
system_time_srv: rtc bootstrap done
```

辅助观察：

- 阶段 B 的 `01:20:19` 与阶段 A 的 `01:18:13` 连续递增，说明 RTC 在断电间隔内继续走时。
- 阶段 B 在 Wi-Fi 尚未成功连接前已经完成 `rtc bootstrap done`，说明 bootstrap 不依赖网络。
- `wakeup_evidence` 仍能完成 RTC timer 证据后停止 timer，并保持 `light_sleep_test_skipped: disabled_for_usb_console_safety`，未进入 sleep。

## 结论

- `PCF85063ATL` 当前板端可读，且断电后 `OS` 未置位。
- `system_time_service` 的 RTC bootstrap 链路成立。
- 网络 ready 后 SNTP 同步成立，并写回 RTC：`rtc_writeback=1`。
- 经一次物理断电重启观察，RTC 时间保持成立，且重启后能继续作为系统时间来源。

## 未验证风险

- 这次验证证明“有电池/RTC 备份供电条件下”的保持，不等价于证明拔掉所有 RTC backup 能保持；后者本来就不应保持。
- 阶段 B Wi-Fi 首次 auth 后断开，日志窗口内未再次进入 `SERVICE_READY`，但这不影响 RTC 断电保持结论；SNTP 写回已经在阶段 A 验证。
- 后续若要验证更长时间保持，应做 10 分钟、1 小时、过夜三档断电重启观察，并记录 RTC 与 SNTP 校正差值。
