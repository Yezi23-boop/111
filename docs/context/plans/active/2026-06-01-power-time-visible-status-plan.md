---
id: power-time-visible-status-plan-20260601
tags: plan, active, watch, power, time, ui, low-battery, observability
summary: 在不能拔电源的约束下，分阶段推进整机电源管理：先可观测，再做运行态 STANDBY，再以新低功耗框架整理 dry-run 和后续 sleep 证据。
status: active
last_reviewed: 2026-06-01
memory_type: project_plan
scope: repo
owners: main/services/power_policy.c, main/services/power_service.c, main/ui, components/system_time, main/services/system_time_service.c
triggers: LOW_BATTERY_WARN, power_policy, system_time, 电源状态, 时间状态, STANDBY 前置, low power framework
evidence_level: design
---

# 整机电源管理阶段执行计划

## Purpose / Big Picture

当前 `system_time` 已完成 RTC bootstrap、SNTP 写回 RTC 和一次断电保持上板验证；`power_policy` 也已经能发布运行态预算。下一步可以跨大一点，但不能跳过可观测性和运行态 STANDBY：

- 当前时间来自 RTC、SNTP 还是业务服务器。
- 当前 RTC 是否可信，`OS` 是否为 0。
- 当前电源来自外部供电还是电池。
- 当前是否低电量预警。
- 当前 `power_policy` 为什么进入某个状态。

本计划的目标是从“能看到状态”推进到“能进入不 sleep 的待机态”，再和 `low-power-framework-architecture.md` 对齐，整理后续 dry-run / sleep 证据。执行顺序是：

```text
Phase 1: 可视化与摘要日志
  -> Phase 2: STANDBY 运行态省电，不进 ESP sleep
  -> Phase 3: sleep dry-run / 外部唤醒增强证据整理
  -> Phase 4: timer-based Light Sleep / Deep Sleep 显式实验
```

本轮可以把 Phase 1 和 Phase 2 作为主要实现目标，Phase 3/4 作为设计和前置证据，不因为当前不能拔电源而阻塞。

## Current Constraint

用户当前不能拔电源，因此本计划降低“真实断电/真实低电量”的验收权重，但不降低软件结构要求：

- 不要求真实拔 USB / 电池。
- 不要求把电池放到真实低电量。
- 不要求证明过夜 RTC 保持。
- 不做极低电量自动关机。
- 不进入 ESP Light Sleep / Deep Sleep。

本轮允许使用：

- source tests。
- 编译验证。
- 串口日志。
- 可控调试注入或测试替身模拟 `LOW_BATTERY_WARN`。
- 当前外部供电/充电状态下的正常路径观察。

真实断电、真实低电量、过夜保持作为后续非阻塞验收项。STANDBY 阶段允许在外部供电状态下验证“策略动作是否正确”，不要求证明实际电流下降幅度。

## Source Context

- `docs/context/knowledge/project/runtime-owner-contract.md`
- `docs/context/knowledge/project/low-power-management-baseline.md`
- `docs/context/knowledge/project/ui-refresh-policy-idle-dim.md`
- `docs/context/runs/2026-06-01-attempt-system-time-rtc-power-cycle-validation.md`
- `docs/context/plans/active/2026-05-30-system-time-rtc-sntp-owner-plan.md`

## Owner Boundary

### `power_service` / `board_power`

负责提供只读电源事实：

- 外部供电是否存在。
- 电池是否存在。
- 是否充电。
- `VBAT / VSYS / SOC`。
- 数据是否 stale。

不负责 UI 文案、告警展示、后台任务策略。

### `power_policy`

负责把电源事实合成为整机预算：

- `ACTIVE`
- `CHARGING`
- `LOW_BATTERY_WARN`
- `MAINTENANCE`
- `STANDBY`

不直接操作 LVGL、面板亮度、Wi-Fi、音频、模型或具体后台任务。

本计划不再保留 `IDLE_DIM` 作为产品级状态。短时间不动后的降亮/降刷新如果仍存在，只能作为 `STANDBY` 内部实现细节或迁移阶段兼容路径，后续目标状态是 `ACTIVE -> STANDBY -> ACTIVE`。

