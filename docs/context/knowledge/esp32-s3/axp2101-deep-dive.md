---
id: axp2101-deep-dive
tags: esp32-s3, axp2101, pmic, battery, charger, irq, power
summary: AXP2101 在当前 ESP32-S3 手表板上的功能块、关键寄存器组、状态机与接入边界梳理。
last_reviewed: 2026-04-09
memory_type: semantic
scope: board
owners: components/axp2101, main/app/board_power.c
triggers: axp2101, deep, dive
evidence_level: observed
---

# AXP2101 深度梳理

## 器件定位

- `AXP2101` 是单节锂电场景的 `NVDC PMIC`，核心不是“单纯电量计”，而是把：
  - `VBUS` 输入管理
  - 单节锂电充电
  - `BATFET` 电池/系统电源路径切换
  - 多路 `DCDC/LDO`
  - `Fuel Gauge`
  - `ADC`
  - `PWRON/PWROK/IRQ`
  - sleep / wakeup / watchdog
  集成在一颗芯片里。
- 当前仓库这块 `ESP32-S3-Touch-AMOLED-2.06` 板上，`AXP2101` 更接近“整机电源中枢”，不是孤立外设。

## 当前板上的已知事实

- 原理图和上下文已确认板上存在 `AXP2101`。
- 已能确认相关控制网络：
  - `PWRON`
  - `PWROK`
  - `AXP_IRQ`
  - `VBUS`
  - `VBAT1/2`
- 当前共享控制总线是 `GPIO14/15` 对应的 `I2C_NUM_0`，已被：
  - `ES8311`
  - `ES7210`
  - `FT5x06/FT3168`
  复用。
- 当前仓库尚未接入 AXP2101 驱动或板级电源策略模块。
- 板子在“未写 PMIC 控制寄存器”的情况下已经能点屏、起 UI、起音频，因此第一阶段默认应视为：
  - 板级默认电源轨配置已经可用
  - 软件先做只读观测
  - 不要贸然重配输出轨

## 本页原理图可直接确认的板级连线

- `SDA/SCK` 直接接到 `ESP32_SDA / ESP32_SCL`，说明 `AXP2101` 确实挂在当前共享 `I2C` 总线上。
- `IRQ` 引脚通过 `RP5 10k` 上拉到 `VCC_RTC`，并接到 `AXP_IRQ` 网络，原理图气泡标成 `EXIO5`。
- `PWROK` 没有单独接到一个普通 GPIO，而是直接接到 `CHIP_PU`。
- `PWRON` 接到独立 `PWRON` 网络，并在该脚对地并了 `1uF` 电容，明显是按键/上电时序用途，而不是普通状态输出。
- `BAT` 接 `VBAT1`，`TS` 外挂 `10k` 热敏电阻，`CHGLED` 单独引出。
- `RTCLDO` 输出到 `VCC_RTC`，`VBACKUP` 接到 `VBAT2`。
- 本页能直接看清的一路主输出是：
  - `DCDC1 -> VCC3V3`
- 左侧电源分配表还列出了当前板上使用的其余输出命名：
  - `DCDC2 -> 0.9V`
  - `DCDC3 -> 1.2V`
  - `DCDC4 -> 1.8V`
  - `DCDC5 -> NC`
  - `ALDO1 -> A3V3`
  - `ALDO2 -> VL2_3.3V`
  - `ALDO3 -> VCC3V`
  - `ALDO4 -> VL1_1.8V`
  - `BLDO1 -> VL1_2V`
  - `CPUSLDO -> VCL_1.2V`

## 这些连线对软件语义的直接影响

- `AXP_IRQ` 不是“待确认是否存在”的抽象网络了，而是已经能确认：
  - 开漏输出
  - 外部 `10k` 上拉
  - 拉到 `VCC_RTC`
  - 接向 `EXIO5`
