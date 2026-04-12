---
id: axp2101-power-component-design
tags: project, axp2101, power, pmic, design, rtc
summary: 当前手表项目接入 AXP2101 新电源管理组件的分层设计、接口草案、初始化时序、阶段边界与验证闭环。
last_reviewed: 2026-04-10
---

# AXP2101 电源管理组件设计

## 目标

- 为当前 `ESP32-S3-Touch-AMOLED-2.06` 手表项目建立可扩展、低侵入、可验证的电源管理组件。
- 第一版优先完成“只读电源观测基座”，稳定获取：
  - `VBUS` 存在与质量
  - 电池存在状态
  - 充电 / 放电状态
  - 电池电压
  - 系统电压
  - 电量百分比
  - `AXP2101` IRQ 状态
- 设计必须为后续能力预留接缝：
  - 电池图标 / 充电状态栏
  - 低电量告警
  - `AXP_IRQ` 驱动
  - `PCF85063ATL` RTC 联动
  - sleep / wakeup / 长按关机

## 非目标

- 第一版不修改 `AXP2101` 电源轨开关或电压。
- 第一版不修改充电电流、终止电流、充电目标电压。
- 第一版不接管 `PWRON / PWROK`。
- 第一版不直接实现 light sleep / deep sleep / 软件关机。
- 第一版不把 `GPIO10` 重新定义为电源键原始输入。

## 证据来源

- 板级硬件与网络映射：
  - `docs/context/knowledge/esp32-s3/amoled-206-board-hardware-map.md`
  - `docs/context/knowledge/project/power-wakeup-control-map.md`
- AXP2101 结构与寄存器边界：
  - `docs/context/knowledge/esp32-s3/axp2101-deep-dive.md`
  - `docs/context/knowledge/project/axp2101-integration-staging.md`
  - `tmp/axp2101-swcharge-extract.txt`
- 当前仓库启动链路与共享总线：
  - `main/app/hardware_init.c`
  - `main/app/app_main.c`
  - `components/i2c_manager/include/i2c_manager.h`
  - `components/i2c_manager/i2c_manager.c`
  - `components/audio_codec/audio_codec.c`
- 可借鉴的多板型电源抽象思路：
  - `D:/xiaozhiai/xiaozhi-esp32/main/boards/common/board.h`
  - `D:/xiaozhiai/xiaozhi-esp32/main/boards/common/power_save_timer.cc`
  - `D:/xiaozhiai/xiaozhi-esp32/main/boards/common/adc_battery_monitor.cc`

## 当前约束

### 硬件约束

- `AXP2101` 在当前板上不是孤立外设，而是整机电源中枢。
- `AXP_IRQ` 为 active-low，外部已上拉到 `VCC_RTC`，网络接向 `EXIO5`。
- `PWROK -> CHIP_PU`，不适合按普通 GPIO 建模。
- `PWRON` 是 PMIC 电源键输入，不等价于 MCU 侧普通按键。
- `GPIO10` 接的是 `SYS_OUT` 镜像，和电源键链路相关，但不是 `PWRON` 原始脚。
- `RTC_INT -> GPIO39`，后续 RTC 唤醒必须与 PMIC 设计协同考虑。

### 软件约束

- 共享 `I2C` 使用 `GPIO14/15`、`I2C_NUM_0`、`400kHz`。
- 当前总线已被触摸、音频控制面复用，新增 PMIC 访问不能破坏现有设备。
- `hardware_init.c` 已承担 NVS、音频、SD、按键、配网初始化，不应继续堆入大量 PMIC 逻辑。
- 当前启动链路要求“基础硬件先 ready，联网后台继续”，PMIC 第一版不应改变这一原则。

### 风险约束

- 若误写 `REG80+` 输出轨控制，可能直接影响显示、音频、RTC 或其他板级电源域。
- 若误把 `GPIO10` 当作 `PWRON`，后续关机 / 唤醒策略会偏离硬件事实。
- 若把 `PWROK` 当普通状态脚读取或驱动，会干扰主控复位链路。
- 若在共享 I2C 上做阻塞或异常访问，可能连带影响触摸和音频控制。

## 方案比较

### 方案 A：直接引入单体 `PowerManager`

- 思路：
  - 参照 `xiaozhi-esp32`，用一个大类或大模块同时处理采样、状态、策略、睡眠和 UI 接缝。
- 优点：
  - 上层接入简单。
  - 电池 UI、策略、日志集中。
