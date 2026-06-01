---
id: attempt-2026-05-16-rtc-pmic-wakeup-evidence-loop
date: 2026-05-16
status: active
result: partial
summary: 首次接入 PCF85063ATL countdown timer 与 wakeup_evidence_service，用运行态日志闭环 RTC_INT(GPIO39) 和 AXP2101 IRQ bank，暂不进入 ESP sleep。
last_reviewed: 2026-05-16
scope: repo
owners: components/pcf85063atl, main/services/wakeup_evidence_service.c, main/app/app_main.c
tags: attempt, power, wakeup, rtc, pmic, pcf85063atl, axp2101
record_because: 首次把 RTC_INT(GPIO39) 与 AXP2101 IRQ bank 做成可上板验证的运行态证据闭环，后续 sleep 唤醒实现会依赖这轮日志口径。
---

# RTC/PMIC 唤醒证据闭环尝试

## 背景

用户要求先做 `RTC/PMIC` 唤醒证据闭环。当前项目已有 AXP2101 只读电源快照、`board_power`、`power_service` 和 `power_policy`，但还没有进入 ESP `Light Sleep / Deep Sleep`。原理图证据显示 `RTC_INT -> GPIO39`，`AXP_IRQ` 只确认到 `EXIO5`，因此本轮目标是先确认唤醒源本身可观测。

## 环境

- 仓库：`D:\esp32S3\111`
- 目标：ESP32-S3 / ESP-IDF 5.5.3
- RTC：`PCF85063ATL`，I2C 地址 `0x51`
- PMIC：`AXP2101`，复用现有只读组件

## 操作

- 新增 `components/pcf85063atl` 最小 driver：
  - probe
  - 读时间
  - 读 `Control_2`
  - 清 RTC interrupt flags
  - 启动秒级 countdown timer
- 新增 `main/services/wakeup_evidence_service.[ch]`：
  - 配置 `GPIO39` 为只读输入
  - 启动 8 秒 RTC countdown timer
  - 周期打印 RTC `Control_2` 与 `GPIO39` 电平
  - 周期读取并清除 AXP2101 IRQ status bank
- 在 `main/app/app_main.c` 的 core policy ready 阶段启动证据服务。
- 新增 source tests 锁定不进入 sleep、不写 GPIO 输出、不把该服务做成策略 owner。
- 2026-05-30 收敛运行态 timer：新增 `pcf85063atl_stop_countdown_timer()`，首次 `TF/GPIO39` 证据成立并清标志后停止 countdown timer，避免每约 8 秒持续刷 `rtc_int_sample control2=0x08`。

## 观测

预期上板日志：

- `wakeup evidence init: rtc_present=1 rtc_int_gpio=39 level=...`
- `rtc_time_snapshot: ...`
- `rtc_timer_armed: seconds=8 rtc_int_gpio=39 initial_level=...`
- `rtc_int_sample: gpio=39 level=... control2=0x...`
- `rtc_timer_flag_observed: clearing_tf`
- `axp_irq_snapshot: irq0=... irq1=... irq2=...`

2026-05-30 COM3 烧录测试观测：

- `idf.py build` 通过，`111.bin` 大小 `0x8d76c0`，factory app 分区剩余 `0x128940`，约 `12%`。
- `idf.py -p COM3 flash` 成功，bootloader、app、partition table、srmodels、assets、resources 均 `Hash of data verified`。
- 运行态串口能看到 RTC countdown timer 周期性置位：
  - `rtc_int_sample: gpio=39 level=0 control2=0x08`
  - `rtc_timer_flag_observed: clearing_tf`
  - 下一秒可读到 `control2=0x00`
- `control2=0x08` 的出现间隔约为 `8s`，说明 `PCF85063ATL Timer_value/Timer_mode/TF` 链路成立。
- 但 `gpio=39 level=0` 在 `control2=0x00` 后仍持续为低，说明 `RTC_INT(GPIO39)` 电平释放或 GPIO 映射还没有闭环。

