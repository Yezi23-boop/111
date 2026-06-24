# Evidence Bank

## 可直接支撑的结论

| 结论 ID | 可写结论 | 证据来源 | 证据强度 | 写作边界 |
| --- | --- | --- | --- | --- |
| E01 | 本作品面向听障用户危险声音感知场景 | `hearing-assist-danger-alert-system-architecture.md` | 强 | 不写医疗诊断或救命承诺 |
| E02 | active 主线 danger 聚焦 horn/siren/alarm | `hearing-assist-danger-alert-system-architecture.md` | 强 | 不写识别所有危险声 |
| E03 | 系统采用端侧音频采集、特征处理和 ESP-DL 推理 | `espdl_audio_runtime.cpp`、`espdl_model_runner.cpp`、`hearing-assist-danger-alert-firmware-mapping.md` | 强 | 具体模型指标待补 |
| E04 | 风险融合层存在 Monitoring/Suspicious/Alerting/Cooldown 等状态 | `danger_detection_service.c`、`state-machine-and-notification-policy.md`、`firmware-mapping.md` | 强 | 需要补板端状态流转日志 |
| E05 | 当前有连续窗口确认、hold 和 cooldown 设计 | `danger_detection_service.c`、`firmware-mapping.md` | 强 | 具体参数以源码最终版本为准 |
| E06 | 当前提醒层已有屏幕/音频告警能力，但震动优先仍是后续优化重点 | `app_alert_manager.c`、`firmware-mapping.md` | 强 | 不把震动优先写成已完成 |
| E07 | 手表定位为 Hermes 的随身输入与交互工具 | `ai-memory-watch-product-positioning.md` | 强 | 不写 ESP32 端运行完整 Agent |
| E08 | watch endpoint 负责 ESP32 与 Hermes 之间的窄服务端桥接 | `server/watch_voice_endpoint/README.md`、`watch_contract.v1.json` | 强 | 不暴露密钥和真实 token |
| E09 | ESP32 只应知道 watch endpoint 和 device token，不应知道 Hermes API key | `watch_contract.v1.json`、`README.md` | 强 | 可作为安全设计点 |
| E10 | 项目是 ESP32-S3 + ESP-IDF 固件，包含 LVGL、音频、联网、危险识别、Hermes 服务 | `project-profile.md` | 强 | 仓库画像不能替代实物完成证据 |
| E11 | 官方报告结构要求摘要、作品概述、系统组成、完成情况、总结、参考文献 | `2026-application-track-report-template.docx/.txt` | 强 | 正式报告应贴合模板 |
| E12 | 视频、报告和代码包有格式、大小、匿名和截止时间要求 | 官方上传要求、`source_index.md` | 强 | 提交前需逐项检查 |
| E13 | 2026-06-09 日志显示固件可启动到 UI 首帧并联网 ready | `board_logs/2026-06-09-15-19-53-hermes-text-command-smoke-stackfix.summary.json` | 中 | 不能证明危险识别或 Hermes 完整成功 |
| E14 | 2026-06-17 语音日志未进入有效 observe，不应作为成功证据 | `board_logs/2026-06-17-00-10-34-hermes-voice-mw-upload-psram-stack-30s.summary.json` | 强负证据 | 只能用于说明仍需补测 |

## 需要用户补充或后续测试的证据

| 缺口 ID | 缺口 | 建议采集方式 | 对报告影响 |
| --- | --- | --- | --- |
| G01 | 危险声识别触发证据 | 播放 horn/siren/alarm，保存串口日志和界面截图 | 第三部分测试结果核心证据 |
| G02 | 断网本地提醒证据 | 断开 Wi-Fi 后触发危险声，录制 10-20 秒视频 | 支撑“核心安全提醒不依赖网络” |
| G03 | 响应时间 | 串口打点或视频逐帧估计，从声音到 Alerting/屏幕变化 | 性能指标表 |
| G04 | Hermes 语音回执 | 手表录音上传、服务器处理、手表显示回执截图 | 支撑 Hermes 随身操控端 |
| G05 | 事件上传/数据闭环 | 服务器日志或接口返回记录 | 支撑数据闭环；若缺失则写成预留能力 |
| G06 | 震动提醒 | 真机震动视频或代码证据 | 若未完成，写入后续优化 |
| G07 | 功耗/续航 | 电源日志或 AXP2101 读数 | 若缺失，不写具体续航 |
