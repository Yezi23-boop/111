---
id: safety-monitor-background-completion-plan-20260525
tags: plan, archived, watch, safety-monitor, danger-detection, background-service, resource-management, esp-dl
summary: 完整危险识别后台服务目标计划书，固定 Safety Monitor 后台能力的目标态、owner 分工、差距、实现阶段、subagent 复查和验收闭环。
status: archived
last_reviewed: 2026-05-25
memory_type: project_plan
scope: repo
owners: main/services/background_service_manager.c, main/services/background_service_manager.h, main/services/safety_monitor_session.c, main/services/safety_monitor_session.h, main/features/danger_detection/danger_detection_service.c, main/features/danger_detection/danger_detection_service.h, main/features/alerts/app_alert_manager.c, main/ui/custom/danger_detection_controller.c, components/espdl_inference, components/audio_codec
triggers: safety monitor, danger detection, background service, 完整危险识别, 后台服务, background_service_manager, safety_monitor_session, app_alert_manager, espdl
evidence_level: design
---

# Safety Monitor 后台服务完善计划

## Purpose / Big Picture

- 任务目标：把危险识别从“已后台化的薄骨架”继续完善成可解释、可验证、可复查的 Safety Monitor 后台服务。
- 为什么现在做：当前代码已具备 `background_service_manager -> safety_monitor_session -> danger_detection_service` 主链路，但完整产品态还需要把状态解释、文档映射、资源阻塞、持续提醒策略和验收证据收拢，避免后续 agent 重新把功能塞回页面或新增总管家。
- 完成后用户会看到什么变化：
  - 危险识别页只作为状态页和开关入口，退出页面后后台监听继续。
  - AI 对话、低电量、维护窗口、麦克风占用等场景会显示可解释状态。
  - 串口日志能解释 Safety Monitor 为什么启动、停止、阻塞、恢复和触发提醒。
  - 后续 OTA、低功耗或新提醒通道接入时，能按 owner 合同扩展，不重写框架。

## Source Context

- 运行时 owner 合同：`docs/context/knowledge/project/runtime-owner-contract.md`
- 危险提醒产品架构：`docs/context/knowledge/project/hearing-assist-danger-alert-system-architecture.md`
- 状态机和提醒策略：`docs/context/knowledge/project/hearing-assist-danger-alert-state-machine-and-notification-policy.md`
- 参数默认值：`docs/context/knowledge/project/hearing-assist-danger-alert-parameter-defaults-table.md`
- 固件映射：`docs/context/knowledge/project/hearing-assist-danger-alert-firmware-mapping.md`
- 已完成状态机计划：`docs/context/plans/completed/2026-05-12-danger-detection-state-machine-framework-plan.md`
- 已完成资源框架计划：`docs/context/plans/completed/2026-05-12-watch-resource-framework-plan.md`

## Current Baseline

当前已完成：

- `background_service_manager` 已保存 `安全监听` 用户开关，读取 `power_policy` budget 和 `audio_codec` input owner 快照，计算 Safety Monitor 是否应运行。
- `background_service_manager` 已等待 `ui_first_frame_ready`，避免 ESP-DL 模型和麦克风采集抢 UI 首帧资源。
- `safety_monitor_session` 已承接危险识别 runtime start/stop、ERROR 恢复、运行确认和失败退避。
- `danger_detection_service` 已发布运行状态和 `risk_state`，并实现 `MONITORING / SUSPICIOUS / ALERTING / COOLDOWN` 风险状态机。
- `app_alert_manager` 已负责 P0 危险提醒编排和普通音频 output 抢占。
- `danger_detection_controller` 已通过 `background_service_manager_set_danger_detection_enabled()` 表达用户开关，页面退出不再 stop runtime。
- `official_chat_service` 已通过 foreground audio 声明暂停 Safety Monitor，退出 AI 对话后按用户开关恢复。

当前主要缺口：

