---
id: light-sleep-readiness-framework-plan-20260601
tags: plan, superseded, watch, power, low-power, light-sleep, rtc, pmic, wakeup, framework
summary: 已被 low-power-framework-execution-plan-20260601 替代；保留为上一版讨论记录，后续执行以 power_budget + sleep_coordinator 计划为准。
status: superseded
last_reviewed: 2026-06-01
memory_type: project_plan
scope: repo
owners: main/services/power_policy.c, main/services/wakeup_evidence_service.c, main/services/background_service_manager.c, main/services/network_service.c, main/ui/ui_refresh_policy.c, components/pcf85063atl, components/axp2101, components/wifi_control
triggers: Light Sleep, Deep Sleep, STANDBY, internal RTC timer, RTC_INT, AXP_IRQ, PCF85063ATL, wakeup_evidence, power_policy, sleep_coordinator, low power framework, 低功耗框架, 睡眠就绪
evidence_level: design
superseded_by: docs/context/plans/active/2026-06-01-low-power-framework-execution-plan.md
garden_status: keep-history
garden_reviewed: 2026-06-07
---

# Light Sleep Readiness 框架计划

> Superseded: 后续执行以 `docs/context/plans/active/2026-06-01-low-power-framework-execution-plan.md` 为准。本文件保留为上一版讨论记录，不再作为执行主线。

## Purpose / Big Picture

当前项目已经完成运行态 `STANDBY`：屏幕渐进降亮到 0%、LVGL 降频、网络进入省电预算、非关键网络同步暂停，且用户已确认恢复路径没有问题。下一步不能直接把 `esp_light_sleep_start()` 接进正常启动链路，而是先把“预算是否允许 sleep、sleep coordinator 是否 dry-run 可观测、醒后如何恢复资源”这三件事拆开。

本计划的目标是建立 `Light Sleep Readiness`，不是立刻做自动睡眠：

```text
已验证的运行态 STANDBY
  -> power_budget sleep_permission / blockers dry-run
  -> sleep 前资源冻结/恢复合同
  -> 默认关闭的 timer-based 手动 Light Sleep harness
  -> 板端证据充分后再讨论自动策略
```

第一版原则：正常固件仍不自动进入 `Light Sleep / Deep Sleep`。

## Current Baseline

已经成立的事实：

- `ui_refresh_policy -> power_policy -> 各资源 owner` 是当前运行态低功耗主链路。
- `STANDBY` 是唯一保留的空闲省电产品态，不再推进单独 `IDLE_DIM`。
- `power_policy` 只发布预算，不直接操作 LVGL、panel、Wi-Fi、audio、PMIC 或模型。
- `network_service` 已能消费预算：进入 STANDBY 后启用 Wi-Fi modem PS，并暂停非关键云端探测。
- `LOW_BATTERY_WARN` V1 只作为 `power_budget` flag 和日志字段，不改预算、不弹 UI、不做极低电量自动关机。
- `PCF85063ATL` 已有运行态 countdown timer 证据：`RTC_INT(GPIO39)` 能被拉低，清 `TF` 后能恢复高电平；该证据保留为后续外部唤醒增强，不是 V1 sleep 主唤醒源。
- `wakeup_evidence_service` 已能记录 RTC 时间快照、GPIO39 电平、`Control_2/TF` 和本地 sleep test snapshot。
- `system_time_service` 已接管 RTC bootstrap、SNTP 同步、RTC 写回和 official_chat 时间请求。

未闭环的事实：

- `AXP_IRQ` 仍只确认到 `EXIO5`，最终 MCU GPIO 映射和 IRQ enable 还没有完整证据。
- 目前没有系统级 `Light Sleep / Deep Sleep` 编排。
- USB 串口/JTAG 在 sleep 场景下不可靠，不能把“COM3 无日志”当作唤醒失败证据。
- CO5300 硬件 sleep-in、panel 反初始化、显示供电轨关闭、PMIC rail 控制都不属于当前阶段。

## Architecture Decision

### 本阶段新增的是“就绪框架”，不是“睡眠策略”

`Light Sleep Readiness` 只回答：

- 当前是否具备进入一次手动 Light Sleep 测试的条件。
- 进入前哪些资源必须处于安全状态。
- 内部 RTC timer 唤醒、唤醒原因、RTC/PMIC 状态如何留下证据。
- 醒后资源恢复是否仍满足现有 owner 合同。

它不回答：

- 用户多久不操作后自动 sleep。
- 低电量时是否自动关机。
- Deep Sleep 后如何冷启动恢复全部业务。
- PMIC 供电轨怎么关。

### Owner 划分

