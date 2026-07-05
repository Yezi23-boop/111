# Figure Asset Map

## 必备图表

| 图表 ID | 图表名称 | 类型 | 当前素材状态 | 生成/补充建议 |
| --- | --- | --- | --- | --- |
| F00 | 作品功能总览图 | 框图 | 已生成 PNG：`final_paper/figures/generated_diagrams/f00_function_overview.png`，并在 `main.tex` 中引用 | 展示危险声识别、本地提醒、App 远程补充告警、Hermes 任务辅助和样本闭环 |
| F00B | 典型应用场景图 | 场景关系图 | 已生成 PNG：`final_paper/figures/generated_diagrams/f00b_application_scenario.png`，并在 `main.tex` 中引用 | 只保留一张应用场景图；后续若重绘，可在同一图中覆盖出行、公共场所和家属/监护人远程获知提醒 |
| F00C | 设计流程图 | 流程图 | 已生成 PNG：`final_paper/figures/generated_diagrams/f00c_design_process.png`，并在 `main.tex` 中引用 | 需求分析 -> 系统架构 -> 端侧识别 -> 状态机提醒 -> 任务辅助 -> 闭环优化 |
| F01 | 系统总体架构图 | 框图 | 已生成 PNG：`final_paper/figures/generated_diagrams/f01_system_architecture.png`，并在 `main.tex` 中引用 | 支撑第二部分系统组成 |
| F02 | 手表端软件模块图 | 框图 | 已生成 PNG：`final_paper/figures/generated_diagrams/f02_software_architecture.png`，并在 `main.tex` 中引用 | 展示端侧安全链路、Hermes 服务链路、样本记录和告警投递 |
| F03 | 危险声识别流程图 | 流程图 | 已生成 PNG：`final_paper/figures/generated_diagrams/f03_edge_ai_flow.png`，并在 `main.tex` 中引用 | 麦克风采集 -> 滑窗重采样 -> Fbank -> ESP-DL 推理 -> 证据融合 -> 本地提醒 |
| F04 | 风险状态机图 | 状态图 | 已生成 PNG：`final_paper/figures/generated_diagrams/f04_risk_state_machine.png`，并在 `main.tex` 中引用 | Off、Monitoring、Suspicious、Alerting、Cooldown |
| F05 | Hermes 任务辅助协同流程图 | 流程图 | 已生成 PNG：`final_paper/figures/generated_diagrams/f05_hermes_flow.png`，并在 `main.tex` 中引用 | 手表端 -> 云服务端 -> ASR -> Hermes -> 电脑端工具 -> 结构化回执；用于说明手表作为个人 AI Agent 的随身交互与控制终端 |
| F06 | 数据闭环流程图 | 流程图 | 已生成 PNG：`final_paper/figures/generated_diagrams/f06_data_loop.png`，并在 `main.tex` 中引用 | 危险告警 -> 连续 PCM -> SD 本地缓存 -> 人工导出 -> 样本积累 -> 误报分析 -> 训练优化 -> 量化部署 |
| F07 | 整机实物组图 | 照片组图 | 缺失，P0 必补 | 正面、斜 45 度、佩戴或装配状态合成一张，证明作品不是只有原理图/PCB |
| F07B | PCB 实物组图 | 照片组图 | 缺失，P0 必补 | PCB 正面、背面、上电或焊接调试状态合成一张，补足硬件实物证据；不替代整机组图 |
| F08 | 危险告警/震动组图 | 截图/日志/视频帧组图 | 缺失，P0 必补 | 危险提醒界面、状态机日志片段、震动短视频关键帧合成一张 |
| F08B | 灵敏度与后台安全监听截图 | 截图/日志组图 | 缺失，P0 必补 | 同图展示保守/标准/敏感三档、安全监听开关和后台状态；24 h 稳定运行摘要可合并在角落 |
| F09 | Hermes 回执截图 | 截图组图 | 缺失，P1 建议补 | 建议使用一张真实截图组图：手表端请求 + 回执摘要；电脑端工具协同结果仅在完成实测后补充 |
| F10 | 测试结果表 | 表格 | 已在 `main.tex` 中生成骨架并补入最新状态 | 危险声、本地提醒、灵敏度档位、震动提醒、后台安全监听、Hermes、App/手机端远程补充告警、SD 样本保存、云服务端鉴权 |
| F11 | 关键板端日志整理 | 文本/代码片段 | 已并入 `main.tex` 3.3 特性成果 | 状态机流转、灵敏度/震动/后台安全监听代码证据、SD board test、Hermes 文本/语音回执、模型大小与推理耗时 |
| F12 | App/手机通知链路组图 | 流程图/截图组图 | 流程图已生成 PNG：`final_paper/figures/generated_diagrams/f12_phone_alert_chain.png`，并在 `main.tex` 中引用；P0 真实截图缺失 | 手表触发危险、App/手机通知栏、云服务端接收日志合成一张；家属/监护人绑定提醒作为后续扩展 |
| F13 | 系统完整原理图 | 硬件设计图 | 已从用户 DOCX 提取：`final_paper/figures/hardware/f13_system_schematic.png`，并在 `main.tex` 中引用；本轮已做匿名性与版面检查 | 展示 USB、AXP2101、ESP32-S3R8、音频、SD、AMOLED、MIC、MOTOR 等硬件模块 |
| F14 | PCB 板框图 | 硬件设计图 | 已从用户 DOCX 提取：`final_paper/figures/hardware/f14_pcb_outline.png`，并在 `main.tex` 中引用；本轮已做匿名性与版面检查 | 展示 46.0 mm × 37.0 mm 板框、定位孔、Type-C 避让和圆角轮廓 |
| F15 | PCB 顶层布局图 | 硬件设计图 | 已从用户 DOCX 提取：`final_paper/figures/hardware/f15_pcb_top_layout.png`，并在 `main.tex` 中引用；本轮已做匿名性与版面检查 | 展示 PCB 顶层器件与走线布局；作为设计图，不替代实物照片 |
| F16 | PCB 底层布局图 | 硬件设计图 | 已从用户 DOCX 提取：`final_paper/figures/hardware/f16_pcb_bottom_layout.png`，并在 `main.tex` 中引用；本轮已做匿名性与版面检查 | 展示 PCB 底层器件、接口与马达/麦克风等丝印；作为设计图，不替代实物照片 |
| F17 | 模型评估与结构指标 | 指标表/混淆矩阵/结构表 | 已有真实数据，已写入 `main.tex` 1.4 和 3.3 特性成果 | 数据来自当前部署模型评估报告：131 条 background/horn/siren 测试集，准确率 96.18%，召回率 93.83%，误报率 0.00%；正文已补训练平台、DSCNNTiny、Fbank、ESP-DL int8 等结构与部署参数 |
| F18 | SD 样本保存组图 | 文件截图/JSON 截图组图 | 缺失，P0 必补 | SD 目录中的 WAV/JSON 文件与打开后的 JSON 关键字段合成一张 |