- 因为 `PWROK -> CHIP_PU`，所以 `PWROK` 对当前主控更像“硬件级 enable/reset 链路”，而不是一个适合在应用层轮询的普通状态脚。
- `PWRON` 更像 PMIC 自己管理的电源键网络，不应再默认假设它会直接复用为 `GPIO10` 那种普通按键输入。
- `RTCLDO -> VCC_RTC` 与 `VBACKUP -> VBAT2` 说明：
  - AXP2101 不只管主系统供电
  - 还参与了 RTC/保活电源域
  - 后续做 `PCF85063ATL` 保时、低功耗和唤醒时，必须把 `VCC_RTC/VBAT2` 一起建模
- `KEYS` 页还补充了一个很关键的板级事实：
  - 物理 `PWR` 键并不是直接进 MCU
  - 它一边拉 `PWRON`
  - 一边通过 `BSS138 + R11/R16` 生成 `SYS_OUT`
  - `SYS_OUT` 再接到 `GPIO10`
- 结合 datasheet 中 `PWRON` 内部上拉语义，可以推断：
  - 未按下时 `GPIO10/SYS_OUT` 为低
  - 按下电源键时 `GPIO10/SYS_OUT` 为高
  - 这与当前软件里 `GPIO10` 按键 `active_level = 1` 的配置一致

## 通信与地址

- `AXP2101` 支持 `TWSI/I2C` 从设备模式。
- datasheet 给的是 `0x68/0x69` 写/读地址，这是 `8` 位地址。
- 换算成 `7` 位地址后默认是 `0x34`。
- 因此当前板上最小探测时，`i2c_manager_scan()` 预期应在已有 `0x18 / 0x38 / 0x40` 之外再看到 `0x34`。

## 供电路径视角

### 1. `VBUS` 与 `BAT` 并不是简单二选一

- `AXP2101` 上电来源是 `VBUS` 和 `BAT` 中更高的那个。
- 当 `VBUS` 有效时，系统可由 `VBUS` 经线性充电路径供电，同时给电池充电。
- 当电池电压高于 `VSYS` 时，`BATFET` 打开进入补电模式。
- 当没有适配器时，系统电流由电池单独承担。

### 2. 这对当前仓库意味着什么

- 以后做“插 USB 显示充电”“拔 USB 进入电池供电”“低电量关机”“待机唤醒”时，核心观测点不是单个 GPIO，而是：
  - `VBUS good`
  - `Battery present`
  - `Battery current direction`
  - `Charging status`
  - `BATFET state`

## 功能块梳理

### 1. 多路电源输出

- 以 datasheet 的输出表和 `REG80+` 控制组为准，`AXP2101` 实际具备：
  - `5` 路 `DCDC`
  - `11` 路 `LDO`
  - `1` 路 switch / 复用输出
- 其中输出表明确列出：
  - `DCDC1~5`
  - `ALDO1~4`
  - `BLDO1~2`
  - `DLDO1~2`
  - `VCPUS`
  - `RTC-LDO1`
  - `RTC-LDO2`
- datasheet 首页对 `DCDC` 数量的文字描述存在前后不一致，不能只看首页摘要；真正做驱动时应以输出表和 `REG80~REG91` 寄存器组为准。

### 2. 充电器

- `AXP2101` 集成单节锂电线性充电器。
- 充电条件至少要求：
  - `VBUS` 存在且可用
  - 输入源检测完成且 good
  - 充电使能位打开
  - 芯片温度、TS、电池状态满足条件
- 充电过程包含：
  - 预充
  - 恒流
  - 恒压
  - 充满终止
  - 自动再充
- 关键可调参数包括：
  - `REG61` 预充电流
  - `REG62` 恒流充电电流
  - `REG63` 终止电流与终止控制
  - `REG64` 充电目标电压
  - `REG65` 热调节阈值
  - `REG67` 安全定时器

### 3. Fuel Gauge 与 ADC

- `Fuel Gauge` 负责输出：
  - 电池百分比 `REGA4`
  - 电池电压 `REG34/35`
- `ADC` 是 `14` 位，读数必须先读高 `6` 位再读低 `8` 位，不能乱序。
- `REG30` 控制 ADC 通道开关，其中默认已打开：
  - `TS`
  - `VBAT`