| 事项 | Owner | 说明 |
| --- | --- | --- |
| UI activity / STANDBY 事实 | `ui_refresh_policy` | 继续只发布 UI 活跃度、刷新节奏和亮度目标 |
| 整机预算 | `power_policy` | 合成 `ACTIVE / STANDBY / LOW_BATTERY_WARN / CHARGING / MAINTENANCE`，不直接 sleep |
| RTC/PMIC 唤醒证据 | `wakeup_evidence_service` | 只做证据、快照和手动测试结果，不拥有产品策略 |
| Sleep 执行 | `sleep_coordinator` | 只消费 `power_budget` 中的 `sleep_permission / sleep_blockers / interval_hint`，默认 dry-run，显式测试时唯一调用 `esp_sleep_*` |
| RTC 寄存器访问 | `components/pcf85063atl` | probe、time、status、countdown、clear flag |
| PMIC 状态/IRQ bank | `components/axp2101` + `board_power` | 只读快照和 IRQ 状态，不在本阶段做 rail 策略 |
| Wi-Fi runtime PS | `components/wifi_control` | 提供 STA runtime power save 能力 |
| 网络预算消费 | `network_service` | 消费 `power_policy` 预算，暂停/恢复非关键同步 |
| 可暂停后台任务 | `background_service_manager` 及各 session owner | 只根据预算阻塞/恢复各自 runtime，不变成通用 sleep manager |

如果后续确实需要 sleep 编排入口，第一版只能新增窄职责 `sleep_coordinator`，负责 dry-run、手动测试协调和证据汇总。不要新增大而全 `ResourceManager`、`system_power_manager` 或把逻辑塞进 `hardware_init()`。

## Non-goals

- 不在正常开机路径自动调用 `esp_light_sleep_start()`。
- 不做 `Deep Sleep`。
- 不做 PMIC rail 关闭、CO5300 sleep-in、panel deinit 或显示供电控制。
- 不修改分区表、NVS 结构、OTA 布局或 bootloader。
- 不把 `wakeup_evidence_service` 升级成低功耗策略 owner。
- 不因为 sleep 计划而重构 Wi-Fi / audio / Safety Monitor 主架构。

## Phase Plan

### Phase 0: 稳定基线冻结

目标：先固定当前已验证的运行态 STANDBY 作为可回退点。

执行项：

- 记录当前 board log 证据：启动、首帧、STANDBY 进入、触摸/按键/P0 恢复、Wi-Fi 预算降级/恢复。
- 确认普通固件中 Light Sleep 测试入口仍默认关闭。
- 确认 `LOW_BATTERY_WARN` 只做 flag/log，不触发 UI、关机或 sleep。

验收：

- `STANDBY` 30 秒进入、约 5 秒亮度降到 0%、触摸恢复。
- `power_policy` 日志能看到 `STANDBY` 预算变化。
- 采集窗口内无 panic、watchdog、NO_MEM、显示 flush 失败。

### Phase 1: 外部唤醒增强证据整理

目标：把后续外部事件唤醒可能依赖的信号整理清楚。V1 timer-based Light Sleep 不依赖这些外部信号闭环。

执行项：

- 启动日志输出 RTC readiness summary：`rtc_present`、`os`、`RTC_INT(GPIO39)` 初始电平、`Control_2/TF`。
- 保留 PCF85063ATL countdown timer 运行态证据：只需要首次触发，触发后停止 timer。
- 输出 AXP readiness summary：PMIC 是否 present、当前 IRQ bank 是否可读、是否已清 pending IRQ。
- 明确记录 `AXP_IRQ` 状态：`EXIO5` 已知、最终 MCU GPIO 未知或已知、IRQ enable 未完成或已完成。

验收：

- 日志能区分 RTC timer 触发、GPIO39 拉低、清 `TF` 后释放。
- AXP 路径不再用“没日志”推断不可用，必须明确是“未映射 / 未使能 / 未触发 / 已触发”。
- 仍不进入 ESP sleep。

### Phase 2: sleep 前资源合同

目标：定义进入一次手动 Light Sleep 前哪些资源必须被暂停、保持或降级。

合同草案：

- UI：必须已经处于 `STANDBY` 或由测试入口显式请求短暂冻结；醒后必须恢复 `ui_refresh_policy_notify_*` 能拉回 ACTIVE。
- 网络：进入测试前允许启用 modem PS；第一版不主动断 AP、不销毁 IP、不重启 Wi-Fi。
- Safety Monitor：普通 `STANDBY` 不关闭；手动 sleep 测试可以要求短暂暂停，但必须由 `background_service_manager / safety_monitor_session` 表达，不允许测试代码直接停模型 runtime。
- Audio：测试前必须确认没有 official_chat 前台输入输出 session；如需暂停，走 `audio_codec` owner/session 语义。
- RTC/PMIC：V1 测试只要求配置 ESP32-S3 internal RTC timer；若同时观测 `RTC_INT(GPIO39)`，必须先清 RTC `TF` 并把结果标记为外部唤醒增强证据。

验收：

