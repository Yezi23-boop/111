# AXP2101 电源管理组件设计

## 背景

当前仓库已经完成显示、触摸、音频、配网和 AI 服务的基础接入，但和整机电源、RTC、待机唤醒直接相关的硬件能力仍未落地为可复用的软件组件。

当前已确认的关键事实包括：

- 当前板上存在 `AXP2101`，其在本项目里应视为“整机电源中枢”，不是普通电量计或普通 I2C 小外设。
- `AXP2101`、触摸控制器、音频控制面共用 `GPIO14/15` 的共享 `I2C` 总线。
- `GPIO10` 当前在软件中被当作按键输入，但原理图证据已收敛为 `SYS_OUT` 镜像，不是 `PWRON` 原始网络。
- `AXP_IRQ`、`PWROK`、`PWRON`、`RTC_INT` 与未来低功耗/唤醒链路存在直接关联，其中 `PWROK -> CHIP_PU` 不适合当普通应用 GPIO 对待。
- 当前板子在“未写 PMIC 控制寄存器”的前提下已经能点屏、触摸和起音频，因此第一阶段应优先保持默认电源轨不变，只做观测与建模。

## 目标

- 为当前仓库新增一套可维护、可验证、可回退的 `AXP2101` 电源管理组件设计。
- 让底层 PMIC 状态能够被 UI、日志、后续低电策略和待机策略稳定消费。
- 复用现有共享 `I2C` 和启动链路，不破坏当前显示、触摸、音频和联网路径。
- 为后续 `PCF85063ATL` RTC、低功耗、长按关机和唤醒扩展预留清晰边界。

## 非目标

本设计第一阶段不做：

- 不修改 `REG80+` 输出轨开关或电压。
- 不修改充电电流、充电截止电压、输入限流等充电参数。
- 不直接接入 `sleep/wakeup` 控制寄存器。
- 不在第一版实现自动关机、深睡、RTC 唤醒闭环。
- 不把 `GPIO10`、`PWRON`、`PWROK`、`AXP_IRQ` 混为同一个“电源键”抽象。

## 证据来源

- 当前仓库上下文卡：
  - `docs/context/knowledge/esp32-s3/axp2101-deep-dive.md`
  - `docs/context/knowledge/project/axp2101-integration-staging.md`
  - `docs/context/knowledge/project/power-wakeup-control-map.md`
  - `docs/context/knowledge/esp32-s3/amoled-206-board-hardware-map.md`
  - `docs/context/knowledge/project/startup-init-and-blocking-chain.md`
  - `docs/context/knowledge/project/display-touch-audio-bus-map.md`
  - `docs/context/knowledge/project/i2c-manager-master-bus-migration.md`
- 当前仓库代码：
  - `main/app/app_main.c`
  - `main/app/hardware_init.c`
  - `components/i2c_manager/i2c_manager.c`
  - `components/audio_codec/audio_codec.c`
- 外部参考：
  - `D:\xiaozhiai\xiaozhi-esp32` 中 `Board/PowerManager/PowerSaveTimer` 的层次设计
  - `C:\Users\ye\Desktop\ESP32-S3-Touch-AMOLED-2.06\examples\ESP-IDF-v5.4.2\01_AXP2101`
  - `C:\Users\ye\Desktop\esp32s3手表项目手册\X-power-AXP2101_SWcharge_V1.0.pdf`

## 方案比较

### 方案 A：直接照 `xiaozhi-esp32` 做统一 `PowerManager`

- 优点：上层接口整齐，UI 和策略层接入容易。
- 缺点：`xiaozhi-esp32` 多数实现依赖 `ADC + GPIO` 或强板级逻辑，不适合直接承载当前板子的 `AXP2101`、`AXP_IRQ`、`PWRON/PWROK` 语义。

### 方案 B：纯 `AXP2101` 驱动式

- 优点：贴近硬件，状态准确，便于后续接 IRQ 和 RTC。
- 缺点：若没有板级语义层和策略层，电源状态会重新散落到 UI、日志和业务模块中。

### 方案 C：混合式三层架构

- 底层：`AXP2101` 驱动提供受控的寄存器/I2C/IRQ/快照能力。
- 中层：板级电源语义层把寄存器事实翻译成项目需要的统一状态。
- 上层：策略层只处理周期采样、状态变化、UI 联动和后续待机扩展。

## 结论

采用方案 C。

理由：

- 它同时吸收了 `xiaozhi-esp32` 的“统一接口 + 策略层”优点和当前项目对 `AXP2101` 真实能力的需求。
- 它符合当前仓库“驱动层、板级策略层、启动流程层分离”的方向。
- 它允许第一阶段只读接入，风险最小，且后续具备演进空间。

## 详细设计

### 一、组件分层