2026-05-30 GPIO39 内部上拉诊断复测：

- 代码将 `RTC_INT(GPIO39)` 配置为输入并启用 `GPIO_PULLUP_ENABLE`，不改为输出。
- 清 `TF` 后新增 `rtc_timer_flag_cleared` 复读日志。
- 复烧后关键日志：
  - `rtc_int_sample: gpio=39 level=0 control2=0x08`
  - `rtc_timer_flag_observed: clearing_tf`
  - `rtc_timer_flag_cleared: level_before=0 level_after=1 control2_after=0x00`
  - 后续 `rtc_int_sample: gpio=39 level=1 control2=0x00`
- 该结果说明 `RTC_INT -> GPIO39` 映射与 RTC open-drain 行为成立；此前常低主要是 GPIO39 未启用有效上拉导致的观测问题。

2026-05-30 Light Sleep 自动实验尝试：

- 临时加入一次性 `gpio_wakeup_enable(GPIO39, GPIO_INTR_LOW_LEVEL)` + `esp_sleep_enable_gpio_wakeup()` + `esp_light_sleep_start()` 测试，并加 `esp_sleep_enable_timer_wakeup(13s)` 兜底，目标是区分 RTC GPIO 唤醒和 timer 兜底唤醒。
- 烧录成功后，USB 串口/JTAG 日志通道变为不可观测，`pyserial` 无输出，`esptool chip_id` 一度无法连接，说明当前通过主 USB 串口直接做自动 Light Sleep 会破坏调试/下载通道的可观测性。
- 随后将 `WAKEUP_EVIDENCE_LIGHT_SLEEP_TEST_ENABLED` 改为 `0`，保留实验代码但默认不执行，重新构建并刷回安全版。
- 安全版复烧后串口恢复，继续看到运行态 RTC 证据：
  - `rtc_int_sample: gpio=39 level=0 control2=0x08`
  - `rtc_timer_flag_cleared: level_before=0 level_after=1 control2_after=0x00`
- 该轮不能证明 RTC Light Sleep 唤醒失败，只能证明“当前主 USB 调试链路不适合直接跑自动 Light Sleep 实验”。

2026-05-30 Light Sleep 二次测试约束更新：

- 再次打开 `WAKEUP_EVIDENCE_LIGHT_SLEEP_TEST_ENABLED=1` 后构建/烧录测试固件，随后限时抓串口只捕获到运行态 RTC 证据日志，未捕获 `light_sleep_test_enter` / `light_sleep_test_woke` 首段证据。
- 尝试 `idf.py -p COM3 app-flash monitor` 时命令超时并留下 monitor 子进程占用串口，说明 sleep 测试阶段不能依赖无限时 `flash monitor` 作为默认闭环。
- 结束残留 monitor 后，直接用 USB 串口 RTS/DTR 尝试复位没有得到启动日志，`esptool chip_id` 仍可能出现 `No serial data received`。
- 当前结论仍是“不闭环”：没有拿到 `esp_sleep_get_wakeup_cause()`，因此不能判定 RTC GPIO 唤醒成功或失败。
- 已将源码默认约束恢复为 `WAKEUP_EVIDENCE_LIGHT_SLEEP_TEST_ENABLED=0`，source test 锁定默认关闭；后续若继续测，必须先准备外部 UART、手动 BOOT/RST 恢复步骤，或改成用户显式触发后再睡眠。

2026-05-30 Light Sleep 安全门槛落地：

