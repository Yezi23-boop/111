---
id: esp32-s3-lvgl-porting-baseline
tags: esp32-s3, esp-idf, lvgl, display, touch
summary: ESP32-S3 在 ESP-IDF 下集成 LVGL 显示与触摸的基础点亮流程。
last_reviewed: 2026-03-07
memory_type: semantic
scope: board
owners: components/lvgl_port, components/co5300_panel, components/touch_ft5x06, main/ui/lvgl_task.c
triggers: lvgl, porting, baseline
evidence_level: observed
---

# 基础点亮流程

1. 先验证不含 LVGL 的最小工程可 `idf.py build flash monitor`。
2. 初始化显示总线并完成纯色填充测试。
3. 接入 LVGL tick 与 display flush 回调。
4. 接入触摸驱动与输入回调。
5. 渲染一个按钮控件并验证触摸事件链路。

# 必要日志

- 显示初始化成功日志（分辨率、色彩格式）。
- LVGL tick 频率与刷新周期。
- 触摸中断/轮询频率与坐标打印。

# 常见故障

- 背光引脚未使能。
- 屏幕旋转或颜色字节序配置错误。
- 触摸坐标未按屏幕方向做映射。