- `hearing-assist-danger-alert-firmware-mapping.md` 仍有旧描述，提到页面进入 start、离开 stop，需要更新为当前后台化事实。
- `background_service_manager` 对外快照还缺少更明确的目标态和阻塞原因字段；UI 目前从多个布尔值拼状态，后续增加 OTA/低功耗时解释力不足。
- Safety Monitor 的阻塞/恢复验收主要依赖日志和 source test，还缺少“目标态/阻塞原因/最近策略原因”的统一可观测口径。
- 持续提醒、事件记录、震动优先仍属于 hearing-assist 产品后续 gate；当前硬件没有 haptic driver，不能假装已完成。

## Owner Boundary

- `background_service_manager`
  - 负责用户开关、policy 许可、麦克风阻塞、前台音频声明和 Safety Monitor `should_run` 合成。
  - 不负责模型后端、风险状态机、提醒策略或音频 session 实现。
- `safety_monitor_session`
  - 负责把 `should_run` 翻译成 `danger_detection_service_start/stop`，并处理 ERROR 恢复、退避和运行确认。
  - 不解释用户开关、power budget、麦克风优先级或 UI 文案。
- `danger_detection_service`
  - 负责 ESP-DL 结果到公共风险状态机的融合，发布 `risk_state`、profile、置信度和告警序号。
  - 不负责是否后台运行、不直接控制 UI 页面生命周期。
- `app_alert_manager`
  - 负责提醒编排、P0 output 抢占、overlay/audio 当前 fallback。
  - 不判断模型阈值，不维护连续窗口。
- `danger_detection_controller`
  - 只负责页面创建、用户开关、状态展示。
  - 不拥有 start/stop 生命周期。
- `audio_codec`
  - 负责 input/output session owner 和快照。
  - 不判断 Safety Monitor 策略。

## Scope / Non-Goals

本轮明确要做：

- 写清 Safety Monitor 后台服务目标计划和当前差距。
- 用 subagent 复查框架 owner 和嵌入式资源风险。
- 更新过期固件映射，让文档反映当前后台化实现。
- 补强后台服务可观测性，让快照能直接说明目标态和阻塞原因。
- 增加 source test 锁定 UI 不直接 start/stop、manager 不接管 runtime 细节、资源阻塞可解释。
- 按需完善最小代码，不新增总管家。

本轮明确不做：

- 不换模型，不重新训练，不扩大 active danger 类别。
- 不把 `glass_break / crash / impact` 并入 active 主线。
- 不接入真实 haptic driver；当前只能保留震动优先的产品后续 gate。
- 不做 NVS 持久化开关，除非后续用户明确要求。
- 不做 full standby/light sleep；低功耗只按现有 `power_policy` 预算消费。
- 不新增大而全 `ResourceManager`、`resource_policy`、`session_router` 或默认 `ui_manager`。

## Target Behavior

### 正常开启

```text
危险识别页打开
  -> 用户打开 安全监听
  -> background_service_manager 记录 enabled_by_user=true
  -> 等 ui_first_frame_ready + power_policy 允许 + 麦克风空闲
  -> safety_monitor_session 启动 danger_detection_service
  -> espdl_runtime 申请 AUDIO_CODEC_OWNER_ESPDL_INFERENCE input session
  -> 3s 推理心跳持续输出
```

### 页面退出

```text
危险识别页退出
  -> UI 只返回主界面
  -> 不调用 danger_detection_service_stop()
  -> 后台 Safety Monitor 按用户开关继续运行
```

### AI 对话前台

```text
进入 AI 对话页
  -> official_chat_service 声明 foreground_audio_active=true
  -> background_service_manager 发布 mic blocked
  -> safety_monitor_session 停止 danger_detection_service
  -> audio_codec input session 释放给 official_chat

退出 AI 对话页
  -> foreground_audio_active=false
  -> 若安全监听开关仍开启且 policy 允许
  -> Safety Monitor 自动恢复
```

### 维护窗口 / 低电量

```text
power_policy 不允许 danger_detection
  -> background_service_manager 发布 policy blocked
  -> safety_monitor_session 停止或保持停止
  -> UI 显示 低电量降级 / 维护中暂停
  -> 用户开关不丢失
```

### 风险状态机

```text
MONITORING
  -> SUSPICIOUS
  -> ALERTING
  -> COOLDOWN
  -> MONITORING
```