## 当前不适合使用的素材

| 素材 | 原因 |
| --- | --- |
| 天气图标素材 | 与本比赛主线关系弱，不适合作为核心成果图 |
| 包含学校、指导老师、账号、鉴权凭据、私有服务器地址的截图 | 官方要求匿名，且存在安全风险 |
| 2026-06-17 语音日志截图 | 该日志未形成有效成功记录，暂不作为提交证据 |

## 图表优先级

1. F00、F00B、F00C、F01、F02、F03、F04、F05、F06、F12 已以 PNG 结构图补入 LaTeX，支撑第一、第二部分系统组成。
2. F13-F16 已从用户 DOCX 提取并补入硬件章节，支撑“硬件已完成”的原理图、机械板框和 PCB 布局说明。
3. 第三部分证据压缩为 6 个 P0 核心组图：F07 整机实物组图、F07B PCB 实物组图、F08 危险告警/震动组图、F08B 灵敏度与后台安全监听截图、F12 App/手机通知链路组图、F18 SD 样本保存组图。
4. F10 已在 LaTeX 中生成骨架，本轮补入 App/手机端远程补充告警、灵敏度档位、震动提醒、后台安全监听和 SD 样本保存最新状态。
5. P1 证据包括 F09 Hermes 回执截图和 24 h 稳定运行日志摘要；其中 24 h 日志可合并到 F08B 或 F08。F17 模型评估指标已有真实数据并已入正文，后续只需按需要导出成图片。
6. P2 弱证据建议合并处理：机械结构与交互区域标注图、单独震动提醒照片、Hermes 多张截图、独立服务器测试通过截图、多张重复实物图。

## 缺失图拍摄/采集建议

- F07：在纯色背景前拍摄，正面、斜 45 度、佩戴或装配状态合成一张，屏幕可显示时间或主页，避免背景出现文字。
- F07B：拍摄 PCB 正面、背面、上电或焊接调试状态，合成一张 PCB 实物组图；避免出现学校、姓名、账号、定位地址或私有网络信息。
- F13-F16：本轮渲染页 8--13 后未见学校、指导老师、账号、鉴权凭据或私有 IP 等匿名风险；提交前仍建议按最终 PDF 再检查一次清晰度与是否需要裁剪。它们只能证明设计完成，不能替代实物照片。
- F08：触发危险声后，把手表危险告警界面、状态机日志片段和震动短视频关键帧合成一张。
- F08B：在危险识别页拍摄三档灵敏度控件、安全监听开关和后台状态文案；如已有 24 h 稳定运行日志，可作为小角标合并。
- F09：建议补充一张 Hermes 回执截图，展示手表端请求和回执摘要；电脑端工具协同结果仅在完成实测后加入。
- F12：触发危险告警后，把手表触发画面、App/手机通知栏和云服务端收到告警的日志合成一张。家属/监护人绑定若尚未完成，可标注为后续扩展。
- SD 样本文件：优先截取 SD 卡样本目录中成对生成的 WAV 与 JSON 文件，并打开 JSON 显示置信度、采样率、声道数和前后采样时长字段，合成一张 SD 样本保存组图。
- F17：当前真实指标来自本地 AudioClassification-Pytorch 评估报告和同目录 JSON；该来源仅作内部溯源，正式报告正文使用“当前部署模型”口径，只按 background/horn/siren 写入，不包含 alarm。
