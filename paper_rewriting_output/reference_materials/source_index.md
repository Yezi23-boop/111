# Reference Source Index

## 官方来源

| ID | 来源 | 本地副本 | 关键用途 |
| --- | --- | --- | --- |
| OFFICIAL-HOME | 嵌入式芯片与系统设计竞赛官网 `https://www.socchina.net/home?trackType=2` | 无 | 竞赛流程、时间节点、公告入口 |
| OFFICIAL-UPLOAD | 2026年应用赛道作品上传要求 `https://www.socchina.net/details?id=5298701eb1964050b40ca0d8867b9218&value=2` | 无 | 视频、报告、代码包、截止时间、匿名要求 |
| OFFICIAL-AI | 2026嵌入式大赛口号解读及AI应用说明 `https://www.socchina.net/details?id=b1f52d20f4544949bda6c89fefb66c74&value=2` | 无 | AI for Design & Design for AI，端侧 AI 应用鼓励方向 |
| OFFICIAL-NOTICE | 应用赛道第一轮通知 PDF | `2026-application-track-notice-first-round.pdf` / `.txt` | 参赛对象、参赛办法、赛程依据 |
| OFFICIAL-TEMPLATE | 应用赛道作品报告模板 DOCX | `2026-application-track-report-template.docx` / `.txt` | 报告章节、字数要求、图文组织规范 |

## 项目本地来源

| ID | 来源 | 关键用途 |
| --- | --- | --- |
| LOCAL-GUIDE | `D:\esp32S3\111\docs\competition\embedded-competition-report-ppt-guide.md` | 比赛报告和 PPT 主线 |
| LOCAL-POSITIONING | `D:\esp32S3\111\docs\context\knowledge\project\ai-memory-watch-product-positioning.md` | Hermes 随身操控设备端定位 |
| LOCAL-DANGER-ARCH | `D:\esp32S3\111\docs\context\knowledge\project\hearing-assist-danger-alert-system-architecture.md` | 听障危险提醒系统架构 |
| LOCAL-STATE | `D:\esp32S3\111\docs\context\knowledge\project\hearing-assist-danger-alert-state-machine-and-notification-policy.md` | 风险状态机与提醒策略 |
| LOCAL-FIRMWARE-MAP | `D:\esp32S3\111\docs\context\knowledge\project\hearing-assist-danger-alert-firmware-mapping.md` | 固件模块归属、实现状态和差距 |
| LOCAL-PROFILE | `D:\esp32S3\111\docs\context\knowledge\project\project-profile.md` | 仓库画像和真实 owner |

## 硬性规则提取

| 规则 | 来源 | 对报告/PPT的影响 |
| --- | --- | --- |
| 作品介绍视频 MP4，3 分钟以内，文件 300M 内，重点放作品演示 | OFFICIAL-UPLOAD | 视频不能做成纯 PPT 讲解，要拍实物和运行过程 |
| 作品设计报告 PDF，50M 以内 | OFFICIAL-UPLOAD | 最终需要 PDF 版本，图片需压缩 |
| 重要代码 RAR 压缩包，100M 以内 | OFFICIAL-UPLOAD | 代码包需筛选“重要代码”，避免整个 build 目录 |
| 作品提交截止 2026年7月9日18时 | OFFICIAL-HOME / OFFICIAL-UPLOAD | 倒排报告、PPT、视频、代码包准备时间 |
| 报告及视频中不可出现学校名称及指导老师信息 | OFFICIAL-UPLOAD / OFFICIAL-TEMPLATE | 所有截图、封面、视频画面和文件元数据都要做匿名检查 |
| 报告参考文献限 20 篇以内 | OFFICIAL-TEMPLATE | 引用要少而准，不追求堆数量 |
| 摘要 800 字内，作品概述各小节有明确字数限制 | OFFICIAL-TEMPLATE | 正文要严格控制篇幅，优先图表和证据 |
