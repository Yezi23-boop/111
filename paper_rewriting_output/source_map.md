# Source Map

## 基本配置

| 项目 | 当前值 |
| --- | --- |
| 工作流 | build_from_materials |
| 场景 | competition |
| 输出语言 | zh |
| 目标名称 | 嵌入式 |
| 材料目录 | D:\esp32S3\111 |
| 官方入口 | https://www.socchina.net/home?trackType=2 |

## 写作主线

本报告围绕“聆安 Watch：基于 ESP32-S3 与 Hermes 的听障辅助边缘 AI 智能手表”展开。当前最稳妥的比赛表达主线是：

```text
面向听障人群的边缘 AI 安全提醒终端，
同时也是 Hermes 个人助手的随身操控设备端。
```

报告应避免写成普通智能手表，也避免写成泛泛的云端 AI 助手。核心叙事应落在三个层次：

1. 听障用户在出行和公共场所中难以及时感知鸣笛、警笛、报警声等危险声音。
2. ESP32-S3 手表通过麦克风采集、ESP-DL 端侧推理、风险融合状态机和本地提醒形成低延迟安全闭环。
3. 手表作为 Hermes 的低摩擦随身入口，支持语音提交任务、回执展示、事件回顾与数据闭环。

## 本地材料索引

| 材料 | 类型 | 可支撑内容 | 可信度 |
| --- | --- | --- | --- |
| `D:\esp32S3\111\docs\competition\embedded-competition-report-ppt-guide.md` | 比赛报告/PPT 指南 | 报告结构、PPT 页序、答辩主线、演示视频清单 | 高，用户已确认的比赛材料 |
| `D:\esp32S3\111\docs\context\knowledge\project\ai-memory-watch-product-positioning.md` | 产品定位 | Hermes 随身输入与交互工具、端云分工、V1 功能边界 | 高，项目知识库 active 文档 |
| `D:\esp32S3\111\docs\context\knowledge\project\hearing-assist-danger-alert-system-architecture.md` | 系统架构 | 听障危险提醒目标、核心用户、danger 定义、系统分层、演进路线 | 高，项目知识库设计文档 |
| `D:\esp32S3\111\docs\context\knowledge\project\hearing-assist-danger-alert-state-machine-and-notification-policy.md` | 状态机与提醒策略 | Off/Monitoring/Suspicious/Alerting/Cooldown，确认、保持、清除和冷却策略 | 高，项目知识库设计文档 |
| `D:\esp32S3\111\docs\context\knowledge\project\hearing-assist-danger-alert-firmware-mapping.md` | 固件实现映射 | ESP-DL runtime、danger_detection_service、alert manager、UI controller 的代码归属和完成状态 | 高，包含 observed 级别实现核对 |
| `D:\esp32S3\111\docs\context\knowledge\project\project-profile.md` | 仓库画像 | ESP32-S3 + ESP-IDF 固件主线、LVGL、音频、联网、危险识别、Hermes owner | 高，项目知识库 observed 文档 |
| `D:\esp32S3\111\main\features\danger_detection\danger_detection_service.c` | 固件源码 | 风险融合、连续窗口确认、hold/cooldown、对外快照 | 高，源码证据 |
| `D:\esp32S3\111\components\espdl_inference\espdl_audio_runtime.cpp` | 固件源码 | 音频采集、滑窗、Fbank、模型推理回调 | 高，源码证据 |
| `D:\esp32S3\111\components\espdl_inference\espdl_model_runner.cpp` | 固件源码 | 模型加载、阈值、二分类 danger 判定 | 高，源码证据 |
| `D:\esp32S3\111\main\features\alerts\app_alert_manager.c` | 固件源码 | 告警编排、屏幕/音频提醒现状与差距 | 高，源码证据 |
| `D:\esp32S3\111\main\ui\custom\danger_detection_controller.c` | 固件源码 | 危险识别页面、用户开关、后台 Safety Monitor 状态展示 | 高，源码证据 |
| `D:\esp32S3\111\main\services\memory_watch_service.c` | 固件源码 | Hermes 页面状态、命令、回执和 endpoint 在线状态 | 高，源码证据 |
| `D:\esp32S3\111\main\services\memory_watch_voice_client.c` | 固件源码 | 手表语音上传、watch endpoint 交互 | 高，源码证据 |
| `D:\esp32S3\111\server\watch_voice_endpoint\README.md` | 服务端说明 | watch endpoint、语音桥、Hermes 联动演示依据 | 中高，需继续核对实现和测试 |
| `D:\esp32S3\111\server\watch_voice_endpoint\watch_contract.v1.json` | 接口契约 | 固件和服务器之间的请求/响应边界 | 高，契约证据 |

## 报告章节与材料对应关系

