---
id: hearing-assist-danger-alert-parameter-defaults-table
tags: project, product, hearing-assist, danger-detection, parameters, defaults, notification
summary: 面向听障用户的手表端危险提醒功能参数与默认值建议表，统一收敛产品常量、规则固定数值可调项、可调默认值，以及用户可配置项。
last_reviewed: 2026-05-05
memory_type: semantic
scope: repo
owners: main/features/danger_detection/danger_detection_service.c, main/features/alerts/app_alert_manager.c, components/espdl_inference
triggers: parameter, default, threshold, vibration, cooldown, sensitivity
evidence_level: design
status: active
---

# 听障危险提醒参数与默认值建议表

## 目标

- 本文将前两张设计卡继续落地：
  - [hearing-assist-danger-alert-system-architecture.md](D:\esp32S3\111\docs\context\knowledge\project\hearing-assist-danger-alert-system-architecture.md)
  - [hearing-assist-danger-alert-state-machine-and-notification-policy.md](D:\esp32S3\111\docs\context\knowledge\project\hearing-assist-danger-alert-state-machine-and-notification-policy.md)
- 目标是把“哪些参数属于产品边界、哪些参数可以调、哪些参数应开放给用户”统一收成一张表，避免后续训练、固件和 UI 各自漂移。
- 如需确认这些参数在当前代码里分别落在哪个 owner、已经做到哪一步，应继续查看：
  - [hearing-assist-danger-alert-firmware-mapping.md](D:\esp32S3\111\docs\context\knowledge\project\hearing-assist-danger-alert-firmware-mapping.md)

## 参数级别约定

- `产品常量`
  - 定义产品边界或长期行为原则，轻易不改。
- `规则固定 / 数值可调`
  - 行为骨架稳定，但窗口数、周期数、时长等可继续优化。
- `可调默认值`
  - 当前推荐起点，不是永久真理；允许在不破坏产品边界的前提下继续调参。
- `用户配置项`
  - 直接暴露给用户的模式开关或策略选择。

## 第一组：功能启停与运行前提

| 参数名 | 所属层 | 作用 | 参数级别 | 默认值建议 | 是否用户可配 | 备注 / 为什么这样定 |
| --- | --- | --- | --- | --- | --- | --- |
| `feature_enabled` | 功能控制层 | 控制危险提醒功能是否整体开启 | 用户配置项 | 默认开启 | 是 | 用户必须能明确控制该功能是否工作 |
| `runtime_ready_required` | 运行时层 | 限制功能只有在音频采集与推理运行时就绪后才进入监听 | 产品常量 | 默认要求运行时完全就绪 | 否 | 避免 UI 显示“功能开着”但底层并未真正运行 |
| `mic_resource_required` | 资源归属层 | 限制功能只有在麦克风资源可用时才允许启动监听 | 产品常量 | 默认要求麦克风资源独占或被明确授权复用 | 否 | 音频输入缺失时继续宣称在监听会破坏产品可信度 |
| `background_run_allowed` | 产品策略层 | 决定危险提醒是否允许在用户离开专用页面后继续后台运行 | 规则固定 / 数值可调 | 默认允许后台运行 | 否 | 对真实产品来说，危险提醒不应依赖用户停留在某个页面 |
| `ui_page_required` | UI 编排层 | 限制提醒功能是否必须依赖专用识别页可见 | 产品常量 | 默认不依赖专页常驻 | 否 | 提醒能力应从页面功能提升为系统能力 |

## 第二组：状态机参数