- 缺点：
  - 容易把寄存器事实、板级语义、策略动作耦合在一起。
  - 难以复用到后续 `RTC`、`AXP_IRQ`、低功耗扩展。
  - 不符合当前仓库已有 `components + main/app + main/services` 分层。
- 结论：
  - 不选。适合 ADC 简单板，不适合当前 `AXP2101` 中枢型设计。

### 方案 B：仅做纯 `AXP2101` 驱动

- 思路：
  - 只新增 `components/axp2101`，上层模块各自直接读 PMIC 状态。
- 优点：
  - 硬件边界清晰。
  - 第一阶段只读接入简单。
- 缺点：
  - 上层会反复散落 PMIC 细节。
  - UI、策略、低电量、RTC 联动缺少统一接缝。
- 结论：
  - 不选。适合作为底层，但不足以支撑项目长期演进。

### 方案 C：三层混合架构

- 思路：
  - `components/axp2101` 只做芯片层。
  - `main/app/board_power.[ch]` 做板级语义层。
  - `main/services/power_service.[ch]` 做策略与状态发布层。
- 优点：
  - 同时吸收 `AXP2101` 硬件事实和 `xiaozhi-esp32` 的上层抽象思路。
  - 便于第一版只读接入，后续再逐步放权。
  - 与当前仓库的 `components / app / services` 结构一致。
- 缺点：
  - 第一版文件数比单体实现多。
  - 需要先明确接口边界，不能边写边凑。
- 结论：
  - 选用本方案。

## 选定架构

```text
components/axp2101
  -> 设备生命周期、I2C 访问、寄存器读写、状态快照、IRQ 读清

main/app/board_power.[ch]
  -> 将 PMIC 状态翻译为当前板子的电源语义

main/services/power_service.[ch]
  -> 周期刷新、状态变更通知、UI/策略接缝
```

## 文件划分与职责

### 1. `components/axp2101`

建议文件：

- `components/axp2101/CMakeLists.txt`
- `components/axp2101/include/axp2101.h`
- `components/axp2101/axp2101.c`
- `components/axp2101/axp2101_regs.h`

职责：

- 通过现有 `i2c_manager` 复用共享总线。
- 设备 `probe`。
- 封装寄存器读写。
- 提供只读状态快照。
- 提供 IRQ 状态读取与清除。

第一版不开放：

- 电源轨开关控制
- 充电参数配置
- 睡眠 / 唤醒控制
- 软件关机 / 重启

### 2. `main/app/board_power.[ch]`

职责：

- 聚合 `AXP2101` 快照。
- 对外提供“板级可理解的电源状态”。
- 缓存最近一次有效状态。
- 第一版只做状态面，不做策略控制。

### 3. `main/services/power_service.[ch]`

职责：

- 周期轮询 `board_power`。
- 判断状态是否变化。
- 打日志。
- 通过回调或缓存向 UI / 状态栏 / 未来策略模块提供状态。
- 负责将“低电量阈值”“数据陈旧后的降级语义”这类产品策略施加到缓存状态上。
- 发布层应使用双缓冲快照，避免读到半更新状态；`power_service_get_state()` 与回调参数都只代表服务层拥有的只读视图，长期持有时应由调用方自行复制。

第一版不负责：

- 自动待机
- 自动深睡
- RTC 唤醒编排
- 长按关机

## 接口草案

### `axp2101.h`

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

### `board_power.h`

```c
typedef struct {
    bool available;
    bool battery_data_valid;
    bool snapshot_stale;
    bool charging;
    bool discharging;
    bool external_power_present;
    bool battery_present;
    uint16_t battery_mv;
    uint16_t system_mv;
    uint8_t battery_percent;
} board_power_state_t;

esp_err_t board_power_init(void);
esp_err_t board_power_refresh(board_power_state_t *state);
const board_power_state_t *board_power_get_cached_state(void);
```

### `power_service.h`

```c
typedef void (*power_state_changed_cb_t)(const board_power_state_t *state);

esp_err_t power_service_init(void);
esp_err_t power_service_start(void);
void power_service_register_callback(power_state_changed_cb_t cb);
const board_power_state_t *power_service_get_state(void);
```

## 状态映射规则

### PMIC 事实层

- `REG00 / REG01`：
  - `VBUS good`
  - `battery present`
  - `BATFET state`
  - 当前充电 / 放电方向
  - 充电阶段
