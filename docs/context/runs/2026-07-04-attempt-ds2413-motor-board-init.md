---
id: 2026-07-04-attempt-ds2413-motor-board-init
tags: context, runs, attempt-log, ds2413, motor, haptic, hardware-init, freertos, esp32s3
summary: 记录从 59e29713 抽取 DS2413 马达板级能力并接入当前主线 Board Foundation 的实现、验证与边界。
last_reviewed: 2026-07-04
status: completed
record_because: route-choice, hardware-init, freertos, evidence
changed: components/ds2413, main/app/board_ds2413_motor.c, main/app/hardware_init.c, main/CMakeLists.txt, main/idf_component.yml
evidence_level: observed
---

# Attempt Log: DS2413 Motor Board Init

## 目标

- 从 `59e29713ba4ffbd4d6aef9eaf06f9bc0a29bf50d` 抽取 DS2413 马达板级能力。
- 不 cherry-pick 整提交，不带入 `app_main_test.c` 测试入口或 `scratch/ds2413_motor_migration`。
- 只实现板级默认关断和最小开关/脉冲 API，不接 `app_alert_manager`、UI、低电量提示或 Hermes 通知。

## 关键实现

- 新增 `components/ds2413` 最小 1-Wire 驱动，只支持当前板测 family code `0xBA`。
- 新增 `main/app/board_ds2413_motor.c/.h`：
  - GPIO18 作为 1-Wire 总线；
  - 只使用 RMT backend，不保留 UART1 兜底；
  - FreeRTOS mutex 串行化 DS2413 访问；
  - PIOA release 打开马达，PIOA pull-low 关闭马达，PIOB 始终 release；
  - 初始化成功后立即写入默认关闭态。
- `hardware_init()` 在 NVS 成功后、SD/codec 等较慢初始化前调用 `board_ds2413_motor_init()`；失败只记录 warning，不阻断启动。
- `main/idf_component.yml` 加入 `espressif/onewire_bus: ^1.1.1`，`main/CMakeLists.txt` 接入 `board_ds2413_motor.c`、`ds2413` 与 `espressif__onewire_bus`。

## 验证

- 2026-07-04 补充注释：完善 DS2413 公开接口契约、GPIO18/RMT-only 约束、FreeRTOS mutex 串行化原因、open-drain release/pull-low 语义和协议字节说明；未改变运行时代码行为。
- `python -m unittest tests.test_board_ds2413_motor_source tests.test_nonblocking_boot_source tests.test_power_integration_source`：27 passed。已同步两处漂移断言：天气任务栈当前为 `6144`，`power_policy_task` 当前使用 `xTaskCreateWithCaps(...)`。
- `idf.py build` 通过；构建中确认 `ds2413` 与 `espressif__onewire_bus` 进入组件图；删除 UART1 fallback 后生成 `build/111.bin`，大小 `0xac7aa0`，最小 app 分区剩余 `0x338560`/23%。
- 2026-07-04 闭环板测：发现 `sdkconfig` 漂移为 `CONFIG_RUNTIME_RESOURCE_GATE_BOARD_TEST=y`，导致正常启动后自动运行 runtime gate 板端压测；已恢复为 `# CONFIG_RUNTIME_RESOURCE_GATE_BOARD_TEST is not set`。按仓库规则执行 `idf.py fullclean; idf.py build`，通过后生成 `111.bin` `0xac7130`，app free `0x338ed0`/23%。
- COM7 正常固件 `app-flash` 成功，随后 pyserial 复位采集 `board_logs/2026-07-04-20-47-24-ds2413-normal-com7-pyserial.log`。日志出现 `DS2413 ROM via RMT: BA DC CD 73 50 05 10 46`、`DS2413 motor default off: raw=0x78 PIOA(state=0 latch=0)`、`boot_stage: startup_sequence_done`、`boot_stage: ui_first_frame_ready`、`boot_stage: cold_boot_resource_snapshot_done`，未出现 `runtime_gate_test`、Guru、panic、watchdog、`ESP_ERR_NO_MEM`。

## 未验证

- 未验证马达脉冲/震动策略；本轮只证明启动早期 DS2413 枚举、默认关断和正常启动链路。

## 后续边界

- 若要把 hearing-assist 产品提醒推进到震动优先，应另开任务接入 `app_alert_manager`，并结合 `power_policy.haptic_alert_allowed` 设计 P0/持续提醒节奏。
- 当前提交只让硬件通路可用并在启动早期安全关断，不改变现有告警输出策略。