| 参数名 | 所属层 | 作用 | 参数级别 | 默认值建议 | 是否用户可配 | 备注 / 为什么这样定 |
| --- | --- | --- | --- | --- | --- | --- |
| `suspicious_threshold` | 风险融合层 | 进入 `Suspicious` 的门槛，决定系统多早开始怀疑风险 | 可调默认值 | 中高，低于正式告警阈值，但不能低到让可疑态长期常驻 | 否 | 依赖模型输出分布，应允许随训练版本优化 |
| `alert_threshold` | 风险融合层 | 单窗正式危险证据门槛，决定窗口分数高到什么程度才算强危险信号 | 可调默认值 | 偏高，优先压误报，首版默认保守 | 否 | 高阈值路线是产品原则，具体数值不是永久常量 |
| `enter_alert_rule` | 风险融合层 | 连续证据确认规则，决定满足什么多窗条件后才正式进入告警 | 规则固定 / 数值可调 | 采用连续证据确认，避免单窗直达正式告警 | 否 | 规则骨架应稳定，窗口数可调 |
| `clear_rule` | 风险融合层 | 危险解除确认规则，决定满足什么连续安全证据后才退出告警 | 规则固定 / 数值可调 | 采用连续安全证据解除，清除条件应宽于进入条件 | 否 | 宁可解除慢一点，也不要频繁闪回 |
| `min_alert_hold_ms` | 风险融合层 | 正式告警最短保持时长，用于避免告警刚进入就立即清除 | 可调默认值 | 中等偏长，覆盖短促回落，避免刚告警就立即清除 | 否 | 典型体验调优项，需结合现场实测优化 |
| `cooldown_ms` | 风险融合层 | 告警结束后的冷却时长，用于抑制边缘重复强提醒 | 可调默认值 | 中等，抑制重复触发，但不压掉新的真实危险 | 否 | 太短无意义，太长会吞掉新风险 |
| `realert_rule` | 风险融合层 | 持续提醒规则，决定危险持续或再次出现时何时补发后续提醒 | 规则固定 / 数值可调 | 危险持续期间默认应继续提醒，但提醒节奏和强度应低于首次告警 | 否 | 听障用户需要持续风险感知，但不能每窗都强震 |
| `hysteresis_enabled` | 风险融合层 | 启用回滞防抖，避免状态在危险边界附近频繁翻转 | 产品常量 | 默认开启，不建议关闭 | 否 | 这是成熟状态机的基本稳态机制，不是锦上添花 |

## 第三组：提醒参数

| 参数名 | 所属层 | 作用 | 参数级别 | 默认值建议 | 是否用户可配 | 备注 / 为什么这样定 |
| --- | --- | --- | --- | --- | --- | --- |
| `suspicious_user_visible` | 用户提醒层 | 决定 `Suspicious` 状态是否直接暴露给用户 | 产品常量 | 默认不直接可见，仅作为内部状态或调试信息保留 | 否 | 首版不应把半成品风险证据频繁暴露给用户 |
| `initial_alert_vibration_pattern` | 用户提醒层 | 首次正式危险告警时的震动模式 | 规则固定 / 数值可调 | 强感知、短促、明显区别于普通通知 | 否 | 第一震必须让用户立刻知道“这不是普通提醒” |
| `sustain_alert_vibration_pattern` | 用户提醒层 | 危险持续期间的后续补提醒震动模式 | 规则固定 / 数值可调 | 弱于首次告警，但保留危险提醒识别感 | 否 | 持续提醒需要存在，但不应每次都像首告警那样重 |
| `sustain_alert_repeat_interval_ms` | 用户提醒层 | 危险持续时补提醒的时间间隔 | 可调默认值 | 中等偏长，明显慢于推理周期，避免形成连续轰炸 | 否 | 与 `realert_rule` 配套，是现场体验优化重点之一 |
| `alert_screen_style` | 用户提醒层 | 正式危险告警时的屏幕表现样式 | 产品常量 | 高对比度、低认知负担、文案简短 | 否 | 屏幕是辅助确认，不是让用户阅读报告 |
| `alert_screen_persistence_policy` | 用户提醒层 | 危险提示页面在告警与解除过程中的保持与退出规则 | 规则固定 / 数值可调 | 跟随状态机稳定切换，不跟单窗结果直接抖动 | 否 | 屏幕持久策略必须由状态机统一驱动 |

## 第四组：用户可配置项