- 每个资源的处理都能追到原 owner，测试入口不直接绕过 owner 操作底层。
- P0 危险提醒仍保留强制拉回 ACTIVE 的产品优先级；如果手动 sleep 测试会暂停 Safety Monitor，日志必须显式写明。

### Phase 3: 默认关闭的手动 Light Sleep harness

目标：只提供工程测试入口，不接入自动策略。

执行项：

- 测试入口默认关闭，普通用户固件不进入 sleep。
- 若后续启用测试，V1 只配置 ESP32-S3 internal RTC timer 唤醒；`RTC_INT(GPIO39)` 与 `AXP_IRQ/EXIO5` 不作为 V1 主唤醒源。
- 进入 sleep 前先写 `sleep_test_enter` 证据。
- 从 Light Sleep 返回后的第一件事是写本地 snapshot：wake cause、elapsed ms、sleep interval hint、可选 GPIO39 level、RTC `Control_2/TF`、PMIC IRQ bank。
- 再打印日志、清 RTC flag、恢复资源预算。

验收：

- 默认固件只打印 `light_sleep_test_skipped` 或等价日志。
- 启用测试时，醒后必须能通过本地 snapshot 或后续日志读取 wake cause；不能只依赖 COM3 实时输出。
- 如果 USB 串口丢失，只能判定观测链路不可靠，不能直接判定 RTC/PMIC 唤醒失败。

### Phase 4: 自动策略候选评审

目标：只有手动 harness 通过后，再决定是否把 sleep 纳入产品策略。

进入条件：

- timer-based Light Sleep dry-run 与显式测试都有完整证据。
- `RTC_INT(GPIO39)` 与 `AXP_IRQ/EXIO5` 作为外部唤醒增强另行评估；它们不是 V1 timer-based sleep 自动策略的 blocker。
- sleep 前资源合同在至少一次手动测试中没有破坏 UI、Wi-Fi、audio、Safety Monitor 恢复。
- 有明确回退方式：关闭测试入口后 `app-flash` 能恢复 STANDBY 稳定固件。

自动策略仍需单独计划，不在本计划内实现。

## Risk Register

| 风险 | 影响 | 当前处理 |
| --- | --- | --- |
| USB 串口/JTAG sleep 后丢日志 | 误判唤醒失败 | 醒后先写本地 snapshot，再打印日志 |
| `AXP_IRQ` MCU 映射不明 | PMIC 唤醒链路无法闭环 | 先做 evidence summary，不接入 sleep policy |
| RTC `TF` 锁存导致 INT 一直低 | GPIO 唤醒源无法再次触发 | 每次测试前后都清 `TF` 并记录电平 |
| 共享 I2C 总线 | RTC/PMIC/touch/audio codec 访问互相影响 | 只在低频 evidence/service 路径访问，失败显式降级 |
| Wi-Fi/audio/model 恢复复杂 | sleep 后业务不稳定 | 第一版不自动 sleep，手动测试前定义资源合同 |
| P0 安全提醒被省电压制 | 产品安全风险 | P0 仍可强制 ACTIVE；手动 sleep 测试若暂停监听必须显式记录 |

## Verification Plan

只改计划文档时：

```powershell
uv run python scripts/context/validate_context.py --level standard --q "Light Sleep Readiness STANDBY RTC_INT AXP_IRQ wakeup_evidence power_policy" --brief
```

后续若改源码：

```powershell
uv run python scripts/context/validate_context.py --level light --q "Light Sleep Readiness STANDBY RTC_INT AXP_IRQ wakeup_evidence power_policy" --brief
. D:\esp-idf\v5.5.3\esp-idf\export.ps1
idf.py build
idf.py -p COM3 app-flash
```

板端采集优先使用：

```powershell
.\scripts\board\agent_serial_monitor.ps1 -Port COM3 -Mode monitor -DurationSec 60 -Pattern "wakeup_evidence|power_policy|ui_refresh_policy|light_sleep"
```

## Framework Acceptance

本计划视为可进入实现阶段的条件：

- 后续实现不改变当前 `STANDBY` 正常路径。
- 所有 sleep 测试入口默认关闭。
- 每个资源的暂停/恢复都能追到现有 owner。
- RTC/PMIC evidence 与 power policy 分离，不互相抢职责。
- 任意板端异常都能通过关闭测试入口回到运行态 STANDBY 稳定固件。

## Source Context

- `docs/context/knowledge/project/runtime-owner-contract.md`
- `docs/context/knowledge/project/low-power-management-baseline.md`
- `docs/context/knowledge/project/rtc-pmic-wakeup-evidence-loop.md`
- `docs/context/plans/active/2026-06-01-power-time-visible-status-plan.md`
- `docs/context/runs/2026-06-01-attempt-standby-power-time-v1-board-validation.md`
- `docs/context/runs/2026-05-16-attempt-rtc-pmic-wakeup-evidence-loop.md`
