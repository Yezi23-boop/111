---
id: axp2101-power-component-implementation
tags: project, axp2101, power, pmic, implementation
summary: AXP2101 第一阶段只读电源组件的实际落地文件、状态语义、轮询策略和主链路接入位置。
last_reviewed: 2026-04-13
memory_type: semantic
scope: component
owners: components/axp2101, main/app/board_power.c, main/services/power_service.c
triggers: axp2101, board_power, power_service, owner, snapshot
evidence_level: observed
---

# AXP2101 电源组件实现落点

## 已落地文件

- `components/axp2101/CMakeLists.txt`
- `components/axp2101/include/axp2101.h`
- `components/axp2101/axp2101.c`
- `components/axp2101/axp2101_regs.h`
- `main/app/board_power.h`
- `main/app/board_power.c`
- `main/services/power_service.h`
- `main/services/power_service.c`

## 第一阶段行为边界

- 仍以“只读观测”为主，不开放电源轨控制、充电参数配置、睡眠/唤醒控制。
- 驱动层唯一主动写寄存器是 `REG30` 的 ADC 通道使能，用于首次读取前打开 `battery/vbus/vsys` 电压采样位。
- `probe()` 不写 PMIC；`REG30` 的写入延后到首次 `read_snapshot()` 时做受控的 read-modify-write。
- 共享总线仍复用 `i2c_manager`，没有单独起新的 I2C 生命周期。

## 驱动层语义

- `axp2101_snapshot_t` 当前输出：
  - `vbus_good`
  - `battery_present`
  - `battfet_on`
  - `charging`
  - `discharging`
  - `battery_mv`
  - `vbus_mv`
  - `vsys_mv`
  - `battery_percent`
- `battery_present` 取自 `REG00 bit3`。
- `battfet_on` 取自 `REG00 bit4`。
- 充放电方向从 `REG01 >> 5` 解码。
- 当无电池时，驱动返回 `battery_mv = 0`、`battery_percent = -1`；当 `VBUS` 不存在时，返回 `vbus_mv = 0`。
- IRQ 读清仍采用按 bank 分次读写的 RW1C 方式，当前不保证跨 bank 原子清除。

## 板级状态语义

- `board_power_init()` 只负责探测 `AXP2101`，失败默认非致命。
- `board_power_refresh()` 成功时刷新缓存；失败时分两种语义：
  - 若已有历史有效快照，则返回一份 `snapshot_stale=true`、`available=false` 的临时视图，但不污染缓存。
  - 若仍处于未采样阶段，则返回未采样默认态，不强行标记为 stale。
- `battery_percent` 只有在 `battery_data_valid=true` 时才可信；否则固定为 `UINT8_MAX`，避免把“未知”误判成“0%”。

## 服务层语义

- `power_service` 默认 `1s` 轮询。
- 连续失败后退避到 `2s / 5s`，并对失败日志做 `5s` 节流。
- 状态变化日志对 `battery_mv/system_mv` 采用 `20mV` 抖动阈值：
  - 小于 `20mV` 的 ADC 细小波动不算状态变化
  - 语义字段如 `external_power_present/battery_present/charging/discharging/snapshot_stale/battery_percent` 仍按精确变化触发日志
- 发布层使用双缓冲切换：
  - 先写非活动缓冲
  - 再切换活动索引
  - `power_service_get_state()` 与回调参数都只代表服务层拥有的只读快照视图
- 若调用方需要跨任务或跨时段持有状态，必须自行复制，不应长期保存内部指针。

## 主链路接入位置

- `main/app/hardware_init.c`
  - 在 `audio_codec_init()` 之后调用 `board_power_init()`
  - 在 `board_power_init()` 成功后立即执行一次 `board_power_refresh()` 并打印启动首帧电源快照
  - 初始化失败仅记日志，不阻塞后续启动
- `main/app/app_main.c`
  - 在 `lvgl_task` 创建后启动 `power_service`
  - 仍保持“基础硬件先 ready，后台网络继续”的现有主流程
- `main/CMakeLists.txt`
  - 主组件已注册 `app/board_power.c`
  - 主组件已注册 `services/power_service.c`
  - `REQUIRES` 已补 `axp2101`

## 当前已验证结论

- `uv run python -m unittest tests.test_axp2101_power_source tests.test_board_power_source tests.test_power_service_source tests.test_power_integration_source -v` 通过。
- 在 `ESP-IDF 5.5.3` 环境下执行 `idf.py build` 通过。
- 监控日志可观测性已补强：
  - 启动阶段会打印一条 `Board power boot snapshot`
  - `power_service` 仅在电源状态真实变化时打印 `power state changed`
  - `battery_mv/system_mv` 小于 `20mV` 的抖动不会触发日志
  - 成功路径不会按 1 秒轮询频率刷屏
- 这一阶段尚未做真机 USB 插拔、IRQ、RTC 联动验证，因此“共享 I2C 不回归”和“AXP 状态随外部电源变化”仍需板端复验。