- 单窗 danger 不直接强提醒。
- 连续 danger 才进入 `ALERTING`。
- hold + clear windows 后进入 `COOLDOWN`。
- `app_alert_manager` 只在正式 `ALERTING` 时组织提醒。

## Implementation Gates

### Gate 1: 文档与当前实现对齐

- `[x]` 更新 `hearing-assist-danger-alert-firmware-mapping.md`：
  - `background_run_allowed` 改为已实现。
  - `ui_page_required` 改为不依赖专页常驻。
  - `danger_detection_controller` 描述改为状态页和开关入口。
  - 保留持续提醒、事件记录、haptic 未实现。
- `[x]` 更新 `CHANGELOG.md`。
- `[x]` 跑 context standard。

### Gate 2: 后台服务可观测性补强

- `[x]` 为 `background_service_manager_snapshot_t` 补充目标态和阻塞原因：
  - `danger_should_run`
  - `danger_block_reason`
  - 可选 `blocking_audio_owner` 本轮暂不加，避免把 manager 扩成音频仲裁器。
- `[x]` 日志保持简洁，状态变化才打印，不每秒刷屏。
- `[x]` UI 状态文案优先消费 manager snapshot，不重复推导底层组合。
- `[x]` 补 source test 锁定阻塞原因和 UI 展示。

### Gate 3: 风险状态机和提醒层复查

- `[x]` 确认 `danger_detection_service` 的 profile、hold、cooldown、confirm/clear windows 与参数表一致。
- `[x]` 确认 `app_alert_manager` 不判断模型阈值、不维护连续窗口。
- `[x]` 确认 AI 前台期间 Safety Monitor 暂停规则只在 background manager / official_chat service 生效，不在提醒层重复判断 speaking 状态。

### Gate 3.5: 嵌入式安全复查 P1 修复

- `[x]` 修复 ESP-DL stop timeout 状态一致性：
  - `espdl_audio_runtime_stop()` 超时后不假装资源已清；若任务稍后退出，下一次 stop 或 start 前会补做 codec deinit 和 model runner destroy。
  - `danger_detection_service_stop()` 在底层 stop 失败时不清 `runtime_started / callback_registered`，并发布 ERROR。
  - `safety_monitor_session` 在 stop 失败时继续把 runtime 视为占用，后续 policy tick 可继续重试 stop。
- `[x]` 修复 `app_alert_manager / audio_alert_player` 跨任务共享状态：状态读写进入临界区，显示/音频调用保持在锁外；`clear()` hide 成功后再提交 inactive，`raise()` 使用 generation 丢弃过期 show。
- `[x]` 修复 stop/暂停期间 stale ESP-DL callback 可重新触发提醒：callback 在 raise/clear 前二次确认 service 仍允许提交提醒，stop 成功后再次兜底 clear。
- `[x]` 修复 ESP-DL 推理循环内 `pcm_float` 反复动态分配：任务启动时一次性分配，推理窗口内复用。
- `[x]` 补板端验收脚本：`scripts/board/validate_safety_monitor_board.ps1` 可列串口、可选 flash+monitor 抓日志，并自动汇总启动、推理、AI 前台暂停/恢复和 `ALERTING` 关键日志模式。
- `[ ]` 补真实板端长期运行堆碎片证据。

### Gate 4: 验证闭环

- `[x]` 运行 source tests：
  - `uv run python -m pytest tests/test_audio_codec_port_source.py tests/test_espdl_single_model_runtime_source.py tests/test_danger_detection_service_source.py tests/test_safety_monitor_session_source.py tests/test_danger_detection_controller_source.py tests/test_power_integration_source.py tests/test_official_chat_service_source.py`
- `[x]` 运行 context：
  - `uv run python scripts/context/validate_context.py --level standard --q "Safety Monitor 后台服务 danger_detection background_service_manager safety_monitor_session" --brief`
- `[x]` 确认 `export.ps1` 可用后运行 `idf.py build`。
- `[ ]` 真机验收：
  - 开启安全监听后 3s 推理日志持续。
  - 退出危险识别页后推理日志继续。
  - 进入 AI 对话页 Safety Monitor 停止或 resource blocked。
  - 退出 AI 对话页 Safety Monitor 恢复。
  - 普通人声不进入 `ALERTING`。
  - horn/siren/alarm 连续确认后进入 `ALERTING`。

