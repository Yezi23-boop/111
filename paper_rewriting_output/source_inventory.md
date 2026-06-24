# Source Inventory

## 官方竞赛材料

| 类别 | 文件/链接 | 当前状态 | 用途 |
| --- | --- | --- | --- |
| 官网 | `https://www.socchina.net/home?trackType=2` | 已记录 | 竞赛入口、公告和提交时间 |
| 上传要求 | `https://www.socchina.net/details?id=5298701eb1964050b40ca0d8867b9218&value=2` | 已记录 | 视频、报告、代码包和匿名要求 |
| 第一轮通知 | `reference_materials/2026-application-track-notice-first-round.pdf` | 已下载 | 参赛对象、赛制 |
| 报告模板 | `reference_materials/2026-application-track-report-template.docx` | 已下载并抽取文本 | 正式报告章节和字数约束 |

## 项目设计材料

| 类别 | 文件 | 用途 |
| --- | --- | --- |
| 比赛准备 | `D:\esp32S3\111\docs\competition\embedded-competition-report-ppt-guide.md` | 报告、PPT、视频和答辩主线 |
| Hermes 定位 | `D:\esp32S3\111\docs\context\knowledge\project\ai-memory-watch-product-positioning.md` | 手表作为 Hermes 随身入口 |
| 危险提醒架构 | `D:\esp32S3\111\docs\context\knowledge\project\hearing-assist-danger-alert-system-architecture.md` | 听障危险声提醒目标和分层 |
| 状态机策略 | `D:\esp32S3\111\docs\context\knowledge\project\hearing-assist-danger-alert-state-machine-and-notification-policy.md` | 风险状态机、告警保持和冷却 |
| 固件映射 | `D:\esp32S3\111\docs\context\knowledge\project\hearing-assist-danger-alert-firmware-mapping.md` | 当前实现状态与差距 |
| 仓库画像 | `D:\esp32S3\111\docs\context\knowledge\project\project-profile.md` | ESP-IDF、LVGL、音频、联网、AI owner |

## 源码材料

| 模块 | 文件 | 报告用途 |
| --- | --- | --- |
| 端侧推理运行时 | `D:\esp32S3\111\components\espdl_inference\espdl_audio_runtime.cpp` | 音频采集、滑窗、特征、推理 |
| 模型 runner | `D:\esp32S3\111\components\espdl_inference\espdl_model_runner.cpp` | 模型加载、阈值、二分类判定 |
| 风险融合服务 | `D:\esp32S3\111\main\features\danger_detection\danger_detection_service.c` | 连续确认、hold、cooldown、快照 |
| 提醒管理 | `D:\esp32S3\111\main\features\alerts\app_alert_manager.c` | 当前告警编排能力 |
| 危险识别 UI | `D:\esp32S3\111\main\ui\custom\danger_detection_controller.c` | 页面入口、用户开关和状态展示 |
| Hermes 服务 | `D:\esp32S3\111\main\services\memory_watch_service.c` | Hermes 页面状态、命令、回执 |
| 语音客户端 | `D:\esp32S3\111\main\services\memory_watch_voice_client.c` | 手表语音上传和服务器通信 |
| 录音/封装 | `D:\esp32S3\111\main\services\memory_watch_recorder.c` | 录音链路 |
| watch endpoint | `D:\esp32S3\111\server\watch_voice_endpoint\app.py` | 服务端语音/文本入口 |
| 接口契约 | `D:\esp32S3\111\server\watch_voice_endpoint\watch_contract.v1.json` | ESP32 与服务器字段边界 |

## 测试和日志材料

| 文件 | 当前价值 | 注意事项 |
| --- | --- | --- |
| `D:\esp32S3\111\board_logs\2026-06-09-15-19-53-hermes-text-command-smoke-stackfix.summary.json` | 可证明启动、联网、UI 首帧、无 fatal，以及 Hermes 文本联调环境 | 部分 custom evidence 为 0，不能夸大 |
| `D:\esp32S3\111\board_logs\2026-06-17-00-10-34-hermes-voice-mw-upload-psram-stack-30s.summary.json` | 当前显示 flash 阶段未进入有效 observe，不适合当成功证据 | 只能作为调试记录，不当成果证据 |
| `D:\esp32S3\111\server\watch_voice_endpoint\README.md` | 支撑 watch endpoint 设计、测试脚本和安全边界 | 报告中引用时要避免暴露 token/env 路径 |
| `D:\esp32S3\111\server\watch_voice_endpoint\tests` | 支撑接口契约测试存在 | 需要后续跑测试或摘取结果 |

## 缺失材料

| 材料 | 用途 | 后续动作 |
| --- | --- | --- |
| 实物照片 | 第三部分“完成情况” | 需要拍手表正面和斜 45 度照片 |
| 危险提醒界面截图 | 软件成果和测试结果 | 需要从真机或模拟器截图 |
| 危险声识别日志 | 识别效果、延迟和状态机证据 | 需要板端串口测试 |
| 断网提醒视频 | 证明核心安全提醒不依赖网络 | 需要录制短视频或日志 |
| Hermes 回执截图 | 证明随身操控端闭环 | 需要页面截图或日志 |
| 重要代码清单 | 提交代码包 | 需要排除 build/cache，只保留关键源码 |