| 参数名 | 所属层 | 作用 | 参数级别 | 默认值建议 | 是否用户可配 | 备注 / 为什么这样定 |
| --- | --- | --- | --- | --- | --- | --- |
| `sensitivity_mode` | 用户策略层 | 选择整体提醒灵敏度策略 | 用户配置项 | 默认 `标准`，提供 `保守 / 标准 / 敏感` 三档 | 是 | 用户理解模式比理解原始阈值更容易 |
| `notification_mode` | 用户策略层 | 选择危险提醒输出通道 | 用户配置项 | 默认 `震动 + 屏幕`，允许切换为 `仅震动` | 是 | 听障用户通常仍应以震动为主通道 |
| `sustain_alert_enabled` | 用户策略层 | 控制危险持续存在时是否继续补提醒 | 用户配置项 | 默认开启 | 是 | 默认路线应持续提醒，但应允许用户按耐受度关闭 |
| `event_log_enabled` | 用户策略层 | 控制是否记录最近危险提醒事件 | 用户配置项 | 默认开启 | 是 | 事件记录有助于建立信任与支持后续复盘 |

## 第五组：工程内部参数

| 参数名 | 所属层 | 作用 | 参数级别 | 默认值建议 | 是否用户可配 | 备注 / 为什么这样定 |
| --- | --- | --- | --- | --- | --- | --- |
| `window_size_ms` | 推理链路层 | 定义单次推理音频窗口长度 | 规则固定 / 数值可调 | 固定长度窗口，优先平衡危险覆盖与实时性 | 否 | 直接影响特征 shape、响应速度和资源占用 |
| `stride_ms` | 推理链路层 | 定义相邻推理窗口的滑动步长 | 可调默认值 | 明显短于窗口长度，兼顾响应速度与资源占用 | 否 | 决定系统“看世界有多勤快” |
| `feature_pipeline_profile` | 信号表达层 | 标识当前特征提取方案 | 规则固定 / 数值可调 | 使用稳定、可追踪的特征 profile，并与训练/导出/板端一致 | 否 | 这是训练、导出和设备侧一致性的桥 |
| `model_profile_id` | 模型管理层 | 标识当前启用模型版本 | 产品常量 | 与训练版本矩阵、部署文件和阈值策略一一对应 | 否 | 对单次发布版本来说应冻结并可追溯 |
| `danger_class_profile` | 产品边界层 | 定义当前 active danger 事件边界 | 产品常量 | 固定为 `siren / horn / alarm` 主线 profile | 否 | 不允许上线时悄悄扩大 danger 口径 |
| `deployment_threshold_profile` | 部署策略层 | 标识当前部署使用的整套后处理参数配置 | 规则固定 / 数值可调 | 使用成套 profile 管理，默认走保守误报控制路线 | 否 | 不应只记单个阈值数字，而应记录整套策略 |

## 默认不直接开放给用户的参数

- 下列参数虽然重要，但第一版不建议直接开放给用户：
  - `suspicious_threshold`
  - `alert_threshold`
  - `enter_alert_rule`
  - `clear_rule`
  - `min_alert_hold_ms`
  - `cooldown_ms`
  - `realert_rule`
  - `hysteresis_enabled`
  - `sustain_alert_repeat_interval_ms`
- 原因不是它们不重要，而是它们属于工程语言，不是用户语言。
- 用户应通过 `sensitivity_mode`、`notification_mode`、`sustain_alert_enabled` 这类高层模式来间接影响系统，而不是直接接触原始阈值和状态机参数。

## 对后续实现的约束

- 后续训练、板端部署、UI 提醒策略和版本切换都应优先对齐本文，而不是单独围绕某一版模型临时拍脑袋调参。
- 若某个 challenger 模型想改动 `danger_class_profile`、`enter_alert_rule` 或 `alert_screen_style` 这类高层边界，应先回到产品/系统架构文档重新确认，而不是直接在固件里改默认值。
- 若未来需要做 A/B 实验，应优先在 `deployment_threshold_profile` 与 `model_profile_id` 层管理，而不要绕过本文直接散落多个难以追踪的局部参数。