- `REG34 / REG35`：
  - 电池电压
- `REG38 / REG39`：
  - `VBUS` 电压
- `REG3A / REG3B`：
  - `VSYS` 电压
- `REGA4`：
  - 电量百分比
- `REG48 / REG49 / REG4A`：
  - IRQ 状态

### 板级语义层

- `available`
  - `AXP2101` 已探测成功且最新快照有效。
- `battery_data_valid`
  - 电池电压与电量数据可用。
  - 当无电池、读数失败或 gauge 数据不可用时，应明确置为 `false`，而不是把 `battery_percent=0` 误判为低电量。
- `snapshot_stale`
  - 最近一次刷新失败，但仍保留上一份有效缓存。
  - 便于 UI 和日志区分“当前轮读取失败”和“缓存里仍有最近一次已知状态”。
- `external_power_present`
  - 第一版建议映射为 `vbus_good`。
- `charging`
  - 按 PMIC 当前状态位判断。
- `discharging`
  - 按电池电流方向位判断。

### 策略层派生语义

- `low_battery`
  - 不建议作为 `board_power_state_t` 的原生字段。
  - 第一版建议由 `power_service` 按产品阈值派生，例如：
    - `battery_data_valid == true`
    - `battery_percent <= 15`
    - 或与 `battery_mv` 联合判断
  - 后续若接 `REG1A + IRQ`，仍由策略层决定如何合并“阈值”和“中断”。

## 初始化时序

### 第一版推荐时序

1. `hardware_init()` 保持现有 NVS、音频、SD 初始化顺序不变。
2. 在 `audio_codec_init()` 成功之后调用 `board_power_init()`。
3. `board_power_init()` 内部调用 `axp2101_init()`，其内部通过 `i2c_manager_init()` 获取共享总线。
4. `board_power_init()` 若失败：
   - 记录日志
   - 标记状态不可用
   - 不阻塞整机继续启动
5. `app_main()` 在 `lvgl_task` 创建后、`network_service_start()` 前后均可启动 `power_service`。
   - 第一版推荐在 `lvgl_task` 之后启动，便于后续状态栏接入。

### 为什么不把 PMIC 第一版前移到更早阶段

- 当前板在不写 PMIC 的情况下已经能正常点屏、触摸、音频。
- 第一版目标是“只读观测”，不是“接管上电时序”。
- 把只读 PMIC 初始化放在音频总线已稳定之后，可降低回归风险。
- 当后续进入“关机 / 唤醒 / 电源轨控制”阶段，再评估是否需要前移。

## 轮询与错误退避

- 第一版不建议高频轮询，默认建议 `1s` 或更低频率。
- `power_service` 在连续读取失败时应：
  - 先读取 `board_power_get_cached_state()`
  - 仅当缓存本身代表“已有历史有效快照”时，才将 `snapshot_stale` 置为 `true` 并把 `available` 收回到 `false`
  - 若缓存仍是未采样默认态，则保留未采样语义，不硬标 `stale`
  - 对错误日志做节流，避免刷屏
  - 可选做简单退避，例如失败后将下一次采样延后到 `2s~5s`
- UI 不应在 `snapshot_stale` 时把缓存直接当最新事实，需要显示为“最近一次已知状态”或静默保持。
- 服务层发布应采用双缓冲切换，先写入非活动缓冲，再在临界区切换活动索引，避免并发读取到半更新结构。

## 第一阶段寄存器范围

### 必读

- `REG00`
- `REG01`
- `REG34 / REG35`
- `REG38 / REG39`
- `REG3A / REG3B`
- `REGA4`
- `REG48 / REG49 / REG4A`

### 暂不写

- `REG18`
- `REG1A`
- `REG25`
- `REG26`
- `REG27`
- `REG40 / REG41 / REG42`
- `REG61 ~ REG68`
- `REG80+`

原因：

- 这些寄存器涉及充电器总控、告警阈值、睡眠/唤醒、IRQ 使能、充电参数、电源轨控制。
- 当前设计第一版只需要读事实，不需要改策略。

## 与现有模块的接缝

### `hardware_init.c`

- 仅新增一次 `board_power_init()` 调用。
- 初始化失败默认非致命。
- 不把 `AXP2101` 业务塞进 `button_init()`、`wifi_provision` 或音频模块。

### `app_main.c`

- 新增 `power_service_init()/start()`。
- 第一版只要求服务启动，不强依赖 UI。

### UI / 状态栏