#### 1. `components/axp2101`

职责：

- `AXP2101` 设备探测
- 共享 `I2C` 设备挂载与设备句柄维护
- 寄存器读写封装
- 状态寄存器、ADC/gauge 数据读取
- IRQ 状态寄存器读写

不承担：

- UI 语义
- 低电策略
- 休眠策略
- 网络或业务联动

建议文件：

- `components/axp2101/include/axp2101.h`
- `components/axp2101/axp2101.c`
- `components/axp2101/axp2101_regs.h`

#### 2. `main/app/board_power.[ch]`

职责：

- 将 `AXP2101` 原始状态翻译为当前板级语义
- 维护一份“可信状态缓存”
- 提供上层统一查询接口
- 管理第一阶段的低频刷新入口

不承担：

- 寄存器细节
- 直接操作深睡、关机
- 复杂 IRQ 分发

#### 3. `main/services/power_service.[ch]`

职责：

- 周期刷新 `board_power`
- 检测状态变化
- 记录日志
- 未来为 UI、低电告警、待机策略提供统一接缝

第一阶段先只做“状态发布和缓存维护”，不做深睡控制。

### 二、接口设计

#### 1. 底层驱动接口

```c
typedef struct {
    bool vbus_good;
    bool battery_present;
    bool battfet_on;
    bool charging;
    bool discharging;
    uint16_t battery_mv;
    uint16_t vbus_mv;
    uint16_t vsys_mv;
    int8_t battery_percent;
} axp2101_snapshot_t;

typedef struct {
    uint8_t irq0;
    uint8_t irq1;
    uint8_t irq2;
} axp2101_irq_status_t;

esp_err_t axp2101_init(void);
esp_err_t axp2101_probe(bool *present);
esp_err_t axp2101_read_snapshot(axp2101_snapshot_t *snapshot);
esp_err_t axp2101_read_irq_status(axp2101_irq_status_t *status);
esp_err_t axp2101_clear_irq_status(const axp2101_irq_status_t *status);
```

第一阶段刻意不暴露：

- `set_charge_current()`
- `set_charge_voltage()`
- `power_off()`
- `reboot()`
- `enter_sleep()`
- `enable_output_rail()`

#### 2. 板级状态接口

```c
typedef struct {
    bool available;
    bool charging;
    bool discharging;
    bool external_power_present;
    bool battery_present;
    bool low_battery;
    uint16_t battery_mv;
    uint16_t system_mv;
    uint8_t battery_percent;
} board_power_state_t;

esp_err_t board_power_init(void);
esp_err_t board_power_sample_now(board_power_state_t *out_state);
const board_power_state_t *board_power_get_cached_state(void);
```

`board_power_state_t` 是“项目语义层”，故意不直接暴露 `REG00/01/49` 位名。

#### 3. 策略层接口

```c
typedef void (*power_state_changed_cb_t)(const board_power_state_t *state);

esp_err_t power_service_init(void);
esp_err_t power_service_start(void);
void power_service_register_callback(power_state_changed_cb_t cb);
const board_power_state_t *power_service_get_state(void);
```

### 三、初始化时序

#### 推荐时序

1. `app_main()` 进入当前主链路。
2. `hardware_init()` 内先完成：
   - `NVS`
   - `audio_app_init()`
   - `sd_manager_init()`
   - `audio_codec_init()`
3. `board_power_init()` 放在 `audio_codec_init()` 之后执行。
4. `button_init()` 与 `wifi_provision_init()` 保持现状。
5. `app_main()` 里 `lvgl_task` 启动后，再启动 `power_service_start()`。
6. `network_service_start()` 与 `official_chat_service_init()` 继续保持现状。

#### 为什么第一阶段不把 `AXP2101` 初始化前移

- 当前 `audio_codec_init()` 已承担共享 `I2C` 总线初始化。
- 项目已有上下文建议“第一版初始化放在音频编解码器成功起总线之后，先做状态观测”。
- 若后续进入真正的电源轨管理或休眠控制，再评估是否将 `AXP2101` 初始化前移。

### 四、状态流

第一阶段推荐状态流：

1. `board_power_init()` 调 `axp2101_init()` 和一次 `axp2101_probe()`
2. 若设备存在，则读取一次快照并填充缓存
3. `power_service` 以低频轮询刷新缓存
4. UI 或日志仅读取缓存，不直接触碰 PMIC

这样做的核心目的，是避免多个模块各自发起 `I2C` 访问，降低和触摸、音频控制面抢总线的概率。

### 五、第一阶段推荐寄存器范围

优先读取：

- `REG00`
  - `VBUS good`
  - `Battery present`
  - `BATFET state`
- `REG01`
  - 充电阶段
  - 电池电流方向
- `REG34/35`
  - 电池电压
