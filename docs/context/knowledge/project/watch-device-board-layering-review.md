---
id: watch-device-board-layering-review
tags: project, architecture, layering, board, device, driver-adapter, bsp, review
summary: 记录 2026-07-07 对当前 ESP32-S3 手表固件设备层与板层的审查结论：AXP2101/board_power/power_service 三段较清楚，QMI8658C/board_imu/imu_service 存在文档链路与实现链路错位，DS2413 马达板层偏厚但当前可接受，i2c_manager 与部分 public header 暴露 SDK/raw handle 需要后续收窄或 allowlist。
last_reviewed: 2026-07-07
memory_type: project_knowledge
scope: repo
owners: docs/context/knowledge/project/watch-device-board-layering-review.md
triggers: 设备层, 板层, board layer, device layer, BSP, driver adapter, board_imu, qmi8658c, board_power, axp2101, ds2413, i2c_manager
evidence_level: review
status: active
---

# Watch Device / Board Layering Review

## 一句话结论

当前设备层和板层没有 P0 级结构错误，主线可继续使用；但有几处边界不干净：

- IMU 文档推荐链路是 `imu_service -> board_imu -> qmi8658c`，当前实现实际是 `imu_service -> board_imu + qmi8658c`。
- QMI8658C driver 仍保留默认地址初始化路径，当前板可用，但多板型时会和 `board_imu` 的板级地址 owner 冲突。
- AXP2101 / `board_power` / `power_service` 三段最接近当前理想分层，但 `axp2101_read_snapshot()` 内部会启用 ADC channel，不是绝对纯读 getter。
- `board_ds2413_motor` 是“板级设备 owner”，不是纯 board facts；当前可接受，但震动策略不能继续塞进这里。
- `i2c_manager` 和部分 public header 暴露 SDK/raw handle，短期可用，长期需要 source test 或 allowlist 管住。

## 当前推荐边界

设备层负责芯片协议、寄存器、总线事务和设备原始事实：

```text
components/qmi8658c
components/axp2101
components/ds2413
components/co5300_panel
components/touch_ft5x06
```

板层负责当前这块板的接线、地址、GPIO、安装方向和板级语义转换：

```text
main/app/board_imu.*
main/app/board_power.*
main/app/board_ds2413_motor.*
main/app/board_button.*
```

服务层负责运行 profile、轮询、重试、状态机、snapshot 和产品策略：

```text
main/services/imu_service.*
main/services/power_service.*
features/alerts/haptic_alert_player.*
```

## 当前做得好的地方

### AXP2101 电源链路分层清楚

当前电源链路接近标准三段：

```text
components/axp2101
  -> 设备生命周期、I2C 访问、寄存器读写、状态快照、IRQ 读清

main/app/board_power.*
  -> 将 PMIC 原始快照转换成当前板级电源语义

main/services/power_service.*
  -> 周期刷新、去抖、状态发布、UI/策略接缝
```

`board_power` 只把 `axp2101_snapshot_t` 转成 `board_power_state_t`，不创建后台任务，不直接做低电量策略，不写电源轨。这个方向是合理的。

### QMI8658C public API 已经收敛到物理量

`qmi8658c` public header 不再向上暴露 raw sample、WoM public type 或 raw-to-physical helper。对上层输出的是：

- `qmi8658c_info_t`
- `qmi8658c_config_t`
- `qmi8658c_sample_t`
- `qmi8658c_read()`

这让上层不用重复维护量程表，是设备层应该承担的职责。

### `board_imu` 自身很干净

`board_imu` 当前只保存：

- QMI8658C 7-bit I2C 地址。
- QMI_INT1 对应 GPIO。
- IMU 在板上的安装方向。

它没有保存 WoM 阈值、窗口长度、姿态阈值或抬腕策略，这符合 board facts 边界。

### DS2413 设备层保持了最小协议面

`components/ds2413` 只封装：

- 1-Wire ROM 枚举。
- PIO 状态读取。
- PIO latch 写入与回读校验。