| 报告单元 | 计划表达 | 主要材料锚点 | 证据缺口 |
| --- | --- | --- | --- |
| 摘要 | 交代听障危险声感知问题、ESP32-S3 端侧识别、本地提醒、Hermes 操控入口和数据闭环 | 比赛指南、产品定位、危险提醒架构 | 需要最终压缩为 300-500 字版本 |
| 设计背景与目的 | 听障用户在交通和公共场所难以及时感知高价值危险声音 | 危险提醒架构、比赛指南 | 可补外部背景资料或官方赛题导向 |
| 选题方向匹配 | 作品适合“嵌入式边缘 AI 应用”：端侧采集、推理、决策和实时提醒 | 比赛官方入口、ESP-DL 固件实现、危险提醒架构 | 需要下一步核对官方赛题要求 |
| 总体方案 | 手表端、watch endpoint、Hermes、服务器、电脑端协同 | 比赛指南、产品定位、server/watch_voice_endpoint | 需要整理成一张系统总体架构图 |
| 硬件组成 | ESP32-S3、麦克风、屏幕、触摸、震动/提醒、电源管理等 | project-profile、仓库硬件相关组件 | 需要补硬件实物图和最终 BOM/连接说明 |
| 软件架构 | LVGL UI、音频采集、ESP-DL 推理、风险融合、提醒管理、联网和 Hermes 服务 | project-profile、firmware mapping、源码 owner | 需要整理成模块图 |
| 边缘 AI 识别流程 | 环境声音采集 -> 特征处理 -> 模型推理 -> 连续窗口确认 -> 本地提醒 | espdl_audio_runtime、espdl_model_runner、danger_detection_service | 需要补模型版本、输入窗口、阈值和板端资源数据 |
| 风险状态机 | Monitoring/Suspicious/Alerting/Cooldown，降低误报和抖动 | state-machine policy、firmware mapping、danger_detection_service | 需要用日志或截图证明状态流转 |
| 提醒策略 | 当前已有屏幕/音频告警，产品目标应强调听障场景下震动优先和高对比提示 | state-machine policy、app_alert_manager、danger_detection_view | 震动链路若未实装，报告中必须标成后续优化或当前差距 |
| Hermes 联动 | 手表是 Hermes 的随身操控设备端，负责录音、状态和回执；Hermes 负责记忆、整理、提醒和工具调用 | product-positioning、memory_watch_service、memory_watch_voice_client、watch endpoint | 需要服务端演示截图、接口日志或视频证据 |
| 数据闭环 | 危险事件、识别结果、场景信息用于样本积累、误报分析和模型优化 | 比赛指南、危险提醒架构、firmware mapping | 当前更多是设计闭环，需补真实事件日志或明确为预留能力 |
| 完成情况 | 列表化呈现已完成、部分完成、规划中功能 | firmware mapping、源码、测试/日志 | 需要板端实测证据 |
| 实验结果 | 危险声触发、断网本地提醒、Hermes 语音任务、事件上传、电脑端协同 | 串口日志、UI 截图、server 日志、演示视频 | 需要继续收集真实截图、日志、表格 |
| 创新点 | 端侧危险声识别、本地安全提醒、Hermes 随身操控、跨设备任务协同、自进化数据闭环 | 比赛指南、产品定位、危险提醒架构 | 创新点表述要避免超出已实现证据 |
| 总结与展望 | 已形成边缘 AI 安全提醒和 Hermes 操控入口原型，后续优化抗噪、震动、低功耗、事件闭环 | 比赛指南、firmware mapping、state-machine policy | 展望需区分已完成和计划中 |

## 当前可用论点

| 论点 | 可写强度 | 支撑材料 | 写作注意 |
| --- | --- | --- | --- |
| 作品面向听障人群的危险声音感知补偿需求 | 强 | 危险提醒架构、比赛指南 | 不要夸大为医疗设备或救命系统 |
| 作品采用 ESP32-S3 + ESP-DL 进行端侧危险声识别 | 强 | project-profile、espdl_inference 源码、firmware mapping | 需要补具体模型版本和资源占用 |
| 危险识别不是单窗即报警，而是有连续确认、保持和冷却机制 | 强 | state-machine policy、danger_detection_service、firmware mapping | 可作为降低误报的技术亮点 |
| 核心安全提醒不应依赖网络 | 中强 | 危险提醒架构、ESP-DL 端侧实现 | 需要板端断网测试证据 |
| 手表是 Hermes 的随身操控设备端 | 强 | product-positioning、memory_watch_service、watch endpoint | 不要写成 ESP32-S3 端跑大模型 |
| Hermes 负责长期记忆、上下文、任务整理、提醒和工具调用 | 中强 | product-positioning | 需要区分规划能力和当前接入能力 |
| 数据闭环可用于样本积累、误报分析和模型优化 | 中 | 危险提醒架构、比赛指南 | 若缺真实上传和训练证据，应写成“预留/设计为” |
| 当前提醒层距离听障产品目标仍需震动优先、持续提醒和事件记录 | 强 | firmware mapping、state-machine policy | 可以放在不足与展望，不应遮蔽已完成主线 |

## 禁止或需谨慎的表述

- 不写“可识别所有危险声音”。
- 不写“可判断危险距离、方向和真实物理位置”。
- 不写“ESP32-S3 端运行大模型/完整 Hermes Agent”。
- 不写“已完成云端自进化训练闭环”，除非后续补到真实证据。
- 不把当前音频+红屏提醒包装成完整的听障震动提醒体系；如震动未实装，应明确为后续优化。
- 不编造准确率、召回率、延迟、功耗、样本规模和用户实验数据。

## 下一步材料任务

1. 核对官方赛题方向和作品报告要求，形成 `reference_materials/source_index.md` 与 `research_dossier.md`。
2. 建立 `source_inventory.md`，按文档、源码、测试、日志、截图、视频资产分类。
3. 建立 `evidence_bank.md`，把每个报告结论绑定到截图、日志、源码或文档。
4. 生成候选控制动机，等待用户确认后再写正文。
