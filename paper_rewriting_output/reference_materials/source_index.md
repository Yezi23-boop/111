# Source Index

本地/默认参考材料索引，供 paper-spine-research 阶段使用。按 `reference_mode=local_first` 优先索引仓库内材料，web 材料仅作补充。

| Source ID | Type | Title/Name | Origin/URL/Path | Why Included | Local File/Note | Used For |
|---|---|---|---|---|---|---|
| S01 | official_template | 2026 全国大学生嵌入式芯片与系统设计竞赛应用赛道作品报告模板 | `paper_rewriting_output/reference_materials/2026-application-track-report-template.docx` / `.txt` | 官方报告结构、字数/图表/匿名要求 | 已提取 txt：摘要 800 字、第一部分 6 小节、第二部分系统组成、第三部分完成情况及照片、第四部分总结、参考文献 20 篇以内 | 确定报告结构、字数上限、证据形式 |
| S02 | official_notice | 2026 应用赛道初赛通知 | `paper_rewriting_output/reference_materials/2026-application-track-notice-first-round.pdf` / `.txt` | 报名、阶段、匿名、提交物要求 | pdf 文本提取不完整，需配合官网或模板 | 确认提交规则与匿名边界 |
| S03 | website | 全国大学生嵌入式芯片与系统设计竞赛官网 | `https://www.socchina.net/home?trackType=2` | 官方最新通知与上传入口 | web 补充，不替代本地模板 | 核对截止日期、分赛区规则 |
| S04 | project_profile | 项目画像 | `docs/context/knowledge/project/project-profile.md` | 了解本作品目标、技术栈、边界 | 仓库上下文核心入口 | 写作边界与术语统一 |
| S05 | architecture | 听障辅助危险提醒系统架构 | `docs/context/knowledge/project/hearing-assist-danger-alert-system-architecture.md` 等 4 张卡 | 危险声识别、状态机、参数、固件映射 | 强约束：active danger 只认 horn/siren/alarm | 产品边界与功能描述 |
| S06 | model_artifact | ESP-DL 危险声识别模型 | `components/espdl_inference/models/edge_mix_teacher_dscnn_medium_v59_v54_anchor_softdistill_t90_20260608.espdl` | 当前 active 模型文件与大小证据 | 32,160 B（约 31.4 KB） | 模型大小、性能指标 |
| S07 | board_logs | 板端串口日志 | `board_logs/2026-06-*.log` / `.summary.json` | 启动、推理、状态机、Hermes 测试原始记录 | 2026-06-06 默认安全监听、2026-06-09 文本命令、2026-06-27 语音对话 | 实测指标与证据引用 |
| S08 | server_code | watch endpoint 服务端 | `server/watch_voice_endpoint/app.py` / `tests/test_app.py` / `watch_contract.v1.json` | Hermes 桥接接口、鉴权、幂等、超时 | 包含 health / voice-command / text-command / cancel | 系统设计、接口契约、测试证据 |
| S09 | manuscript | 当前 LaTeX 草稿 | `paper_rewriting_output/final_paper/main.tex` / `main.pdf` | 已完成的报告框架与占位图 | 5 张核心 TikZ 架构图已完成 | 续写与补证据的基准 |
| S10 | context_index | 仓库上下文索引 | `docs/context/INDEX.agent.md` | 快速定位其余上下文文档 | 低 token 入口 | 辅助引用与术语核对 |
| S11 | confirmed_motivation | 已确认的写作动机 | `paper_rewriting_output/confirmed_motivation.md` | 当前作品定位与核心主张 | 需要核实是否仍有效 | 动机一致性检查 |
| S12 | figure_asset_map | 图表资产清单 | `paper_rewriting_output/figure_asset_map.md` | 缺失与已有图片清单 | F07-F10 待补 | 规划缺失证据 |
| S13 | evidence_bank | 证据银行 | `paper_rewriting_output/evidence_bank.md` | 可支撑结论与缺口 | G01-G07 缺口 | 指导实测补录 |

## 已补充说明

- 本索引服务于当前应用赛道报告收尾阶段，重点在“第三部分 完成情况及性能参数”的证据规范与匿名要求。
- 官方模板 S01 是结构主依据；S02、S03 仅用于核对规则变化。
- S06-S08 提供可直接引用的工程证据，避免编造数据。
