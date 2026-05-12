---
id: danger-detection-state-machine-framework-plan-20260512
tags: context, plans, danger-detection, hearing-assist, esp-dl, state-machine, execplan
summary: 将当前危险识别从隐式连续窗口逻辑整理为显式服务层状态机，固定 ESP-DL、service、alert manager、UI 的 owner 边界。
last_reviewed: 2026-05-12
memory_type: task
scope: task
owners: main/features/danger_detection/danger_detection_service.c, main/features/danger_detection/danger_detection_service.h
triggers: danger-detection, hearing-assist, state-machine, espdl, alerting, cooldown
evidence_level: design
---

# 危险识别状态机框架完善计划

## Purpose / Big Picture

- 任务目标：把当前危险识别链路从“ESP-DL 单窗结果 + service 内部计数 + UI/提醒各自处理”整理为显式、可观测、可继续演进的服务层状态机。
- 为什么现在做：当前模型阈值和连续窗口已经能压住一部分误报，但系统还缺少统一的 `Suspicious / Alerting / Cooldown` 状态语义，后续换模型、加震动、做后台运行都会继续漂。
- 完成后用户会看到什么变化：
  - 危险提醒状态在 service 层有明确枚举。
  - UI 和提醒层可以只读 service 快照，不再各自维护一套危险生命周期。
  - 日志能解释为什么触发、为什么清除、当前处于什么状态。

## Source Context

- 产品架构：`docs/context/knowledge/project/hearing-assist-danger-alert-system-architecture.md`
- 状态机策略：`docs/context/knowledge/project/hearing-assist-danger-alert-state-machine-and-notification-policy.md`
- 参数表：`docs/context/knowledge/project/hearing-assist-danger-alert-parameter-defaults-table.md`
- 固件映射：`docs/context/knowledge/project/hearing-assist-danger-alert-firmware-mapping.md`

## Owner Boundary

- `components/espdl_inference`
  - 负责音频采集、重采样、Fbank、`.espdl` 模型加载、INT8 推理、输出 `danger_prob` 和模型标签。
  - 不负责提醒状态机，不负责用户可见告警生命周期。
- `main/features/danger_detection/danger_detection_service.*`
  - 本轮核心 owner。
  - 负责公共状态机、连续证据融合、hold、clear、cooldown、对外快照。
- `main/features/alerts/app_alert_manager.*`
  - 负责提醒动作编排。
  - 不判断模型阈值，不维护连续窗口。
- `main/ui/custom/danger_detection_controller.c`
  - 只负责页面展示和用户入口。
  - 不把页面生命周期作为长期功能 owner。

## Scope / Non-Goals

- 本轮明确要做：
  - 在 `danger_detection_service` 对外快照里增加显式风险状态。
  - 把现有 ESP-DL 后处理整理成 `Monitoring / Suspicious / Alerting / Cooldown` 语义。
  - 保持当前 active 主线：`siren / horn / alarm`，二分类输出仍是 `danger / non_danger`。
  - 保持当前高阈值路线，运行阈值仍由 ESP-DL runner/profile 管理。
  - 增加必要日志，让状态转移可解释。
- 本轮明确不做：
  - 不换模型，不重新训练。
  - 不把 `glass_break / crash / impact` 并入 active。
  - 不做后台常驻启动策略。
  - 不做震动硬件接入。
  - 不大改 UI 页面结构。
  - 不改 audio codec 生命周期归属，除非构建暴露必须修复的问题。

## Proposed State Machine

### State Definitions

- `OFF`
  - 服务未运行，或功能不可用。
- `MONITORING`
  - 服务运行中，当前无足够危险证据。
- `SUSPICIOUS`
  - 已出现高风险窗口，但尚未满足正式告警条件。
  - 第一版作为内部/调试状态，不强提醒用户。
- `ALERTING`
  - 满足正式危险确认条件，触发用户提醒。
- `COOLDOWN`
  - 告警刚解除，短暂抑制重复强提醒。

### First Implementation Policy

- `MONITORING -> SUSPICIOUS`
  - 出现 1 个 ESP-DL danger 窗口。
- `SUSPICIOUS -> ALERTING`
  - 连续 danger 窗口达到现有确认规则。
  - 当前沿用现有规则：2 个连续 danger 窗口。
- `SUSPICIOUS -> MONITORING`
  - danger 证据中断，且当前未进入正式告警。
- `ALERTING -> COOLDOWN`
  - 已过最短 hold，且连续 clear 窗口达到现有清除规则。
  - 当前沿用现有规则：hold 2000ms 后连续 3 个 non-danger 窗口。
- `COOLDOWN -> MONITORING`
  - 冷却计时结束，且无持续危险证据。
- `COOLDOWN -> ALERTING`
  - 冷却计时结束后仍持续满足连续 danger 证据，可重新进入正式告警。
  - 冷却计时未结束前，即使模型继续输出 danger，也只累计证据，不重复触发强提醒。

## Parameter Policy

- `runtime_threshold`
  - 继续由 `espdl_model_runner` 管理。
  - 当前实测建议高阈值路线，目标运行阈值 0.90。
- `confirm_windows`
  - service 层规则，首版沿用 2。
