---
id: axp2101-rail-control-evidence-plan-20260707
tags: plan, active, axp2101, pmic, power-rail, low-power, runtime-save, board-power
summary: 梳理 AXP2101 电源轨控制外设断电的安全推进路线：先只读输出状态与板级映射，再选择低风险外设做显式 opt-in 断电实验。
status: active
last_reviewed: 2026-07-07
memory_type: project_plan
scope: repo
owners: components/axp2101, main/app/board_power.c, main/services/power_policy.c, main/services/sleep_coordinator.c
triggers: AXP2101, AXP, PMIC, 电源轨, 外设断电, LDO, DCDC, LIGHT_ALLOWED, runtime save
evidence_level: design
depends_on: docs/context/knowledge/project/axp2101-power-component-design.md, docs/context/knowledge/esp32-s3/axp2101-deep-dive.md, docs/context/knowledge/project/power-wakeup-control-map.md, docs/context/plans/active/2026-07-07-light-allowed-runtime-save-plan.md
---

# AXP2101 外设断电证据计划

## 目标

用户想确认：当前板上的 `AXP2101` 能不能控制其他设备断电，以及后续应该怎么安全做。

结论先收敛为一句话：

```text
硬件上 AXP2101 有多路 DCDC/LDO，可以控制部分电源轨；
但当前项目软件只开放只读观测，不应直接写电源轨开关。
```

本计划的目标不是立刻省多少电，而是先把“哪一路电源供给哪个设备”确认清楚，避免误关主控、RTC、Flash、PSRAM、共享 I2C 或唤醒链路。

## 当前软件现状

当前 `components/axp2101` 已经提供：

- `axp2101_init()`
- `axp2101_probe()`
- `axp2101_read_snapshot()`
- `axp2101_read_irq_status()`
- `axp2101_clear_irq_status()`

当前未提供：

- 不提供 DCDC/LDO enable/disable API。
- 不提供电压设置 API。
- 不提供 sleep / poweroff / wakeup 寄存器写入 API。
- 不提供按外设名断电的板级 API。

这不是缺能力，而是安全边界：`AXP2101` 是整机 PMIC，错误写 `REG80+` 可能直接影响主控、显示、音频、RTC 或保活域。

## 已知电源轨事实

根据当前上下文库，板上已知输出命名如下：

```text
DCDC1  -> VCC3V3
DCDC2  -> 0.9V
DCDC3  -> 1.2V
DCDC4  -> 1.8V
DCDC5  -> NC
ALDO1  -> A3V3
ALDO2  -> VL2_3.3V
ALDO3  -> VCC3V
ALDO4  -> VL1_1.8V
BLDO1  -> VL1_2V
CPUSLDO -> VCL_1.2V
RTCLDO -> VCC_RTC
VBACKUP -> VBAT2
```

AXP2101 输出控制相关寄存器组：

```text
REG80   DCDC1~5 使能
REG81   DCDC PWM/PFM 模式
REG82+  DCDC/LDO 电压设置
REG80~REG91 需要按 datasheet 输出表逐项确认
```

当前 `axp2101_regs.h` 没有定义 `REG80+` 电源轨控制寄存器，这符合现阶段“只读观测”的设计。

## 电源轨风险分类

### 禁止直接断电

```text
DCDC1 / VCC3V3
DCDC2 / 0.9V
DCDC3 / 1.2V
DCDC4 / 1.8V
CPUSLDO / VCL_1.2V
RTCLDO / VCC_RTC
VBACKUP / VBAT2
PWROK / CHIP_PU 链路
```

原因：

- `VCC3V3` 可能是系统 3.3V 主域，关掉会影响大量外设甚至主控外围。
- `0.9V / 1.2V / 1.8V / CPUSLDO` 可能涉及核心、存储、显示或内部时序域，不能只凭名字判断。
- `RTCLDO / VBACKUP` 关系到 RTC/保活，不应为了运行态省电关闭。
- `PWROK -> CHIP_PU` 是主控 enable/reset 链路，不能当普通 GPIO 或普通电源控制处理。

### 待确认候选

```text
ALDO1 / A3V3
ALDO2 / VL2_3.3V
ALDO3 / VCC3V
ALDO4 / VL1_1.8V
BLDO1 / VL1_2V
DCDC5 / NC
```

这些名字看起来更可能对应外设电源域，但必须继续确认：

- 这一路实际接到了哪些芯片。
- 这一路是否给共享 I2C 上拉供电。
- 这一路是否影响触摸唤醒、屏幕恢复、音频 codec、传感器或 SD 卡。
- 这一路关掉后，外设重新上电是否需要完整 init/deinit 顺序。

### 第一批更适合实验的外设类型

后续如果原理图证明有独立电源轨，优先考虑：

```text
非关键传感器
音频 codec / 麦克风链路
SD 卡
显示面板外围电源
```

不建议第一批实验：

```text
主控供电
Flash / PSRAM
RTC / PCF85063ATL
共享 I2C 上拉供电
触摸唤醒必需链路
危险提醒必需链路
```

## Owner 边界

### `components/axp2101`

负责芯片级寄存器读写封装。

第一步只新增只读能力：

- 读取输出轨 enable 状态。
- 读取关键电源轨相关原始寄存器。
- 打印或返回原始状态，不做策略判断。

暂不负责：

- 不按“屏幕/音频/SD”这类产品名断电。
- 不直接判断 `LIGHT_ALLOWED`。
- 不直接调用外设 deinit/init。

### `main/app/board_power.c`

负责把芯片输出轨翻译成板级语义。