- `REG38/39`
  - `VBUS` 电压
- `REG3A/3B`
  - `VSYS` 电压
- `REGA4`
  - 电池百分比
- `REG48/49/4A`
  - IRQ 状态

第一阶段不推荐写：

- `REG18`
- `REG25/26/27`
- `REG61~68`
- `REG80+`

### 六、GPIO 与硬件语义边界

第一阶段必须坚持以下硬边界：

- `GPIO10` 只允许继续按“当前按键镜像输入”理解，不能等价为 `PWRON`
- `AXP_IRQ` 仅作为后续中断源候选，不在第一阶段纳入复杂中断流程
- `PWROK` 不纳入普通 GPIO 控制或轮询逻辑
- `RTC_INT(GPIO39)` 在 `PCF85063ATL` 未接入前不提前消费

### 七、从 `xiaozhi-esp32` 借鉴什么，不借鉴什么

可借鉴：

- `Board/PowerManager` 统一上层接口思路
- `PowerSaveTimer` 将“策略”和“硬件动作”拆开
- 低频轮询 + 状态缓存 + 状态变化回调的模式

不直接照搬：

- `ADC` 电量阈值表
- 单板硬编码 GPIO 判充电逻辑
- 将电源管理、休眠动作和业务控制写在同一个类里的做法

## 风险分析

### 1. 共享 I2C 并发风险

- 触摸、音频控制、PMIC 共享一条 `I2C`
- 当前 `touch_ft5x06` 仍走 legacy 访问路径
- 若策略层和 UI 随意直读 PMIC，容易产生并发访问风险

控制策略：

- 第一阶段只允许 `board_power` 或 `power_service` 统一采样
- UI 只读缓存，不直访硬件
- 轮询周期保持低频

### 2. 启动链路耦合风险

- 若把 `AXP2101` 过早插进启动前半段，容易让当前稳定的音频/触摸/显示引导链变复杂

控制策略：

- 第一阶段放在 `audio_codec_init()` 之后
- 先做状态观测，不提前重排现有启动顺序

### 3. 硬件语义误用风险

- 将 `GPIO10` 误当 `PWRON`
- 将 `PWROK` 误当普通 GPIO
- 将 `AXP_IRQ` 当作已经验证完备的应用中断源

控制策略：

- 在设计和接口中显式拆分这些概念
- 第一阶段不把它们合并成统一“电源键”模型

## 验证计划

### 编译验证

- 新组件不引入 C++ 依赖
- 新组件复用现有 `i2c_manager`
- `idf.py build` 可通过

### 运行验证

1. `i2c_manager_scan()` 仍能看到已有设备
2. 新增 `0x34`
3. 连续读取快照不影响触摸和音频控制
4. 插拔 USB 时 `charging/external_power_present/vbus` 状态发生合理变化
5. 读取并清除 `IRQ` 状态后不出现异常粘住现象

### 日志验证

建议第一阶段至少打印：

- `vbus_good`
- `battery_present`
- `charging`
- `discharging`
- `battery_mv`
- `battery_percent`

## 回滚策略

若新设计引入不稳定，回滚路径应保持最小：

1. 仅停用 `board_power_init()`
2. 不修改任何 PMIC 控制寄存器
3. 保持当前 `hardware_init()`、`lvgl_task`、`network_service`、`official_chat_service` 主链路不变

这样可以回到当前“无 PMIC 组件，但现有功能仍稳定”的状态。

## 后续演进

第一阶段完成后，再按顺序推进：

1. `AXP_IRQ` 只读/中断来源确认
2. UI 状态栏的充电、电量展示
3. 低电告警与节流
4. `PCF85063ATL` 接入与 `RTC_INT(GPIO39)` 建模
5. `sleep/wakeup` 策略
6. 长按关机与真正的板级电源策略

## 参考文件

- `main/app/app_main.c`
- `main/app/hardware_init.c`
- `components/i2c_manager/include/i2c_manager.h`
- `components/i2c_manager/i2c_manager.c`
- `components/audio_codec/audio_codec.c`
- `docs/context/knowledge/esp32-s3/axp2101-deep-dive.md`
- `docs/context/knowledge/project/axp2101-integration-staging.md`
- `docs/context/knowledge/project/power-wakeup-control-map.md`
- `docs/context/knowledge/project/startup-init-and-blocking-chain.md`
- `docs/context/knowledge/project/display-touch-audio-bus-map.md`
- `D:\xiaozhiai\xiaozhi-esp32\main\boards\common\board.h`
- `D:\xiaozhiai\xiaozhi-esp32\main\boards\common\power_save_timer.cc`
- `D:\xiaozhiai\xiaozhi-esp32\main\boards\common\adc_battery_monitor.cc`
