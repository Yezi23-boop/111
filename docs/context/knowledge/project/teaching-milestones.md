---
id: esp32-ui-audio-teaching-milestones
tags: teaching, milestone, validation, beginner, lvgl, audio, wifi
summary: 面向新手的 ESP32-S3 显示/音频项目分阶段教学验收点。
last_reviewed: 2026-03-11
memory_type: procedural
scope: repo
owners: docs/context/knowledge/project/teaching-milestones.md
triggers: teaching, milestones
evidence_level: design
---

# 里程碑 1：环境可用

- 完成基础 ESP-IDF 工程编译、烧录与运行。
- 能抓取串口日志并识别启动阶段。

# 里程碑 2：UI 点亮

- 在屏幕上显示 LVGL 控件。
- 确认触摸事件进入 UI 控件回调。

# 里程碑 3：连接与音频

- 稳定完成一次 Wi-Fi 配网或连网流程。
- 能从 SPIFFS 或 SD 卡播放至少一类音频文件。

# 里程碑 4：候选发布

- 完成功耗回归检查。
- 通过 UI、触摸、音频和配网关键路径冒烟测试。