马达 GPIO、PIOA/PIOB 连接语义和默认关闭态没有放进 `ds2413` 设备层。

## 主要问题

### P1: `imu_service` 直接依赖 `qmi8658c`

文档推荐链路是：

```text
imu_service -> board_imu -> qmi8658c -> shared I2C
```

当前实现实际是：

```text
imu_service -> board_imu
imu_service -> qmi8658c
```

`imu_service` 会读取 `board_imu_get_config()`，但随后自己构造 `qmi8658c_bus_t`，并直接调用：

- `qmi8658c_init_bus()`
- `qmi8658c_probe()`
- `qmi8658c_config()`

风险：

- service 层知道了设备初始化和 probe/config 细节。
- 后续换板、换 IMU 或做多板型时，service 会跟着改。
- `board_imu` 只剩 facts holder，没有成为 board/device 接缝。

建议：

- 短期保留现状，不为了形式立刻大改。
- 下一轮 IMU 整改时，把 bus/probe/config 接缝收敛到 `board_imu` 或窄 board adapter。
- `imu_service` 只保留运行 profile、状态机、retry、snapshot。

### P1: QMI8658C driver 仍保留默认板级地址路径

`qmi8658c_init()` 默认使用 `QMI8658C_I2C_ADDR_7BIT`，`qmi8658c_probe()` 会先调用默认 init。

当前板地址正好是 `0x6B`，所以不会暴露问题。但长期看，I2C 地址是板级 SA0 绑法事实，应由 `board_imu` 明确传入。

风险：

- 多板型时 driver 默认地址会和 board facts owner 冲突。
- 调用方如果绕过 `qmi8658c_init_bus()`，会把当前板事实偷偷带入设备层。

建议：

- 后续弱化或 internalize `qmi8658c_init()` 默认地址路径。
- 新代码优先从 `board_imu` 提供的地址进入。
- source test 锁住 `hardware_init` 和普通 service 不直接调用默认 `qmi8658c_init()`。

### P1: `axp2101_read_snapshot()` 有受控写寄存器副作用

`axp2101_read_snapshot()` 内部会调用 `axp2101_ensure_voltage_adc_channels()`，必要时写 `AXP2101_REG_ADC_CHANNEL_CTRL` 以启用电压 ADC channel。

这不等于错误，因为读电压前启用 ADC channel 是设备层可以承担的准备动作。但它意味着：

- `axp2101_read_snapshot()` 不是绝对纯读 getter。
- 不能把它和 service/UI 高频 snapshot getter 混为一类。
- 后续新增 rail/charging/sleep 写接口时，必须和 ADC enable 这种受控写区分开。

建议：

- 文档和 source test 明确：当前 `read_snapshot()` 只允许受控启用 ADC channel，不允许写 `REG80+` rail、充电参数、sleep/wakeup 控制。
- 若后续做电源轨控制，新增显式命名 API，例如 `axp2101_set_rail_enabled()`，并只由 `board_power` 或更窄 rail owner 调用。

### P2: `board_ds2413_motor` 是板级设备 owner，板层偏厚

`board_ds2413_motor` 同时知道：

- GPIO18。
- 外部 R22 4.7k 上拉。
- DS2413 ROM 枚举。
- RMT 1-Wire bus 创建。
- PIOA 控制 Q1 / 马达，PIOB 未使用。
- 默认关闭态和 mutex 保护。

它严格说不是纯 board facts，而是“当前板 DS2413 马达通路 owner”。在当前只有一个板子、一个 DS2413 马达时可接受。

风险：

- 如果继续加入震动节奏、告警策略、UI 状态，就会变成产品策略 owner。

建议：

- 保持 `board_ds2413_motor` 只负责安全初始化、开/关、短 pulse primitive。
- 震动节奏继续放在 `features/alerts/haptic_alert_player` 或提醒 owner。
- 不在 `board_ds2413_motor` 中加入危险提醒、通知、Hermes 或 UI 文案。

