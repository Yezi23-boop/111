---
id: main-directory-map
tags: project, architecture, main, layout, esp32-s3
summary: 记录 main 组件当前按 app、services、features、ui 分层后的目录职责，便于后续整理与定位改动。
last_reviewed: 2026-04-08
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
  - `network_service.[ch]`：后台联网状态机与 BLE/AP 切换入口。
  - `official_chat_service.[ch]`：`official_chat` 生命周期与消息缓存服务。
- `main/features/alerts`
  - `app_alert_manager.[ch]`：提醒聚合入口。
  - `audio_alert_player.[ch]`：告警提示音播放。
  - `display_alert_adapter.[ch]`：危险覆盖层 UI 适配。
- `main/features/danger_detection`
  - `danger_detection_service.[ch]`：危险声音识别 service 层。
- `main/features/audio`
  - `audio_app.[ch]`：录音相关应用层逻辑。
- `main/features/weather`
  - `time_weather.[ch]`：时间天气任务。
  - `hptts.[ch]`：天气 HTTP 请求与解析遗留模块。
- `main/ui`
  - `lvgl_task.[ch]`：LVGL 主任务。
  - `custom/`：手写控制器、视图、字体桥接和辅助逻辑。
  - `generated/`：GUI Guider 导出的页面与资源。

## include 约定

- 同目录头文件继续使用短 include。
- 跨目录 hand-written include 统一使用基于 `main` 根目录的显式路径，例如：
  - `services/network_service.h`
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
- 若后续把 `features/weather` 进一步拆成 service 层，或把 `hptts` 下沉到组件层，需要同步更新本文。
