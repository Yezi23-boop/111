---
id: esp32-s3-watch-power-checklist
tags: esp32-s3, power, sleep, battery, wearable
summary: ESP32-S3 智能手表待机功耗优化检查清单。
last_reviewed: 2026-06-07
memory_type: procedural
scope: board
owners: components/axp2101, main/services/power/power_service.c, main/ui/ui_refresh_policy.c
triggers: power, optimization, checklist
evidence_level: design
---

# 待机电流优化清单

- 在空闲态降低背光亮度或关闭背光。
- 熄屏后降低传感器采样频率。
- 在空闲态挂起非关键 FreeRTOS 任务。
- 关闭未使用外设并校正 GPIO 上下拉配置。
- 使用 light sleep 并显式配置唤醒源。

# 测量流程

1. 测启动后空闲基线电流。
2. 测亮屏活跃电流。
3. 测熄屏空闲电流。
4. 测睡眠电流与唤醒延迟。

# 验收目标

- 空闲电流低于上一版本。
- 100 次唤醒测试无漏唤醒。
- UI 恢复时延低于约定阈值。
