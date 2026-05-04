---
id: espdl-danger-model-plan-anchor
tags: project, esp-dl, audio, danger-detection, model-plan, training
summary: "固定 ESP-DL 危险声音模型计划的索引入口，指向训练仓库路线文档、版本矩阵、当前 active 模型和阈值策略，避免上下文检索丢失。"
last_reviewed: 2026-05-04
memory_type: semantic
scope: repo
owners: components/espdl_inference, main/features/danger_detection/danger_detection_service.c, main/ui/custom/danger_detection_controller.c
triggers: espdl, danger, model, plan, anchor
evidence_level: design
---

# ESP-DL 危险声音模型计划索引锚点

本文是主工程上下文库里的固定入口。后续遇到“危险声音模型计划、训练计划、中文人声 hard negative、teacher-student、DS-CNN-tiny、阈值 0.80、ESP32-S3 部署”等问题时，优先从这里跳转到训练侧文档，避免只依赖聊天上下文。

## 关键文档

- 产品/系统架构草案：`D:\esp32S3\111\docs\context\knowledge\project\hearing-assist-danger-alert-system-architecture.md`
- 主训练路线：`D:\esp32S3\AudioClassification-Pytorch\docs\espdl_danger_sound_roadmap.md`
- 训练侧命令与导出说明：`D:\esp32S3\AudioClassification-Pytorch\docs\espdl_training_side.md`
- 模型版本矩阵：`D:\esp32S3\AudioClassification-Pytorch\docs\espdl_model_versions\model-version-matrix-20260502.md`
- 当前 active 模型版本：`D:\esp32S3\AudioClassification-Pytorch\docs\espdl_model_versions\edge_mix_teacher_dscnn_tiny_1s_int8input_v20260503.md`
- 主工程 ESP-DL 接入记录：`D:\esp32S3\111\docs\context\knowledge\project\espdl-audio-tdnn-port.md`

## 当前必须记住的结论

- 固件当前 active 模型是 `edge_mix_teacher_dscnn_tiny_1s_int8input_v20260503`。
- 产品长期目标是“面向听障用户的危险提醒系统”，不是泛音频分类；用户最终拿到的是稳定提醒，不是类别名本身。
- active 主线 danger 定义固定聚焦 `siren / horn / alarm`，扩展事件不得默认覆盖该边界。
- 首版主工程只跑单模型 V3.3 DS-CNN-tiny，不并行跑 V3.2/V3.3，原因是 RAM 不够且双模型 ANY_DANGER 融合没有独立验证指标。
- 输入是 16 kHz mono 音频，1 秒窗口，Fbank 40 bins，模型入口为 INT8。
- 部署侧默认危险阈值是 `0.80`，并使用连续 2 个 danger 窗口确认、连续 non_danger 窗口和 hold/cooldown 清除，目标是降低人声误报。
- 训练本身没有固定阈值；阈值属于评估和部署后处理。但每个新 student 模型都必须报告 `threshold=0.80` 下的指标。
- 新版本验收必须单独统计 hard negative 误报率，尤其是大声/高频中文人声、普通喊话、电视/短视频/直播人声、公开摩擦/刮擦声、音乐、children_playing、dog_bark、drilling。
- 下一阶段模型路线是 V3.4：继续使用 DS-CNN-tiny INT8 student，先补公开数据中的大声/高频中文人声、手表佩戴相似摩擦声和危险正样本。纯中文人声、大声中文人声、喊话、电视/短视频/直播中文人声统一标为 `non_danger` hard negative；当前不做“救命/着火/报警”等中文语义危险识别。
- V3.4 首轮中文人声数据源收窄为 `Common Voice 中文` 优先，`AISHELL-1` 少量补干净参考；暂不优先下载 `ST-CMDS` 和 `MagicData Mandarin`。
- V3.4 active 主线只把 `siren / horn / alarm` 作为核心 danger。如果人声背景里同时有真实 `siren / horn / alarm`，不能因为有人声就标为 `non_danger`，应作为 danger 或混合正样本处理。
- `glass_break / crash / impact` 只属于 `--danger_profile extended` challenger，除非用户明确扩大产品定义，否则不得进入默认 active 训练和上线口径。

## 检索关键词

危险声音识别，危险声音模型计划，模型训练计划，中文人声，大声高频中文人声，中文喊话，人声误报，手表摩擦，衣服摩擦，cloth rustling，fabric rubbing，scraping，hard negative speech zh，teacher-student，AST，PaSST，AudioClassification-Pytorch，ESP-DL，ESP32-S3，DS-CNN-tiny，DS-TCN-small，TDNN，Fbank，INT8，threshold 0.80，danger/non_danger，V3.3，V3.4。

## 维护规则

- 训练侧路线、数据策略、teacher/student 变化，优先更新 `espdl_danger_sound_roadmap.md`，再在本文同步一句“当前必须记住的结论”。
- 固件 active 模型、阈值、防抖策略变化，优先更新 `espdl-audio-tdnn-port.md`，再在本文同步 active 状态。
- 新增正式模型版本时，必须更新模型版本矩阵，并确认本文指向的“当前 active 模型版本”仍然正确。