`STANDBY` 的进入/退出不是 `power_policy` 独占判断：

- UI/input owner 发布只读 activity snapshot，例如最近触摸、按键、强制活跃时间。
- `power_policy` 读取 activity snapshot 和电源事实，合成 `STANDBY` 预算。
- `ui_refresh_policy`、`background_service_manager`、`network_service` 等各自消费预算并执行本 owner 的降级/恢复动作。
- `power_policy` 不直接管理触摸、按键、LVGL 刷新循环、面板亮度、Wi-Fi 或后台任务。

### `system_time` / `system_time_service`

负责提供时间事实：

- 时间是否可信。
- 当前来源：`NONE / RTC / SNTP / SERVER`。
- RTC 是否存在。
- RTC `OS` 是否停振。
- 最近一次 RTC 写回是否成功。

不负责 UI 展示和电源策略。

### UI 层

负责展示状态和用户反馈：

- 可选的电源/时间调试摘要。

不直接读 AXP2101 / PCF85063ATL 寄存器，不直接启动 SNTP，不直接改 `power_policy`。

## Scope / Non-Goals

本轮要做：

- 补 `system_time` 启动摘要日志。
- 补 SNTP 同步后 RTC/SNTP drift 统计日志。
- 让 `LOW_BATTERY_WARN` 在不拔电源条件下可通过日志、source test 或调试注入验证。
- 设计并实现第一版 `STANDBY` 运行态省电，不进入 ESP sleep：
  - 屏幕灭屏或接近灭屏。
  - LVGL 主循环更低频。
  - Wi-Fi 进入更省电模式或暂停非关键同步。
  - 暂停可暂停后台任务。
  - 触摸/按键恢复到 `ACTIVE`。
- 增加 source tests 锁 owner 边界。
- 更新 context 证据。

本轮不做：

- 不做真实低电量放电测试。
- 不做极低电量自动关机。
- 不进 `esp_light_sleep_start()`。
- 不进 Deep Sleep。
- 不承诺当前阶段的实际电流指标。
- 不把 RTC_INT / AXP_IRQ 当正式 wake source 用于 sleep。
- 不新增大而全 `ResourceManager`、`ui_manager` 或通用事件总线。

## Target Behavior

### Boot Summary

开机后应能看到一行摘要，避免人工拼多行日志：

```text
system_time_boot: rtc_present=1 os=0 source=RTC sys_valid=1 rtc=2026-06-01 01:20:19
```

若 RTC 停振：

```text
system_time_boot: rtc_present=1 os=1 source=NONE sys_valid=0 reason=rtc_oscillator_stopped
```

### SNTP Drift Summary

SNTP 成功后应能看到：

```text
system_time_sync: source=SNTP rtc_writeback=1 drift_sec=2
```

解释：

- `drift_sec` 是 SNTP 校准前 RTC 估算时间与 SNTP 成功后系统时间的差值。
- 后续长时间断电保持验证靠该值判断 RTC 走时质量。

### Low Battery Flag / Log

当 `power_policy` 发布 `LOW_BATTERY_WARN`：

```text
power_policy: budget_flags_changed low_battery_warn=1 soc=18 vbat=...
```

本轮不关机、不 sleep、不弹 UI、不改变预算。后续如果需要用户可见提示，由 UI/alert owner 消费 `LOW_BATTERY_WARN` 的边沿变化另开任务。

### STANDBY Runtime Mode

用户无交互达到阈值后进入 `STANDBY`，但 CPU 仍正常运行：

```text
ui inactive for 30 seconds
  -> power_policy state=STANDBY
  -> ui_refresh_policy: screen_off_or_min_brightness=1
  -> network/background: non_critical_tasks_paused=1
  -> touch/button event
  -> power_policy state=ACTIVE
  -> ui_refresh_policy: restore brightness
```

第一版 `STANDBY` 的定义：

