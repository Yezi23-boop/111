---
id: espdl-danger-model-plan-anchor
tags: project, esp-dl, audio, danger-detection, model-integration, deployment
summary: "固定 ESP-DL 危险声音模型接入锚点：记录当前主工程 active 模型、部署边界、固件 owner，并指向训练仓库的模型版本与训练经验入口。"
last_reviewed: 2026-06-24
memory_type: semantic
scope: repo
owners: components/espdl_inference, main/features/danger_detection/danger_detection_service.c, main/ui/custom/danger_detection_controller.c
triggers: espdl, danger, model, integration, active-model, registry
evidence_level: observed
route_area: "ESP-DL danger model"
---

# ESP-DL 危险声音模型接入锚点

本文只记录主工程 `D:\esp32S3\111` 需要知道的接入边界。训练路线、数据清洗、teacher 审计、模型比较和样板验证经验已经迁到训练仓库，不再由当前 context run 文档承载。

## 权威入口

- 训练经验索引：`D:\esp32S3\AudioClassification-Pytorch\docs\espdl\runs\README.md`
- 训练经验总览：`D:\esp32S3\AudioClassification-Pytorch\docs\espdl\runs\espdl_danger_training_experience_index.md`
- 模型版本表：`D:\esp32S3\AudioClassification-Pytorch\models\espdl_registry\model_release_table.md`
- 模型版本归档：`D:\esp32S3\AudioClassification-Pytorch\models\espdl_registry`
- 迁移 scratch 证据：`D:\esp32S3\AudioClassification-Pytorch\artifacts\archive\migrated_from_111\scratch_20260610`
- 主工程 ESP-DL 接入记录：`D:\esp32S3\111\docs\context\knowledge\project\espdl-audio-tdnn-port.md`

## 当前主工程事实

- 当前 active 模型：`edge_mix_teacher_dscnn_medium_v59_v54_anchor_softdistill_t90_20260608.espdl`。
- 主工程模型目录：`D:\esp32S3\111\components\espdl_inference\models`。
- 当前 active danger 边界：`siren / horn / alarm`。
- 扩展类边界：`gun_shot / glass_break / crash / impact` 只属于 challenger 或扩展实验，除非产品定义明确变更，否则不得默认进入 active 主线。
- 输入口径：16 kHz mono、1 秒窗口、Fbank 40 bins、INT8 模型入口。
- 部署侧默认阈值：`0.90`。
- 当前最小后处理闭环：连续 2 个 danger 窗口确认，连续 non-danger 窗口清除，保留 hold/cooldown 抑制抖动与重复提醒。
- 验证状态：V59 主工程源码接入与 `idf.py build` 已通过；板端 `app-flash`、monitor 和主工程启动期 `Model::test()` 日志待补。
- 回退锚点：`edge_mix_teacher_dscnn_small_v34_core_t90_sharp_20260511.espdl` 仍保留在主工程模型目录，但当前 CMake 不嵌入。

## 固件 owner 边界

- `components/espdl_inference` 负责 ESP-DL 模型加载、输入输出张量、推理封装和模型文件接入。
- `danger_detection_service` 负责危险识别公共状态机、连续证据融合、hold/cooldown 和对外 snapshot。
- `app_alert_manager` 负责提醒编排；危险检测服务不直接承担用户提醒 UI 编排。
- `danger_detection_controller` 只负责页面展示和用户可见状态读取，不作为长期检测能力 owner。

## 接入规则

- 新 active 模型必须从 `models\espdl_registry` 中选择可追溯版本，不直接从 scratch 临时目录拷贝。
- 晋升 active 前必须确认 `.espdl`、`export_meta.json`、样板 `Model::test()`、板端资源/耗时、类别顺序、阈值和后处理策略。
- 多分类模型可以在样板工程验证部署侧多分类输出，但接入主工程前必须先明确 active danger 映射和用户提醒语义。
- 若要把 `gun_shot / glass_break / crash / impact` 纳入主线，必须同步更新危险提醒系统架构、状态机、参数默认表和固件映射，不能只替换模型文件。
- 当前仓库不再新增训练 run 文档；训练经验继续写入 `D:\esp32S3\AudioClassification-Pytorch\docs\espdl\runs`。

## 相关主工程上下文

- 产品/系统架构：`D:\esp32S3\111\docs\context\knowledge\project\hearing-assist-danger-alert-system-architecture.md`
- 状态机与提醒策略：`D:\esp32S3\111\docs\context\knowledge\project\hearing-assist-danger-alert-state-machine-and-notification-policy.md`
- 参数与默认值：`D:\esp32S3\111\docs\context\knowledge\project\hearing-assist-danger-alert-parameter-defaults-table.md`
- 当前固件实现映射：`D:\esp32S3\111\docs\context\knowledge\project\hearing-assist-danger-alert-firmware-mapping.md`
- ESP-DL 接入记录：`D:\esp32S3\111\docs\context\knowledge\project\espdl-audio-tdnn-port.md`
