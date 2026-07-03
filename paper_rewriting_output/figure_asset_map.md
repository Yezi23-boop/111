# Figure Asset Map

## 必备图表

| 图表 ID | 图表名称 | 类型 | 当前素材状态 | 生成/补充建议 |
| --- | --- | --- | --- | --- |
| F00 | 作品功能总览图 | 框图 | 已生成 PNG：`final_paper/figures/generated_diagrams/f00_function_overview.png`，并在 `main.tex` 中引用 | 展示危险声识别、本地提醒、手机告警、Hermes 任务辅助和样本闭环 |
| F00B | 典型应用场景图 | 场景关系图 | 已生成 PNG：`final_paper/figures/generated_diagrams/f00b_application_scenario.png`，并在 `main.tex` 中引用 | 展示出行、公共场所和 Hermes 任务辅助入口 |
| F00C | 设计流程图 | 流程图 | 已生成 PNG：`final_paper/figures/generated_diagrams/f00c_design_process.png`，并在 `main.tex` 中引用 | 需求分析 -> 系统架构 -> 端侧识别 -> 状态机提醒 -> 任务辅助 -> 闭环优化 |
| F01 | 系统总体架构图 | 框图 | 已生成 PNG：`final_paper/figures/generated_diagrams/f01_system_architecture.png`，并在 `main.tex` 中引用 | 支撑第二部分系统组成 |
| F02 | 手表端软件模块图 | 框图 | 已生成 PNG：`final_paper/figures/generated_diagrams/f02_software_architecture.png`，并在 `main.tex` 中引用 | 展示端侧安全链路、Hermes 服务链路、样本记录和告警投递 |
| F03 | 危险声识别流程图 | 流程图 | 已生成 PNG：`final_paper/figures/generated_diagrams/f03_edge_ai_flow.png`，并在 `main.tex` 中引用 | 麦克风采集 -> 滑窗重采样 -> Fbank -> ESP-DL 推理 -> 证据融合 -> 本地提醒 |
| F04 | 风险状态机图 | 状态图 | 已生成 PNG：`final_paper/figures/generated_diagrams/f04_risk_state_machine.png`，并在 `main.tex` 中引用 | Off、Monitoring、Suspicious、Alerting、Cooldown |
| F05 | Hermes 任务辅助协同流程图 | 流程图 | 已生成 PNG：`final_paper/figures/generated_diagrams/f05_hermes_flow.png`，并在 `main.tex` 中引用 | 手表端 -> watch endpoint -> ASR -> Hermes -> 电脑端工具 -> JSON 回执 |
| F06 | 数据闭环流程图 | 流程图 | 已生成 PNG：`final_paper/figures/generated_diagrams/f06_data_loop.png`，并在 `main.tex` 中引用 | Alerting 事件 -> 连续 PCM -> SD 本地缓存 -> 人工导出 -> 样本积累 -> 误报分析 -> 训练优化 -> 量化部署 |
| F07 | 手表实物照片 | 照片 | 缺失 | 拍摄正面和斜 45 度全局照片，确保背景无学校/指导老师信息 |
| F08 | 危险提醒界面截图 | 截图 | 缺失 | 真机或 UI 预览截图，显示“危险/Alerting”高对比提示 |
| F09 | Hermes 页面/回执截图 | 截图 | 缺失 | 展示语音请求、处理、回执状态；注意不暴露服务器地址和 token |
| F10 | 测试结果表 | 表格 | 已在 `main.tex` 中生成骨架并补入最新状态 | 危险声、本地提醒、Hermes、手机通知、SD 样本保存、电脑端协同 |
| F11 | 关键板端日志整理 | 文本/代码片段 | 已在 `main.tex` 3.5 小节中以 verbatim 形式补入 | 状态机流转、Hermes 文本/语音回执、模型大小与推理耗时 |
| F12 | 手机通知栏告警推送链路 | 流程图/截图 | 流程图已生成 PNG：`final_paper/figures/generated_diagrams/f12_phone_alert_chain.png`，并在 `main.tex` 中引用；通知截图缺失 | ESP32-S3 手表 -> Alerting -> 告警 worker -> watch endpoint -> 手机通知栏 |

## 当前不适合使用的素材

| 素材 | 原因 |
| --- | --- |
| `D:\esp32S3\111\resources\weather\*.png` | 天气图标与本比赛主线关系弱，不适合作为核心成果图 |
| 包含学校、指导老师、账号、token、私有服务器地址的截图 | 官方要求匿名，且存在安全风险 |
| 2026-06-17 语音日志截图 | 该日志未进入有效 observe，不应作为成功证据 |

## 图表优先级

1. F00、F00B、F00C、F01、F02、F03、F04、F05、F06、F12 已以 PNG 结构图补入 LaTeX，支撑第一、第二部分系统组成。
2. F07–F09、F12 通知截图与 SD 样本文件截图是第三部分核心，必须人工补拍实物图/截图。
3. F10 已在 LaTeX 中生成骨架，本轮补入手机通知和 SD 样本保存最新状态。
4. F11 作为日志整理文本，辅助 F07–F09 的证据说明。

## 缺失图拍摄/采集建议

- F07：在纯色背景前拍摄，手表正面一张，斜 45 度一张，屏幕可显示时间或主页，避免背景出现文字。
- F08：触发危险声后，拍摄手表屏幕显示“危险/Alerting”状态的页面；若用模拟器，需保证分辨率与手表一致。
- F09：在 Hermes 页面从语音请求到显示回执的完整流程中截取 2–3 张关键帧。
- F10：在 `server/watch_voice_endpoint` 运行 `pytest tests/test_app.py -q` 后截取终端输出，作为接口测试证据。
- F12：触发危险告警后截取手机通知栏 Android App 通知，同时保留 watch endpoint 收到 `/v1/watch/alerts` 的服务器日志。
- SD 样本文件：触发一次 Alerting 后，截取 `/sdcard/danger_samples/<date>/` 目录中的 `.wav + .json` 文件，并打开 JSON 显示 `window_end_sample_index/pre_ms/post_ms` 字段。