### P2: `i2c_manager` public header 暴露 SDK/raw handle 和板级常量

`i2c_manager.h` 当前暴露：

- `I2C_MANAGER_PORT`
- `I2C_MANAGER_SCL_GPIO`
- `I2C_MANAGER_SDA_GPIO`
- `I2C_MANAGER_FREQ_HZ`
- `i2c_master_bus_handle_t`

当前 components 之间协作需要这个公共面，例如 `qmi8658c`、`axp2101`、`touch_ft5x06`、`audio_codec` 会使用共享 bus handle。

风险：

- 上层 UI/service 也能轻易拿到底层 bus handle。
- 共享 I2C 板级事实和 SDK raw handle 被放在同一个 public header。

建议：

- 短期加 source test，禁止 `main/ui`、`main/services` 非例外路径 include `i2c_manager.h` 或调用 `i2c_manager_get_bus_handle()`。
- 中期考虑把 board constants 与 raw handle API 拆开，或通过 allowlist 明确哪些 device adapter 可用。

### P2: display/touch public header 暴露 raw SDK 类型

与设备/板层相关的公共面风险还包括：

- `co5300_panel.h` 暴露 raw LCD panel/io handle 和 `esp_lcd_panel_io_callbacks_t`。
- `touch_ft5x06` 直接使用 shared I2C bus handle。
- `ui_refresh_policy` 当前直接调用 `co5300_panel_set_brightness_percent()`，这是已知过渡例外。

建议：

- 短期记录并 source test 可见，不急着大拆。
- 后续通过 display runtime owner 收敛亮度控制。
- `lvgl_port` 和 panel driver 之间的 raw handle 协作可以保留，但不应扩散到 UI/service。

## 建议整改顺序

1. 先不做大拆，补检查。
   - source test 禁止 UI/service 直接 include/call 设备层和 raw bus handle。
   - 对 `ui_refresh_policy -> co5300_panel`、`wakeup_evidence_service -> axp2101 IRQ` 这类过渡路径登记例外。

2. 收敛 IMU 接缝。
   - 增加 `board_imu_probe_and_configure(...)` 或类似窄 API。
   - `imu_service` 传入运行 profile，拿回 identity/configured 结果。
   - `qmi8658c_init_bus/probe/config` 细节留在 board adapter 内。

3. 明确 AXP2101 写权限。
   - 把 ADC channel enable 记录为受控设备准备动作。
   - 保持 rail/charging/sleep 写接口不可用，直到 rail map 与 owner 证据完成。

4. 管住共享总线公共面。
   - `i2c_manager_get_bus_handle()` 只允许 device adapter/component 使用。
   - board/service/UI 不直接拿 raw bus handle。

5. 保持 DS2413 马达边界。
   - board 层只做通路和 primitive。
   - haptic/alert owner 做节奏、重试和产品策略。

## 不建议做的事

- 不把 `board_imu` 并入 `qmi8658c` driver。
- 不把 `qmi8658c` driver 改成知道 GPIO21、安装方向或抬腕策略。
- 不把 `board_power` 变成大 `PowerManager`。
- 不让 `axp2101` 设备层直接理解 UI、电池图标、低电量策略或 sleep policy。
- 不把马达震动节奏放进 `board_ds2413_motor`。
- 不为了“纯洁分层”立刻拆掉 `main` 或重写所有 public header。

## 后续验证口径

设备层 / 板层整改时，至少做这些静态验证：

- `main/services/imu_service.c` 不再直接 include `qmi8658c.h`。
- `main/ui` 不直接 include `qmi8658c.h`、`axp2101.h`、`ds2413.h`、`i2c_manager.h`、`co5300_panel.h`。
- `main/services` 非例外路径不直接调用设备层 raw API。
- `components/*` 不反向 include `main/app`、`main/services`、`main/ui`。
- `axp2101` 写接口必须有明确 allowlist，当前只允许 ADC channel enable 和 IRQ clear 这类受控动作。

