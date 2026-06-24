# Figure Asset Map

## 必备图表

| 图表 ID | 图表名称 | 类型 | 当前素材状态 | 生成/补充建议 |
| --- | --- | --- | --- | --- |
| F01 | 系统总体架构图 | 框图 | 无现成图片 | 画出 ESP32-S3 手表、watch endpoint、Hermes、服务器、电脑端 |
| F02 | 手表端软件模块图 | 框图 | 可由源码 owner 生成 | 展示 UI、audio_codec、espdl_inference、danger_detection_service、alert_manager、memory_watch_service |
| F03 | 危险声识别流程图 | 流程图 | 可由文档生成 | 环境声音 -> 滑窗/Fbank -> ESP-DL 推理 -> 状态机 -> 本地提醒 |
| F04 | 风险状态机图 | 状态图 | 可由状态机文档生成 | Off、Monitoring、Suspicious、Alerting、Cooldown |
| F05 | Hermes 语音任务协同流程图 | 流程图 | 可由 product positioning 和 watch_contract 生成 | 手表录音 -> endpoint -> ASR/Hermes -> JSON 回执 -> 手表显示 |
| F06 | 数据闭环流程图 | 流程图 | 目前偏设计 | 危险事件 -> 记录/上传 -> 样本积累 -> 误报分析 -> 模型优化 -> 回部署 |
| F07 | 手表实物照片 | 照片 | 缺失 | 拍摄正面和斜 45 度全局照片 |
| F08 | 危险提醒界面截图 | 截图 | 缺失 | 真机或 UI 预览截图 |
| F09 | Hermes 页面/回执截图 | 截图 | 缺失 | 展示录音、上传、处理、回执状态 |
| F10 | 测试结果表 | 表格 | 可先生成骨架 | 危险声、本地提醒、Hermes、事件上传、电脑端协同 |

## 当前不适合使用的素材

| 素材 | 原因 |
| --- | --- |
| `D:\esp32S3\111\resources\weather\*.png` | 天气图标与本比赛主线关系弱，不适合作为核心成果图 |
| 包含学校、指导老师、账号、token、私有服务器地址的截图 | 官方要求匿名，且存在安全风险 |

## 图表优先级

1. 先画 F01-F05，可直接支撑第二部分系统组成。
2. 再补 F07-F09，用于第三部分完成情况。
3. 最后用 F10 汇总性能和功能完成度。