- 将 `wakeup_evidence_service` 调整为先运行普通 RTC countdown timer 证据闭环，再考虑 Light Sleep 测试入口；测试宏仍默认 `0`。
- 前置证据为：`control2=0x08`、`GPIO39 level=0`、清 `TF` 后 `level_after=1 control2_after=0x00`。
- 修正 sleep wake source 清理逻辑，只保留 `gpio_wakeup_disable(GPIO39)` 和 `esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL)`，避免之前 `ESP_SLEEP_WAKEUP_GPIO/TIMER` 单独 disable 造成 `Incorrect wakeup source` 错误日志。
- `light_sleep_test_woke` 日志新增 `cause_name=gpio|timer|other`，便于区分 RTC GPIO 唤醒和 timer 兜底唤醒。
- source tests 通过，`idf.py build` 通过，`111.bin` 大小 `0x8d78a0`，factory app 分区剩余 `0x128760`，约 `12%`。
- `idf.py -p COM3 -b 115200 app-flash` 成功，app hash verified。
- 35 秒串口复测显示默认关闭路径稳定：第一次 RTC 证据闭环后打印 `light_sleep_test_skipped: disabled_for_usb_console_safety`，未出现 `light_sleep_test_enter`，后续 RTC timer 仍每约 8 秒稳定触发并清除。

2026-05-30 运行态 timer 日志收敛：

- 用户复测日志确认 RTC timer 会在清 `TF` 后继续周期触发，约每 8 秒出现一次 `gpio=39 level=0 control2=0x08`。
- 当前证据已经足够证明 RTC timer 与 GPIO39 链路成立，继续周期触发会干扰后续 AXP_IRQ / power policy 观察。
- 新增 `pcf85063atl_stop_countdown_timer()` 并在首次 `rtc_timer_flag_cleared` 成功后调用；预期后续日志只保留普通采样，不再周期性出现 `control2=0x08`。

2026-05-30 无外部 UART 的本地快照方案：

- 用户确认当前只用 Type-C/COM3，不想额外接 USB-TTL，因此不能继续把串口是否返回作为唯一低功耗证据。
- 在 `wakeup_evidence_service.h` 新增 `wakeup_evidence_sleep_test_result_t`，记录 `test_started / entered_sleep / returned_from_sleep / wake_cause / cause_name / GPIO39 / Control_2 / TF / elapsed_ms`。
- 在 `wakeup_evidence_service.c` 新增 `wakeup_evidence_service_get_sleep_test_result()`，只复制后台发布的本地快照，不做 I2C/GPIO 访问，不推进状态机，便于后续 UI 页面读取。
- Light Sleep 测试返回后先写快照，再打印 `light_sleep_test_woke` 和清 RTC `TF`；这样即使 COM3 当时不可观测，只要 CPU 已从 `esp_light_sleep_start()` 返回，UI 或后续日志仍可读取结果。
- 默认测试宏仍为 `0`；source tests 通过，`idf.py build` 通过，`111.bin` 大小 `0x8d78a0`，factory app 分区剩余 `0x128760`，约 `12%`。
- 当前尚未刷入该安全版：上一次临时 Light Sleep 测试固件进入 `light_sleep_test_enter` 后，自动 `app-flash` 和 `--before usb_reset` 均 `No serial data received`，需要手动 BOOT/RST 或等待设备恢复下载模式后再刷。

## 结论

本轮已完成 RTC 运行态证据闭环，但还不是 sleep 唤醒实现。RTC timer flag 能稳定触发和清除，`RTC_INT(GPIO39)` 在内部上拉后能随 `TF` 低/高变化，说明 RTC timer 与 GPIO39 输入观测链路成立。运行态 evidence timer 已收敛为首次证据成立后停止，避免持续周期日志污染后续观察。Light Sleep 自动实验不能直接沿用主 USB 串口链路，否则会丢失可观测/下载通道；下一轮应准备外部 UART 日志、手动恢复步骤或可控触发开关后再测。若看不到 PMIC IRQ 日志，优先继续确认 IRQ enable bit 与 `EXIO5` 到 MCU 的最终映射，不要直接假设 PMIC IRQ 不工作。

## 未验证风险

- 尚未验证 `RTC_INT(GPIO39)` 在 Light Sleep / Deep Sleep 期间的 pull/hold 行为。
- 尚未用外部 UART 或安全触发方式验证 Light Sleep wakeup cause。
- 尚未确认 `AXP_IRQ/EXIO5` 的最终 MCU GPIO。
- 尚未验证 sleep 后的真实唤醒恢复顺序。
