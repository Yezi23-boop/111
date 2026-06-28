# Figure Asset Map

## 必备图表

| 图表 ID | 图表名称 | 类型 | 当前素材状态 | 生成/补充建议 |
| --- | --- | --- | --- | --- |
| F01 | 系统总体架构图 | 框图 | 已在 `final_paper/main.tex` 中用 TikZ 生成，并已预览检查 | 支撑第二部分系统组成 |
| F02 | 手表端软件模块图 | 框图 | 可由源码 owner 生成 | 展示 UI、audio_codec、espdl_inference、danger_detection_service、alert_manager、memory_watch_service；若时间紧可并入 F01 说明 |
| F03 | 危险声识别流程图 | 流程图 | 已在 `final_paper/main.tex` 中用 TikZ 生成，并已预览检查 | 环境声音 -> 滑窗/Fbank -> ESP-DL 推理 -> 状态机 -> 本地提醒 |
| F04 | 风险状态机图 | 状态图 | 已在 `final_paper/main.tex` 中用 TikZ 生成，并已预览检查 | Off、Monitoring、Suspicious、Alerting、Cooldown |
| F05 | Hermes 语音任务协同流程图 | 流程图 | 已在 `final_paper/main.tex` 中用 TikZ 生成，并已预览检查 | 手表录音 -> endpoint -> ASR/Hermes -> JSON 回执 -> 手表显示 |
| F06 | 数据闭环流程图 | 流程图 | 已在 `final_paper/main.tex` 中用 TikZ 生成，并已预览检查 | 危险事件 -> 记录/上传 -> 样本积累 -> 误报分析 -> 模型优化 -> 回部署 |
| F07 | 手表实物照片 | 照片 | 缺失 | 拍摄正面和斜 45 度全局照片，确保背景无学校/指导老师信息 |
| F08 | 危险提醒界面截图 | 截图 | 缺失 | 真机或 UI 预览截图，显示“危险/Alerting”高对比提示 |
| F09 | Hermes 页面/回执截图 | 截图 | 缺失 | 展示录音、上传、处理、回执状态；注意不暴露服务器地址和 token |
| F10 | 测试结果表 | 表格 | 已在 `main.tex` 中生成骨架，待补实测数据 | 危险声、本地提醒、Hermes、事件上传、电脑端协同 |
| F11 | 关键板端日志整理 | 文本/代码片段 | 已在 `main.tex` 3.5 小节中以 verbatim 形式补入 | 状态机流转、Hermes 文本/语音回执、模型大小与推理耗时 |

## 当前不适合使用的素材

| 素材 | 原因 |
| --- | --- |
| `D:\esp32S3\111\resources\weather\*.png` | 天气图标与本比赛主线关系弱，不适合作为核心成果图 |
| 包含学校、指导老师、账号、token、私有服务器地址的截图 | 官方要求匿名，且存在安全风险 |
| 2026-06-17 语音日志截图 | 该日志未进入有效 observe，不应作为成功证据 |

## 图表优先级

1. F01、F03、F04、F05、F06 已补入 LaTeX，完成文字遮挡修复，支撑第二部分系统组成。
2. F07–F09 是第三部分核心，必须人工补拍实物图/截图。
3. F10 已在 LaTeX 中生成骨架，本轮用 E15–E19 填充实测数据。
4. F11 作为日志整理文本，辅助 F07–F09 的证据说明。

## 缺失图拍摄/采集建议

- F07：在纯色背景前拍摄，手表正面一张，斜 45 度一张，屏幕可显示时间或主页，避免背景出现文字。
- F08：触发危险声后，拍摄手表屏幕显示“危险/Alerting”状态的页面；若用模拟器，需保证分辨率与手表一致。
- F09：在 Hermes 页面从录音、上传到显示回执的完整流程中截取 2–3 张关键帧。
- F10：在 `server/watch_voice_endpoint` 运行 `pytest tests/test_app.py -q` 后截取终端输出，作为接口测试证据。