- 其他如 `VBUS`、`VSYS`、`TDIE` 默认并不都开启，后续若要读更多实时状态，需要明确打开对应通道。

### 4. 电源键、复位、睡眠、唤醒

- `PWRON` 引脚是电源键输入，内部有 `100k` 上拉到 `VINT`。
- `PWROK` 是 power-good 指示输出，可作为系统复位/时序信号的一部分。
- `IRQ` 是开漏输出，需要外部上拉。
- `REG25` 控制 `PWROK` 延时和 `poweroff` 时序。
- `REG26` 控制 sleep / wakeup：
  - 允许 `IRQ low` 触发 wakeup
  - 允许 wakeup 时使用默认电压或睡前电压
- `REG27` 定义：
  - `IRQLEVEL`
  - `OFFLEVEL`
  - `ONLEVEL`
- `REG28~2B` 定义 fast power-on / fast wakeup 各输出轨启动序列。
- 结合当前板级原理图，本板至少还能额外确认：
  - `IRQ` 外部上拉已经存在，不应在 MCU 侧再误配成推挽输出
  - `PWROK` 直接牵到 `CHIP_PU`，其抖动或拉低会直接影响主控运行态
  - `PWRON` 是 PMIC 自己的按键输入语义，软件只能通过 `PKEY short/long` 中断侧观察，不应把它当普通 GPIO 主动驱动

## 最值得先掌握的寄存器组

### 1. 状态寄存器

- `REG00`
  - `VBUS good`
  - `BATFET state`
  - `Battery present`
  - 热调节状态
  - 限流状态
- `REG01`
  - 电池电流方向
  - 系统开关机状态
  - `VINDPM` 状态
  - 充电阶段

### 2. 充电 / gauge / watchdog 总控

- `REG18`
  - `Gauge Module enable`
  - `Button Battery charge enable`
  - `Cell Battery charge enable`
  - `Watchdog Module enable`

### 3. ADC / 电池 / 总线电压

- `REG30`
  - ADC 通道开关
- `REG34/35`
  - 电池电压原始值
- `REG38/39`
  - `VBUS` 电压原始值
- `REG3A/3B`
  - `VSYS` 电压原始值
- `REG3C/3D`
  - 芯片温度原始值

### 4. IRQ 使能与状态

- `REG40~47`
  - IRQ enable
- `REG48`
  - `SOC` warning / shutdown
  - gauge watchdog
  - gauge new soc
  - 电池温度相关 IRQ
- `REG49`
  - `VBUS insert/remove`
  - battery insert/remove
  - `PKEY short/long`
- `REG4A`
  - watchdog expire
  - LDO over-current
  - `BATFET` over-current
  - charge start / done
  - die over-temperature
  - safety timer expire

### 5. 输出轨控制

- `REG80`
  - `DCDC1~5` 使能
  - DVM 斜率
- `REG81`
  - DCDC PWM/PFM 模式
- `REG82~`
  - 各路 DCDC/LDO 电压设置

## 对当前仓库最重要的工程含义

### 1. AXP2101 不是“再加一个 I2C 设备”这么简单