后续适合新增：

- `board_power_read_rail_snapshot()`
- `board_power_get_rail_device_map()`
- `board_power_set_optional_rail()`，但必须只允许白名单候选 rail。

暂不负责：

- 不计算低功耗预算。
- 不决定什么时候进入 `STANDBY` 或 `LIGHT_ALLOWED`。

### `power_policy`

负责发布预算和 blocker。

后续适合表达：

- 当前是否允许进入“外设可断电候选窗口”。
- 当前是否有音频、网络、危险提醒、SD 写入等 blocker。

暂不负责：

- 不直接写 AXP2101 寄存器。
- 不直接关屏幕、音频、SD 或传感器。

### 外设 owner

每个外设 owner 负责自己的安全停用与恢复。

例如：

- 音频链路由 `audio_codec` / 音频 session owner 处理。
- SD 文件系统由 `sd_manager` 处理。
- UI 屏幕刷新由 `ui_refresh_policy` / 显示 owner 处理。
- IMU 由 `imu_service` / `board_imu` / `qmi8658c` 分层处理。

PMIC 只能负责“供电开关”，不能替外设 owner 猜测“现在能不能断”。

## 最小实现路线

### Phase 0：不写寄存器，只补证据

- 从 datasheet 确认 `REG80~REG91` 中每一路 enable/status/voltage 位定义。
- 在文档中建立 `rail -> net name -> device -> owner -> can_off` 表。
- 明确每一路关断后的恢复顺序。

验收：

- 每个候选 rail 都能说清“它供给谁”。
- 每个禁止 rail 都有明确禁止原因。

### Phase 1：只读输出轨 dump

在 `components/axp2101` 增加只读 API，例如：

```c
esp_err_t axp2101_read_output_status(axp2101_output_status_t *status);
```

只读取：

- DCDC/LDO enable 状态。
- 必要的输出配置原始寄存器。

不写：

- 不 enable/disable rail。
- 不改电压。
- 不改 PFM/PWM。
- 不改 sleep/wakeup/poweroff。

验收：

- 启动日志能看到当前 rail 原始状态。
- 多次读取不会改变系统行为。
- 屏幕、触摸、音频、Wi-Fi、RTC 正常。

### Phase 2：板级白名单

在 `board_power` 建立明确白名单：

```text
rail_id
net_name
candidate_device
owner
default_allowed_to_disable = false
```

第一版所有候选默认仍是 `false`，只作为观测和日志。

验收：

- 上层不能绕过白名单直接写 rail。
- `power_policy` 只能看预算，不知道寄存器位。

### Phase 3：单一路 opt-in 实验

选择一个低风险、可恢复、非关键的外设 rail。

实验必须满足：

- 编译期开关默认关闭。
- 运行期开关默认关闭。
- 只在串口明确命令或测试入口触发。
- 断电前调用外设 owner 的 suspend/deinit。
- 上电后调用外设 owner 的 resume/init。
- 失败时能恢复到默认上电状态。

验收：

- 断电前后电流变化可观测。
- 断电前后串口无 Guru、watchdog、I2C bus hang。
- 外设能恢复工作。
- 重启后默认状态不受影响。

## 与 `LIGHT_ALLOWED` 的关系

当前 `LIGHT_ALLOWED` 第一版只做运行态省电，不进入 ESP sleep，也不关闭 PMIC 主电源轨。

未来接入外设断电时，应按下面方向：

```text
power_policy 发布：可以进入深省电候选
sleep_coordinator dry-run/调度：记录证据，不直接关电
外设 owner：先 suspend 自己
board_power：只对白名单 rail 执行 PMIC 输出控制
外设 owner：唤醒后 resume 自己
```

不要做成：

```text
power_policy 直接写 AXP2101 REG80
sleep_coordinator 直接关所有设备
UI timer 里顺手关 PMIC
AXP driver 按产品名理解外设
```

## 风险与兜底

- 若误关系统主电源轨，可能直接复位或黑屏。
- 若误关共享 I2C 上拉电源，可能导致总线挂死，连恢复命令都发不出去。
- 若误关 RTC/保活电源，可能破坏时间保持和唤醒证据。
- 若未先暂停 SD 写入就断 SD 电源，可能损坏文件系统。
- 若未先停音频/I2S 就断 codec 电源，可能产生 I2C/I2S 错误或 pop noise。

兜底要求：

- 所有断电实验默认关闭。
- 每次只实验一路 rail。
- 每路 rail 都必须有恢复路径和人工复位路径。
- 串口/JTAG 观测不是唯一证据，后续最好配合电流表或 USB 功耗仪。

## 进度

- `[x]` 已确认当前 AXP2101 软件层仍是只读观测为主。
- `[x]` 已确认当前不应直接写 `REG80+` 输出轨控制。
- `[x]` 已按现有上下文梳理 rail 风险分类。
- `[ ]` 待根据 datasheet 补齐 `REG80~REG91` 的精确 bit 表。
- `[ ]` 待补 `rail -> device -> owner -> can_off` 板级映射表。
- `[ ]` 待实现只读输出轨 dump。
- `[ ]` 待选择第一路低风险 opt-in 断电实验对象。

## 下一步建议

下一步不要直接写“关闭某个 LDO”的函数。

建议先做一个只读闭环：

```text
新增 axp2101_read_output_status()
启动时打印 DCDC/LDO enable 原始状态
补 board_power rail map 文档表
确认哪一路真的只供给非关键外设
```

等这一步通过后，再讨论第一路真正可断电的外设。
