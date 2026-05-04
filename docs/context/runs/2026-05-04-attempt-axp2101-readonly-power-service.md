---
id: attempt-2026-05-04-axp2101-readonly-power-service
tags: axp2101, power, pmic, attempt-log
summary: axp2101-readonly-power-service；结果：partial。
last_reviewed: 2026-05-04
memory_type: episodic
scope: task
owners: components/axp2101, main/app/board_power.c, main/services/power_service.c, docs/context/knowledge/project/axp2101-power-component-implementation.md
triggers: AXP2101 board_power power_service 20mV snapshot
evidence_level: observed
---

# Attempt Log: axp2101-readonly-power-service

## 背景

- 本次要验证什么：记录 AXP2101 第一阶段只读接入和 power_service 观测边界，避免后续直接开放 PMIC 控制或重复刷屏日志。
- 对应任务或计划：AXP2101 read-only power stack
- 结果状态：partial

## 环境

- 分支/工作区状态：未记录
- 设备/串口/板型：未涉及或未记录
- 关键前置条件：未记录

## 操作

- 修改过的文件或 owner：
- components/axp2101
- main/app/board_power.c
- main/services/power_service.c
- docs/context/knowledge/project/axp2101-power-component-implementation.md
- 执行的命令或动作：
- 新增 AXP2101 驱动、board_power 缓存层和 power_service 1s 低频轮询
- 只在首次 read_snapshot 时受控使能 REG30 ADC 通道
- 对 battery_mv/system_mv 使用 20mV 抖动阈值抑制无意义 power state changed 日志
- 已尝试但不应直接重复的路径：
- 不要把 AXP2101 当普通外设随意改电源轨、充电参数或睡眠唤醒控制
- 不要把 system_mv 当 battery_mv
- 不要按 1 秒轮询频率打印所有 ADC 微小波动

## 观测

- 关键日志/证据：
- 单元测试 tests.test_axp2101_power_source / board_power / power_service / integration 通过
- ESP-IDF 5.5.3 idf.py build 通过；启动日志有 Board power boot snapshot
- 与预期不一致的点：
- 未记录

## 结论

- 本次可以确认的事实：第一阶段结论是只读观测可用；PMIC 控制、IRQ/RTC 联动和真实 USB 插拔仍不应默认展开。
- 仍然不能确认的事实：
- 真机 USB 插拔、IRQ、RTC 联动和共享 I2C 长时稳定性仍需复验

## 未验证风险

- 下一轮仍需补证据的边界：
- 涉及电源策略时先保持只读快照和 owner 分层，新增控制前必须补硬件证据与回滚策略
