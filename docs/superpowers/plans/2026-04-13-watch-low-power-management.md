# 手表项目低功耗管理实现计划

## 目标

在当前 `AXP2101 + RTC + LVGL + Wi-Fi` 架构下，按低风险顺序落地低功耗状态机，优先完成：

- `Idle-Dim`
- `Standby`
- `RTC` 唤醒准备

暂不直接进入 PMIC 深睡和关机控制。

## 约束

- 不误碰 `PWROK -> CHIP_PU`
- 不把 `GPIO10` 当 `PWRON`
- 不在第一阶段写 `AXP2101 REG80+ / REG25/26/27`
- 保持共享 I2C 稳定

## 实施步骤

### Task 1：补低功耗策略源码契约测试

- 新增 `power_policy` 路径测试
- 固定状态机枚举、超时输入、模式切换接口
- 固定 `Active / Idle-Dim / Standby` 三态边界

### Task 2：新增 `power_policy.[ch]`

- 定义状态机
- 提供：
  - `power_policy_init()`
  - `power_policy_notify_user_activity()`
  - `power_policy_tick()`
  - `power_policy_get_mode()`

### Task 3：接显示降级

- 增加 dim/off 接口
- 空闲时降低亮度
- 降低 LVGL 刷新频率
- 停掉非必要动画

### Task 4：接网络与音频降级

- 空闲待机时降低网络活跃度
- 音频链路进入 idle/off
- 确保恢复路径完整

### Task 5：接入主循环和事件

- 在用户交互、触摸、按键、网络活跃时喂入 activity
- 定时驱动 `power_policy_tick()`
- 输出状态切换日志

### Task 6：RTC 准备阶段

- 最小探测 `PCF85063ATL`
- 确认 `RTC_INT(GPIO39)` 可观测
- 不先接系统时间主来源

### Task 7：功耗验证与文档回写

- `idf.py build`
- 串口日志验证状态切换
- 真机测亮屏/Dim/Standby 电流
- 更新 `docs/context`

## 验证清单

- 空闲达到阈值后进入 `Idle-Dim`
- 再次空闲达到阈值后进入 `Standby`
- 用户触摸或按键后恢复 `Active`
- 电源状态采样不回归
- Wi-Fi/音频/触摸恢复正常

## 风险与缓解

- UI 黑屏无法恢复
  - 先做 dim，再做 off
- Wi-Fi 待机后恢复异常
  - 第一阶段只降活跃，不强制完全断网
- 多模块状态机互相打架
  - 统一收口到 `power_policy`

## 回滚

- 回退 `power_policy` 入口调用
- 保留现有 `board_power / power_service`
- 保持 `AXP2101` 仍为只读观测