- 是运行态待机，不是 ESP sleep。
- 是唯一的空闲省电产品状态，不再单独保留短暂不动变暗的 `IDLE_DIM` 层。
- 不改变 RTC/PMIC 供电轨。
- 不关闭 I2C。
- 不关闭 LVGL task，只降低节奏和屏幕输出。
- 第一版屏幕策略是逐渐变暗到最低亮度或 0 亮度，不调用 CO5300 sleep-in、不反初始化 panel、不关闭显示供电轨。
- 第一版网络策略不主动断开 AP，不销毁 IP / MQTT / HTTP 状态；只允许 Wi-Fi owner 进入省电配置或暂停非关键同步。
- P0 危险提醒是 `STANDBY` 强制退出条件，不能被省电预算压制。
- 普通 `STANDBY` 不暂停 Safety Monitor；否则 P0 危险提醒没有证据来源。低电量、维护窗口或用户显式关闭可按既有预算另行限制危险识别。
- 第一版进入阈值使用 30 秒无交互，方便上板观察；后续正式体验可在不改变 owner 边界的前提下调长。
- 第一版默认开启 `STANDBY` 触发，若上板发现恢复不稳定，再关闭触发条件回退。
- 不影响手动进入 AI 对话页后的前台音频 owner。

## Validation Without Power Unplug

由于当前不能拔电源，本轮验收改为：

### Required

- `uv run python -m unittest tests.test_power_integration_source tests.test_system_time_owner_source ...`
- `uv run python scripts/context/validate_context.py --level standard --q "LOW_BATTERY_WARN power_policy system_time status low power framework" --brief`
- `idf.py build`
- 串口观察普通外部供电路径：
  - `power_policy` 仍进入 `CHARGING` 或当前真实状态。
  - `system_time_boot` 摘要出现。
  - SNTP 成功后 `system_time_sync` 摘要出现。
  - 若开启 STANDBY 调试触发，能看到 `ACTIVE -> STANDBY -> ACTIVE` 日志。

### Simulated / Source-Level

- source test 证明 `LOW_BATTERY_WARN` 不直接由 UI 判断。
- source test 证明 `LOW_BATTERY_WARN` V1 只作为 flag/log，不触发 UI、不改预算、不进入 sleep。
- 可选增加测试注入接口或测试替身，模拟：

```text
battery_data_valid=1
external_power_present=0
charging=0
battery_percent<=20
```

预期得到：

```text
activity_state=ACTIVE 或 STANDBY
flags=LOW_BATTERY_WARN
low_battery_warn=1
budget_unchanged_by_low_battery=1
network_sync_allowed=unchanged
ui_high_refresh_allowed=unchanged
sleep_permission=unchanged
```

### Deferred

- 真实拔电源。
- 真实低电量放电。
- 10 分钟 / 1 小时 / 过夜 RTC 保持。
- Light Sleep / Deep Sleep 唤醒。
- 实际电流下降数据。

## Implementation Gates

### Gate 1: System Time Observability

- `[x]` 增加 `system_time` source 文本转换 helper：`NONE / RTC / SNTP / SERVER`。
- `[x]` 开机 RTC bootstrap 完成后输出 `system_time_boot` 摘要。
- `[x]` SNTP 成功后输出 `system_time_sync` 摘要。
- `[x]` 增加 drift 计算，单位为秒。
- `[x]` source test 锁定 `esp_sntp_*` 仍只在 `components/system_time` 内。

### Gate 2: Low Battery Warning Contract

- `[x]` 确认 `power_policy` 的 `LOW_BATTERY_WARN` 仍只由电源快照合成。
- `[x]` 明确低电量阈值来源：当前 `20%` 作为第一版预警点。
- `[x]` 不增加自动关机。
- `[x]` 不进入 sleep。
- `[x]` source test 锁定低电量状态只发布可见提示事实，不顺手禁止普通网络同步或 UI 高刷。

### Gate 3: UI Visible Prompt, Deferred

- `[ ]` 当前 V1 不做低电量 UI 提示。
- `[ ]` 后续如需提示，必须由 UI/alert owner 消费 `LOW_BATTERY_WARN` 边沿变化，不由 `power_policy` 直接弹 UI。
- `[ ]` 后续提示不得抢占 P0 危险提醒；危险提醒优先级高于低电量提示。