- `clear_windows`
  - service 层规则，首版沿用 3。
- `hold_ms`
  - service 层规则，首版沿用 2000ms。
- `cooldown_ms`
  - service 层规则，首版建议先用短冷却，例如 3000ms。
  - 本轮若担心行为变化过大，可以先只发布 `COOLDOWN` 状态并保持提醒行为接近现状。

## Implementation Steps

- `[x]` Step 1：扩展 `danger_detection_service.h`
  - 新增风险状态枚举。
  - 在 snapshot 中增加 `risk_state`。
  - 增加状态转文本函数，方便 UI 和日志复用。
- `[x]` Step 2：整理 `danger_detection_service.c`
  - 初始化/停止时设置 `OFF`。
  - ESP-DL start 成功后进入 `MONITORING`。
  - 在 ESP-DL 回调后处理处统一更新风险状态。
  - 状态变化时输出一行日志。
- `[x]` Step 3：最小 UI 兼容
  - 页面状态文本读取 `risk_state`，展示 `CHECKING / ALERTING / COOLDOWN`。
  - 页面红色危险态跟随 `risk_state == ALERTING`，不再使用本地 2 秒 timer 维护另一套生命周期。
  - 若有文本展示需要避免 `DANGER` 显示为 `NONE`，只做最小修复。
- `[x]` Step 4：文档和验证
  - 更新本计划 Progress。
  - 必要时更新 `docs/context/CHANGELOG.md`。
  - 运行 context light/standard 验证。
  - 确认 IDF 环境后运行构建。

## Validation and Acceptance

- 计划运行的验证命令：
  - `uv run python scripts/context/validate_context.py --level standard --q "危险识别 状态机 danger_detection_service" --brief`
  - 确认 `export.ps1` 可用后运行 `idf.py build`
- 期望看到的结果：
  - context 验证无错误。
  - 固件构建通过。
  - 编译无未定义枚举、结构体字段或格式化错误。
- 当前实际结果：
  - `validate_context.py --level standard` 通过：错误 0，警告 0。
  - `idf.py build` 通过，生成 `build/111.bin`，当前 app 大小 `0x871990`，factory 分区剩余约 16%。
- 真机后续验收：
  - 串口能看到状态转移日志。
  - 普通人声不进入 `ALERTING`。
  - 真实 horn/siren/alarm 能在连续确认后进入 `ALERTING`。
  - 告警解除后能进入 `COOLDOWN` 并返回 `MONITORING`。

## Idempotence and Recovery

- 如果中途中断，下次从本文件 `Progress` 和 `git diff` 继续。
- 如果实现引入构建问题，最小回退路径：
  - 回退 `danger_detection_service.h/.c` 中新增状态机字段和 helper。
  - 保留本计划文档作为未完成设计。
- 如果真机行为变差：
  - 优先回退 `cooldown_ms` 行为，保留 `risk_state` 快照。
  - 不直接降低模型阈值；误报问题优先通过 hard negative 和高阈值策略处理。

## Progress

- `[x]` 计划书创建
- `[x]` 服务层状态枚举和快照字段
- `[x]` ESP-DL 后处理状态机
- `[x]` 最小 UI 兼容检查
- `[x]` 部署策略 profile
- `[x]` 冷却期强提醒抑制
- `[x]` 页面危险态跟随 service 状态机
- `[x]` 构建验证

## Decision Log

- 2026-05-12：本轮优先实现 service 层显式状态机，而不是继续换模型。
  - 原因：当前最大框架缺口是状态语义分散，模型已能先按高阈值路线工作。
- 2026-05-12：本轮不扩大 active danger 类别。
  - 原因：产品边界已收敛到 `siren / horn / alarm`，扩类应留给 challenger。
- 2026-05-12：实现时将服务生命周期 `state` 与危险提醒 `risk_state` 分开。
  - 原因：`STARTING / RUNNING / ERROR` 是运行时状态，`SUSPICIOUS / ALERTING / COOLDOWN` 是产品提醒状态，混成一个枚举会让 UI 和后台 owner 继续漂。
- 2026-05-12：复查后继续补 `deployment_profile_id / danger_class_profile` 和后处理参数 profile。
  - 原因：状态机已经落到 service，下一步应先把确认窗口、清除窗口、hold、cooldown 与 `siren / horn / alarm` 主线口径收成发布 profile，避免继续散落常量。
- 2026-05-12：复查发现冷却期分支顺序会允许连续 danger 在 cooldown 未结束时重新触发强提醒，已改为冷却期只累计证据、不触发 `app_alert_manager_raise()`。
  - 原因：`COOLDOWN` 的产品语义是短暂抑制重复强提醒；若证据持续到冷却结束，后续窗口再重新走正式告警逻辑。
- 2026-05-12：复查发现专页红色危险态仍由本地 2 秒 timer 和 `alert_sequence` 驱动，已改为直接跟随 `risk_state == ALERTING`。
  - 原因：屏幕危险态持久策略必须由服务层状态机统一驱动，UI 只读 snapshot，避免页面和全局提醒各自维护生命周期。

## Next Step

- 上板验证状态转移日志，并继续评估是否把专页启动模式推进为后台系统能力。
