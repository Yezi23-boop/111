---
id: axp2101-integration-staging
tags: project, axp2101, pmic, power, i2c
summary: 当前仓库接入 AXP2101 的推荐分层、最小只读阶段和后续放权边界。
last_reviewed: 2026-04-09
memory_type: semantic
scope: repo
owners: components/axp2101, main/app/board_power.c, main/services/power_service.c
triggers: axp2101, integration, staging
evidence_level: observed
---

# AXP2101 接入分层与阶段建议

## 当前前提

- 板上存在 `AXP2101`，默认 `7` 位地址可按 `0x34` 处理。
- 当前共享控制总线仍是 `GPIO14/15` 对应的 `I2C_NUM_0`，已被 `ES8311`、`ES7210`、`FT5x06/FT3168` 复用。
- 当前代码中还没有 `AXP2101` 驱动或板级电源策略模块。
- 板子在没有软件 PMIC 配置的前提下已经能点屏、跑 UI、起音频，说明第一阶段应默认“先观测，不改默认电源轨”。

## 推荐文件划分

1. `components/axp2101`
   - 只放寄存器访问、只读状态查询、IRQ 状态读写和最小设备生命周期。
2. `main/app/board_power.[ch]`
   - 只放当前板子的电源策略接缝，例如初始化时机、状态日志、后续与 UI/低功耗的桥接。
3. 不建议第一版直接把 PMIC 业务塞进 `main/app/hardware_init.c`
   - 该文件当前已承担 NVS、音频、SD、按键和配网初始化，继续堆 PMIC 容易把“驱动”和“板级策略”混在一起。

## 建议分阶段接入

### 阶段 1：最小只读

- 目标：
  - `probe`
  - 电池电压读取
  - 电量百分比读取
  - 充电/VBUS 状态读取
  - IRQ 状态寄存器读取与清除
- 约束：
  - 不写电源轨开关
  - 不写关机/重启
  - 不改充电电流
  - 不启用 sleep/wakeup 电源序列

### 阶段 2：GPIO 侧补齐

- 在原理图确认后再接：
  - `AXP_IRQ`
  - `GPIO10/SYS_OUT`
- 当前已知硬件事实：
  - `AXP_IRQ -> EXIO5`
  - `PWROK -> CHIP_PU`
  - `GPIO10` 接的是 `SYS_OUT`，来自 `PWRON` 物理按键链路的镜像
- 因此当前不建议把 `PWRON/PWROK` 当普通 GPIO 新增驱动，MCU 侧真正适合观测的是：
  - `AXP_IRQ`
  - `GPIO10/SYS_OUT`

### 阶段 3：板级电源策略

- 补：
  - 电池低电量告警
  - 充电动画/状态上报
  - 长按关机
  - 待机前外设断电顺序
  - 与 `PCF85063ATL` 配合的唤醒链路

### 阶段 4：放开 PMIC 控制寄存器

- 仅在完成寄存器级验证和回滚方案后，才允许修改：
  - 输出轨使能/电压
  - 充电电流/输入限流
  - `REG10H` 关机/重启
  - `REG26H` sleep/wakeup

## 当前仓库里的最小接入方式

- 继续复用 `i2c_manager`
- 在 `axp2101_init()` 里调用 `i2c_manager_init()`
- 在 `ESP-IDF >= 5.3` 下通过 `i2c_manager_get_bus_handle()` 把 `0x34` 设备挂到共享 `master bus`
- 第一版初始化建议放在音频编解码器成功起总线之后，先做状态观测；后续若进入真正的电源时序管理，再前移初始化阶段

## 第一版最值得先读的寄存器

- 电池电压：`reg34H/reg35H`
- 电量百分比：`regA4H`
- IRQ 状态：`reg48H/reg49H/reg4AH`
- VBUS 插入状态位：`reg49H[7]`
- VBUS good：`reg00H[5]`

## 最小验证闭环

1. 共享 I2C 扫描仍能看到 `0x18`、`0x38`、`0x40`
2. 新增看到 `0x34`
3. 连续读取电池电压、电量百分比不影响触摸和音频控制
4. 插拔 USB 时，`VBUS`/IRQ 相关状态位有变化
5. 清 IRQ 后状态位可恢复，且不会持续拉低中断线

## 风险边界

- `AXP2101` 和触摸、音频控制面共用一条 `400kHz` I2C，总线探测和寄存器访问异常会连带影响现有设备。
- 未确认引脚映射前，不要把 `PWRON/PWROK/AXP_IRQ` 当成普通 GPIO 直接驱动。
- 未确认板上默认电源轨拓扑前，不要尝试“软件重新上电”显示、音频、存储或无线相关电源。