### Gate 4: No-Unplug Validation

- `[x]` 用 source tests 验证模拟低电量路径。
- `[x]` 用当前外部供电状态上板观察普通路径不回归。
- `[x]` 记录不能拔电源的验收限制。
- `[x]` 把真实断电/低电量测试列入后续，不阻塞本轮合并。

### Gate 5: STANDBY Runtime Policy

- `[x]` UI/input owner 提供只读 activity snapshot，作为 `STANDBY` 进入/退出事实来源。
- `[x]` `power_policy` 读取 activity snapshot 和电源事实后发布 `STANDBY` 预算，第一版使用 30 秒无交互阈值。
- `[x]` 第一版默认开启 `STANDBY` 触发，并保留关闭触发条件的回退方式。
- `[x]` `power_policy` 只发布 `STANDBY` 预算，不直接操作屏幕、Wi-Fi 或后台任务。
- `[x]` `ui_refresh_policy` 或 UI owner 消费 `STANDBY` 预算，执行渐进变暗到最低/0 亮度和更低刷新。
- `[x]` 第一版不调用 CO5300 sleep-in、不反初始化 panel、不关闭显示供电轨，降低恢复风险。
- `[x]` `background_service_manager` 消费 `STANDBY` 预算；当前没有除 Safety Monitor 外的可暂停后台任务，Safety Monitor 按安全约束保持运行，非关键网络同步由 `network_service` 降级。
- `[x]` 普通 `STANDBY` 不暂停 Safety Monitor，确保 P0 危险提醒仍可触发强制退出。
- `[x]` `network_service` 或 Wi-Fi owner 消费预算，进入省电或暂停非关键同步；第一版不主动断开 AP。
- `[ ]` 触摸/按键能退出 `STANDBY`。（无人验证未覆盖，需后续手动触摸或调试触发补证据）
- `[x]` P0 危险提醒能强制退出 `STANDBY` 并恢复 UI 可见提示。
- `[x]` source tests 锁定 `power_policy` 不直接调用 LVGL、panel、Wi-Fi、audio、model。

### Gate 6: Wake Evidence Preparation

- `[ ]` 将 `sleep_permission / sleep_blockers / interval_hint` 整理为 dry-run 可观测输出。
- `[ ]` 明确 V1 sleep 主路径是 ESP32-S3 internal RTC timer，`RTC_INT(GPIO39)` 和 `AXP_IRQ/EXIO5` 只是后续外部唤醒增强。
- `[ ]` 保留 Light Sleep 默认关闭。
- `[ ]` 设计手动触发入口和本地结果快照，不自动开机入 sleep。

### Gate 7: Manual Sleep Experiment, Deferred

- `[ ]` 准备明确 USB/JTAG 失联恢复路径；外部 UART 只是增强，不是 V1 前置要求。
- `[ ]` 手动触发 timer-based Light Sleep。
- `[ ]` 记录 `wake_cause / elapsed_ms / interval_hint / 可选 GPIO39 / RTC Control_2`。
- `[ ]` Deep Sleep 另按 timer-based cold boot resume 计划推进，不要求先闭环外部唤醒 GPIO。

## Risks

- 若 UI 直接读 `board_power` 或 `axp2101`，会破坏 owner 边界。
- 若 `app_alert_manager` 开始判断电池百分比，会把领域策略混入提示层。
- 若后续低电量提示复用危险提醒红色 P0 overlay，可能误导用户，也可能抢占真正危险提醒；V1 不做低电量 UI 可见提示。
- 若为了验证低电量而强行拔电源或放电，会增加板端风险；本轮明确不要求。
- 若 STANDBY 第一版直接关闭太多 owner，可能造成 UI、Wi-Fi、音频恢复路径混乱；第一版只做运行态省电，不碰 sleep。
- 若 `power_policy` 直接操作资源，会破坏 `runtime-owner-contract`。
- 若当前外部供电下强行证明电流收益，结论会不可靠；本阶段只验证行为和状态链路。

## Rollback