- 当前共享总线由 [i2c_manager.h](/D:/esp32S3/111/components/i2c_manager/include/i2c_manager.h#L15) 固定在 `I2C_NUM_0 / GPIO14 / GPIO15 / 400kHz`。
- `i2c_manager` 在 `ESP-IDF >= 5.3` 下已经用 `master bus handle` 模型初始化共享总线 [i2c_manager.c](/D:/esp32S3/111/components/i2c_manager/i2c_manager.c#L19)。
- 音频控制面已经在用同一个 `bus_handle` [audio_codec.c](/D:/esp32S3/111/components/audio_codec/audio_codec.c#L203)。
- 这意味着：
  - AXP2101 驱动应复用同一总线模型
  - 任何 PMIC 访问异常都可能连带影响触摸和音频控制

### 2. 当前 `hardware_init` 不适合继续堆 PMIC 细节

- [hardware_init.c](/D:/esp32S3/111/main/app/hardware_init.c#L123) 已经承担：
  - `NVS`
  - 音频 SPIFFS
  - `SD`
  - 音频 codec
  - 按键
  - 配网
- 若把 AXP2101 寄存器解析、IRQ 逻辑、板级电源策略直接塞进去，会把：
  - 驱动层
  - 板级策略层
  - 启动流程层
  混成一团。
- 当前仓库更合理的分层仍是：
  - `components/axp2101`
  - `main/app/board_power.[ch]`

### 3. 当前 `GPIO10` 不是 PMIC 电源键语义

- [hardware_init.c](/D:/esp32S3/111/main/app/hardware_init.c#L19) 当前把 `GPIO10` 当成配网按键。
- 现在可以进一步收敛为：
  - `GPIO10` 不是 `PWRON` 原始网络
  - 它接的是 `SYS_OUT`
  - `SYS_OUT` 是物理 `PWRON` 键链路经过晶体管网络变换后的镜像信号
- 以后做“长按关机 / 唤醒”前，必须先把：
  - `GPIO10`
  - `PWRON`
  - `PWROK`
  - `AXP_IRQ`
  分开建模。
- 结合这次原理图页的直接证据，可以进一步收敛为：
  - `PWRON` 是 PMIC 电源键网络
  - `PWROK` 是 `CHIP_PU`
  - `AXP_IRQ` 接 `EXIO5`
  - `GPIO10` 是 `SYS_OUT`，可作为电源键事件的 MCU 侧镜像观测点

## 推荐接入顺序

### 阶段 1：只读观测

- 目标：
  - 探测 `0x34`
  - 读取 `REG00/01`
  - 读取 `REG34/35`
  - 读取 `REGA4`
  - 读取 / 清除 `REG48/49/4A`
- 原则：
  - 不改充电电流
  - 不改输出轨使能
  - 不改 sleep / wakeup
  - 不改关机 / 重启位

### 阶段 2：IRQ / PKEY 语义确认

- 确认硬件上：
  - `AXP_IRQ` 在当前板上对应 `EXIO5`
  - `PWRON` 已被外部实体按键拉低
  - MCU 侧看到的是 `GPIO10/SYS_OUT` 镜像，而不是 `PWRON` 原始脚
  - `PWROK` 已接入 `CHIP_PU` 复位链
- 再决定是否启用：
  - `VBUS insert/remove`
  - charge start/done
  - `PKEY short/long`

### 阶段 3：板级电源策略

- 在确认电源轨归属后，再逐步引入：
  - 电池电量 UI
  - 充电动画
  - 低电量告警
  - 长按关机
  - sleep / wakeup
  - 与 `PCF85063ATL` 配合的 RTC 保活与定时唤醒

## 当前最容易踩的坑

1. 把 `0x68/0x69` 直接当 7 位地址用，导致总线一直扫不到设备。
2. 先写 `REG80+` 输出轨控制，再去排查显示/音频不亮，等于主动把板级默认时序打乱。
3. 只读 `VBAT` 不读 `REG00/01`，导致看到了电压却不知道当前是：
   - 正在充电
   - 电池供电
   - `VBUS` 存在但输入受限
4. 误以为 `GPIO10` 与 `PWRON` 完全无关，或者反过来把两者当成同一根线；正确理解应是：`GPIO10` 看到的是 `SYS_OUT` 镜像。
5. 把 `PWROK` 当成普通 GPIO 状态脚看待，忽略它实际上已经直连 `CHIP_PU`。
6. 忽略 datasheet 内部表述不一致，只凭首页摘要判断输出轨数量或控制范围。

## 适用边界

- 本文用于当前 `ESP32-S3-Touch-AMOLED-2.06` 板上 AXP2101 的结构化理解与接入规划。
- 若后续进入“改电源轨默认值 / 改充电参数 / 改关机唤醒流程”，必须先补：
  - 板级负载归属
  - 输出轨与外设映射
  - 回滚路径
  - 真机 build / flash / monitor 闭环