## Subagent Review Plan

- `framework_reviewer`
  - 复查 owner 分工是否偏离 `runtime-owner-contract`。
  - 检查是否过度抽象或隐性总管家化。
  - 输出最小实现建议。
- `embedded_safety_reviewer`
  - 复查 FreeRTOS、UI 首帧 gate、audio session、ESP-DL start/stop、P0 alert output、power budget 风险。
  - 输出 P0/P1/P2 风险和验证建议。
- 主 agent
  - 整合 subagent 结果。
  - 只做与本计划一致的最小代码和文档改动。
  - 最后跑 source tests、context 和 build。

## Validation and Acceptance

完成标准：

- 文档层：
  - 当前目标态、owner、缺口、禁止项和后续 gate 已写入 active plan。
  - 固件映射不再误导后续 agent 认为页面退出会停止服务。
- 代码层：
  - UI 不直接 start/stop runtime。
  - manager 只合成 should_run 和解释阻塞。
  - session 只执行 lifecycle。
  - service 只管理风险状态机。
  - alert manager 只管理提醒编排。
- 验证层：
  - 相关 source tests 通过。
  - context standard 通过。
  - IDF build 通过。
  - 至少列出板端验收日志要求；若本轮有串口环境，再补真实日志证据。
- 复查层：
  - 至少一个框架复查结果和一个嵌入式风险复查结果被整合。
  - 若有 P0/P1 问题，必须修复或记录阻塞。

## Progress

- `[x]` 检索 runtime owner contract、hearing-assist danger docs 和历史 attempt。
- `[x]` 初步核对当前代码主链路。
- `[x]` 创建本目标计划书。
- `[x]` subagent 框架复查完成：结论为 owner 链路符合 `runtime-owner-contract`，无总管家化阻断。
- `[x]` subagent 嵌入式风险复查完成：未发现已坐实 P0；P1/P2 风险记录到后续 gate。
- `[x]` 文档映射更新。
- `[x]` 当前 Safety Monitor 后台骨架确认并写入 `runtime-owner-contract.md`。
- `[x]` 后台服务可观测性补强：`danger_should_run / danger_block_reason` 已接入 manager snapshot、状态变化日志和 UI 文案。
- `[x]` P1 stop timeout 状态一致性修复。
- `[x]` P1 提醒层共享状态保护和 ESP-DL 热路径分配收敛。
- `[x]` source tests / context / build 验证：最终本地验证 40 项 source tests、context standard、`git diff --check` 和 `idf.py build` 均通过；`111.bin` 大小 `0x8d76c0`，factory 剩余 `0x128940`（12%）。
- `[x]` 最终框架骨架复查完成：当前 owner 链路可作为后续 agent 写代码的固定骨架；板端长期运行证据仍作为独立验收项保留。
- `[x]` 最终 framework subagent 复查完成：无 P0/P1，结论为“框架可验收”；P2 是 runtime owner contract、completion plan、attempt log 仍需纳入版本控制才可约束 clean checkout。

## Decision Log

- 2026-05-25：继续沿用小 owner 协作，不新增总管家。
  - 原因：当前大厂式对齐点是 `policy + session + resource owner + service`，不是全能 `ResourceManager`。
- 2026-05-25：本轮先补后台服务可观测性，而不是先做 haptic 或事件日志。
  - 原因：当前硬件未确认 haptic driver；事件日志和持续提醒需要先有稳定的后台状态解释。
- 2026-05-25：`background_service_manager` 仍只管理 Safety Monitor。
  - 原因：新后台能力接入需要第二个真实能力和预算字段，不能提前变成通用调度器。
- 2026-05-25：Safety Monitor 后台骨架按当前 owner 链路确认成立。
  - 原因：框架复查未发现 owner 偏离或过度抽象；嵌入式复查发现的问题属于 stop 超时、共享状态和长期运行质量 gate，不改变当前骨架。
- 2026-05-25：后台服务可观测性只补目标态与阻塞原因，不补具体 blocking audio owner 字段。
  - 原因：当前 UI 和日志已能解释 user/policy/foreground_audio 三类主阻塞；具体 owner 仍可从 `audio_codec` session snapshot 读，先避免扩大 manager 职责。