- `system_time` 摘要日志可独立回退，不影响授时主链路。
- 低电量 UI 提示已移出 V1；保留 `LOW_BATTERY_WARN` flag/log。
- 若 UI 展示不稳定，先保留日志和 source tests，不继续推进 STANDBY。
- 若 STANDBY 恢复不稳定，保留 Phase 1 可观测性和 `LOW_BATTERY_WARN` flag/log，关闭 STANDBY 触发条件。

## Done Definition

本计划完成时应满足：

- 日志能解释当前时间来源和 RTC 状态。
- 日志能解释 SNTP 与 RTC 的 drift。
- `LOW_BATTERY_WARN` 有 flag/log 路径。
- 不拔电源也能用测试或注入验证低电量 flag/log 链路。
- `STANDBY` 运行态省电路径有明确 owner 和可选调试触发。
- 不进入 ESP sleep 的前提下，能验证 `ACTIVE -> STANDBY -> ACTIVE` 恢复。
- 构建通过，context check 通过。
- 后续真实断电/真实低电量测试作为单独板端验收，不阻塞本阶段。

## Stage Roadmap

### Stage A: Make State Explainable

目标：看得懂当前设备为什么处于某个电源/时间状态。

完成物：

- `system_time_boot`。
- `system_time_sync drift_sec`。
- `LOW_BATTERY_WARN` flag/log。
- source tests 和当前供电日志。

### Stage B: Runtime STANDBY

目标：不进 ESP sleep，但先做到“像手表待机”。

完成物：

- 长空闲进入 `STANDBY`。
- 屏幕最低亮或灭屏。
- UI 刷新降频。
- 非关键网络/后台任务暂停。
- 触摸/按键/提醒恢复。

### Stage C: Sleep Dry-Run And Wake Evidence

目标：把未来 sleep 要依赖的 dry-run、timer 唤醒和外部唤醒增强准备成证据链。

完成物：

- `sleep_permission / sleep_blockers / interval_hint` dry-run 证据。
- ESP32-S3 internal RTC timer 手动 sleep 测试设计。
- `RTC_INT(GPIO39)` 和 `AXP_IRQ/EXIO5` 作为后续外部唤醒增强证据。

### Stage D: Sleep

目标：只在证据和恢复路径清楚后做。

完成物：

- timer-based Light Sleep 手动实验。
- wake cause 本地快照。
- 恢复 UI/I2C/Wi-Fi/time。
- timer-based Deep Sleep 另按 cold boot resume 计划推进，不和 Light Sleep 混做。

## Progress

- `[x]` system_time 启动摘要、SNTP drift、STANDBY 运行态省电和 Wi-Fi budget 消费已实现并板端验证。
- `[x]` `LOW_BATTERY_WARN` V1 收敛为 flag/log，不做 UI 提示或 sleep 动作。
- `[x]` 后续 sleep dry-run 已转入 `low-power-framework-execution-plan`。
- `[ ]` 真实低电量、触摸退出 STANDBY 和真实 sleep 仍未在本计划闭环。

## Decision Log

- 决策：V1 不保留 `IDLE_DIM` 作为产品主状态，收敛为 `ACTIVE / STANDBY`。
- 决策：低电量 V1 只发布 flag/log，不弹 UI、不禁止普通网络同步、不触发 sleep。
- 决策：普通 STANDBY 不暂停 Safety Monitor，P0 危险提醒必须能唤醒 UI。

## Validation and Acceptance

- source tests、context standard、`idf.py build` 和 COM3 板端日志已完成。
- 关键证据见 `docs/context/runs/2026-06-01-attempt-standby-power-time-v1-board-validation.md`。

## Idempotence and Recovery

- 若 STANDBY 恢复路径不稳定，先关闭 STANDBY 触发条件，保留 system_time 与 low-battery 可观测性。
- 若低电量 UI 后续恢复，应作为 UI/alert owner 独立需求，不从 `power_policy` 直接弹 UI。

## Next Step

- 后续优先补手动触摸/按键退出 STANDBY 证据，再考虑真实低电量或 Automatic Light-sleep。
