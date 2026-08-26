---
id: main-directory-map
tags: project, architecture, main, layout, esp32-s3
summary: 记录 main 组件当前按 app、services、features、ui 分层后的目录职责，便于后续整理与定位改动。
last_reviewed: 2026-07-31
memory_type: semantic
scope: repo
owners: main/app/app_main.c, main/app/hardware_init.c, main/services, main/features, main/ui
triggers: main, directory, map
evidence_level: observed
status: active
---

# main 目录分层地图

## 结论

- `main` 组件当前按 `app / services / features / ui` 四层组织。
- 目标是把“入口、后台服务、业务特性、UI 运行时”从根目录解耦，但不改变现有公开 C 接口和运行时行为。
- `ui/generated` 与 `ui/custom` 继续保持边界清晰：生成代码留在 `generated`，手写桥接与视图逻辑留在 `custom`。

## 目录职责

- `main/app`
  - `app_main.c`：正式 `app_main()` 入口。
  - `hardware_init.[ch]`：板级初始化与基础资源拉起。
- `main/services`
  - `memory_watch/`：Hermes 录音、HTTP/WS、同步、inbox、endpoint 配置和 runtime owner。
  - `power/`：电源快照、整机预算、sleep dry-run 与唤醒证据。
  - `network/`：`network_service` ready probe 与兼容 shim。
  - `sensors/`：IMU 长期采样和 snapshot。
  - `runtime/`：启动 readiness、注册式 runtime coordinator、Safety Monitor policy 与默认关闭的协调板测；不拥有业务资源。
  - `time/`：系统时间 service owner。
  - `weather/`：天气周期任务、snapshot 和内部 HTTP client。
  - `safety/`：Safety Monitor session 生命周期。
  - `audio_diag/`：一次性麦克风诊断服务。
  - `official_chat_service.[ch]`：`official_chat` 生命周期与消息缓存服务，暂保留在 services 根目录。
  - `fall_detection_service.[ch]`：跌倒推理 service，暂保留在 services 根目录。
- `main/features/alerts`
  - `app_alert_manager.[ch]`：提醒聚合入口。
  - `audio_alert_player.[ch]`：告警提示音播放。
  - `display_alert_adapter.[ch]`：危险覆盖层 UI 适配。
- `main/features/danger_detection`
  - `danger_detection_service.[ch]`：危险声音识别 service 层。
  - `danger_sample_recorder.[ch]`：危险触发样本录制（WAV+JSON，挂 ESP-DL PCM tap）。
- `main/features/memory_watch`
  - 记录 Memory Watch 的产品语义边界；当前不新增只转发一层的 feature facade。
- `main/ui`
  - `lvgl_task.[ch]`：LVGL 主任务。
  - `custom/`：手写控制器、视图、字体桥接和辅助逻辑。
  - `generated/`：GUI Guider 导出的页面与资源。
- `tools/ui_preview`
  - host 预览、mock、构建与截图工具；不属于板端 `main/ui`。

## include 约定

- 同目录头文件继续使用短 include。
- 跨目录 hand-written include 统一使用基于 `main` 根目录的显式路径，例如：
  - `services/network/network_service.h`
  - `services/memory_watch/memory_watch_service.h`
  - `services/power/power_service.h`
  - `services/weather/weather_service.h`
  - `services/official_chat_service.h`
  - `features/alerts/display_alert_adapter.h`
  - `features/danger_detection/danger_detection_service.h`
  - `ui/custom/ai_ui_controller.h`

## 构建约定

- `main/CMakeLists.txt` 显式列出 `app/services/features/ui` 的 hand-written 源码。
- 仅 `main/ui/generated/*.c` 继续通过 glob 收集，避免每次 GUI Guider 导出后都手工追增文件。
- `INCLUDE_DIRS` 维持最小集合：`.`、`ui/custom`、`ui/generated`。

## 适用边界

- 本文描述的是当前目录职责，不替代更高层的系统架构文档。
- 新 service 必须先判断长期 owner，再进入窄子目录；不要恢复 services 根目录平铺或创建宽泛 `runtime/system` 杂物目录。