- 2026-05-25：优先修 stop timeout 的状态一致性，而不是先改 alert manager 并发。
  - 原因：stop timeout 会影响 Safety Monitor 暂停/恢复和麦克风释放判断，是后台服务生命周期的直接 P1；alert manager 并发属于提醒层下一步质量 gate。
- 2026-05-25：提醒层共享状态只加状态锁，不把显示/音频调用放进锁。
  - 原因：锁只保护 `active / active_request / playing / task_handle` 等共享事实；LVGL pending、mp3 stop、audio output session 可能阻塞或跨 owner，必须在锁外执行。
- 2026-05-25：`app_alert_manager_clear()` 采用“先 hide，成功后提交 inactive”的顺序。
  - 原因：如果 hide 失败，内部状态必须保持 active，方便后续 clear 重试，不留下 overlay 与状态不一致。
- 2026-05-25：ESP-DL result callback 在提交 app alert 前二次检查 service 状态。
  - 原因：回调可能在 stop 前已经进入函数，必须在锁外 raise/clear 前重新确认没有进入 STOPPING/ERROR，避免关闭安全监听或 AI 前台抢麦时补出过期提醒。
- 2026-05-25：ESP-DL 热路径先只移走 `pcm_float` 每窗分配。
  - 原因：这是复查指出的明确反复分配点；更彻底的 PSRAM allocator 需要单独评估 ESP-DL 内部内存策略和板端长期运行证据。

## Surprises & Discoveries

- 2026-05-25：`framework_reviewer` 确认骨架符合 `runtime-owner-contract`，主要剩余缺口是 hearing-assist 产品能力而非框架 owner。
- 2026-05-25：`embedded_safety_reviewer` 未发现已坐实 P0，但指出 ESP-DL stop timeout、alert manager 共享状态和推理循环动态分配需要后续单独处理。
- 2026-05-25：`danger_block_reason` 用来解释目标态阻塞，`last_error` 继续解释 runtime 启停异常；两者分开能避免 UI 把策略暂停和真正故障混在一起。
- 2026-05-25：ESP-DL runtime 的资源释放分成两段：后台任务退出点释放 input session，stop 线程在确认 task_handle 为空后 deinit codec 和销毁模型；若首次 stop 超时，后续 stop/start 会补清第二段资源。
- 2026-05-25：`display_alert_adapter_show/hide` 本身只写 pending 标志，`app_alert_manager` 在锁外调用它不会直接触碰 LVGL 对象；`display_alert_adapter_set_suppressed()` 仍应保持 UI 线程调用边界。
- 2026-05-25：stop 成功后再次调用 `app_alert_manager_clear()` 是有意冗余，作用是清掉可能在 stop 期间进入但被二次检查拦截前后的 pending 告警残留。

- `hearing-assist-danger-alert-firmware-mapping.md` 的部分描述已落后于当前代码：页面已经不直接 start/stop，Safety Monitor 已由后台 manager 接管。
- 当前代码已具备风险状态机 profile，比旧 firmware mapping 记录更完整。

## Idempotence and Recovery

- 如果中途中断，下次从本文件 `Progress` 和 `git diff` 继续。
- 如果代码补强失败，最小回退路径：
  - 回退 `background_service_manager` snapshot 扩展。
  - 保留本计划和 firmware mapping 更新，作为下一轮继续依据。
- 如果真机行为变差，优先回退后台可观测性代码改动，不回退已稳定的 owner 合同。

## Next Step

- 后续 agent 写 Safety Monitor、低功耗、OTA、音频仲裁或新后台能力时，先读 `runtime-owner-contract.md` 的已确认骨架，沿用当前小 owner 协作链路，不从代码现状重新发明框架。
- 剩余未完成项是板端长期运行验收：当前机器只枚举到 `COM1`，未发现 ESP32-S3 开发板串口；设备重新连接后运行 `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\board\validate_safety_monitor_board.ps1 -Port <COMx>`，再采集长时间安全监听、页面退出后继续推理、AI 前台暂停/退出恢复、普通人声不误报、horn/siren/alarm 连续确认后进入 `ALERTING` 等日志。