- 第一版只预留状态获取接口，不强制立即加电池图标。
- 后续若加状态栏，只从 `power_service_get_state()` 读，不直接碰 PMIC 驱动。

### RTC

- 后续接 `PCF85063ATL` 时，应在策略层编排：
  - 谁负责保时
  - 谁负责唤醒
  - 是否需要进入 PMIC sleep
- 不把 RTC 逻辑塞进 `components/axp2101`。

## 阶段计划

### 阶段 1：只读基座

- 新增 `components/axp2101`
- 新增 `board_power`
- 新增 `power_service`
- 跑通状态日志

完成标准：

- `0x34` 可稳定读取
- 连续轮询不影响触摸 / 音频控制
- 插拔 USB 时状态能变化

### 阶段 2：IRQ 与事件模型

- 只在确认 `AXP_IRQ -> EXIO5` 的 MCU 接入路径后再接中断
- 接 `VBUS insert/remove`
- 接 `charge start/done`
- 接 `PKEY short/long`

完成标准：

- IRQ 状态寄存器和外部事件一致
- 清中断后 IRQ 线可恢复

### 阶段 3：UI 与低电量联动

- 状态栏电池 / 充电图标
- 低电量告警
- 低电量降级策略

### 阶段 4：sleep / wakeup / RTC 协同

- `PCF85063ATL` 保时
- RTC 闹钟 / 中断
- PMIC sleep / wakeup 联动
- 长按关机策略

## 主要风险

### P0：误写 PMIC 控制寄存器

- 现象：
  - 屏幕不亮
  - 音频失效
  - RTC 保活异常
- 缓解：
  - 第一版不对外开放危险写接口
  - 在驱动层区分只读 API 与后续受控写 API

### P0：共享 I2C 回归

- 现象：
  - 触摸失联
  - 编解码器控制异常
  - 总线探测卡住
- 缓解：
  - 必须复用 `i2c_manager`
  - 轮询频率保持低
  - 单次事务短小，失败后快速返回

### P1：GPIO 语义误建模

- 现象：
  - 把 `GPIO10` 当 `PWRON`
  - 把 `PWROK` 当普通 GPIO
  - 误把 `RTC_INT` 与 `AXP_IRQ` 混为一类
- 缓解：
  - 文档和接口中显式分开这些信号
  - 第一版不接 `PWRON / PWROK`

### P1：启动链路被 PMIC 失败阻塞

- 现象：
  - PMIC 读失败导致 UI 不起或联网不启动
- 缓解：
  - 第一版 `board_power_init()` 失败不阻塞主链路
  - 仅将状态标记为不可用

## 验证计划

### 构建验证

- 进入 `ESP-IDF 5.5.3` 环境。
- 新增组件后完成 `idf.py build`。
- 若改动 `sdkconfig`，必须先 `idf.py fullclean` 再构建。

### 功能验证

1. 上电后共享总线不回归：
   - 触摸正常
   - 音频控制正常
2. `AXP2101` 探测成功：
   - 读取 `0x34`
   - 快照有效
3. 稳态轮询验证：
   - 连续读取状态不影响现有 UI
4. USB 插拔验证：
   - `external_power_present / charging / discharging` 有合理变化
5. 边界日志验证：
   - 无电池 / 电量未知 / 读失败时日志明确
6. 错误退避验证：
   - 连续读失败时不会导致日志刷屏或其他共享 I2C 设备异常

### 后续 IRQ 验证

- 清中断前后 `REG48/49/4A` 有预期变化
- 不出现“状态位已清，但 IRQ 线仍持续拉低”的异常

## 回滚策略

- 若只读组件引入问题：
  - 去掉 `board_power_init()` 与 `power_service_start()` 调用即可回退
  - 不需要恢复任何 PMIC 配置，因为第一版不写危险寄存器
- 若共享总线出现异常：
  - 临时停用轮询服务，仅保留 `probe`
  - 再最小化到单次寄存器读取排障

## 最终建议

- 当前项目最合适的路径不是“直接移植官方示例”或“直接复用 `xiaozhi-esp32` 的 `PowerManager`”。
- 正确做法是：
  - 用 `AXP2101` 提供真实电源事实源
  - 用 `board_power` 提供当前板级语义
  - 用 `power_service` 承接未来 UI / 低电量 / RTC / 睡眠策略
- 第一版必须坚持“先只读、先观测、先不改电源轨”的边界。
