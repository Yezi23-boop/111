---
id: low-power-framework-execution-plan-20260601
tags: plan, active, watch, power, low-power, architecture, standby, sleep-coordinator, power-budget
summary: 按 low-power-framework-architecture.md 重新整理低功耗框架执行计划：以 power_budget 为中心，先补预算流和 dry-run；当前移除手动 Light-sleep 测试代码，后续走 Automatic Light-sleep 路线。
status: active
last_reviewed: 2026-06-01
memory_type: project_plan
scope: repo
owners: main/services/power_policy.c, main/services/background_service_manager.c, main/services/network_service.c, main/ui/ui_refresh_policy.c, main/services/wakeup_evidence_service.c, components/pcf85063atl, components/axp2101
triggers: low-power-framework-architecture, power_budget, sleep_coordinator, STANDBY, Light Sleep, Deep Sleep, low power execution plan, 低功耗框架执行计划
evidence_level: design
supersedes: docs/context/plans/completed/2026-06-01-light-sleep-readiness-framework-plan.md
---

# 低功耗总框架执行计划

## Purpose

本计划按 `docs/context/knowledge/project/low-power-framework-architecture.md` 重新整理后续执行顺序。核心不是新增“大电源管理器”，也不是把 `Light Sleep` 立刻接入产品自动策略，而是先把下面这条链路做成可观察、可测试、可回退：

```text
各 owner 发布只读事实快照
  -> power_policy 聚合 facts
  -> power_policy 发布 power_budget snapshot
  -> 各 owner 消费 budget
  -> sleep_coordinator 只消费 sleep budget，并且只 dry-run
```

## Correction From Previous Plan

上一份 `Light Sleep Readiness` 计划里的“就绪框架”说法容易被误解成新增一个抽象层。本计划改成更直接的执行口径：

- 不使用 `Light Sleep Readiness` 作为正式产品状态或模块名。
- 保留窄职责 `sleep_coordinator`，但它只能消费 `power_budget` 中的 sleep 字段做 dry-run，不能理解 UI、网络、音频、PMIC 的业务细节。
- 当前移除手动 `esp_light_sleep_start()` 测试代码，不再保留 `LIGHT_TEST / DEEP_TEST` 枚举或编译开关。
- `RTC_INT(GPIO39)`、`AXP_IRQ/EXIO5`、PCF85063ATL/PMIC IRQ 是外部唤醒增强证据，不阻塞 STANDBY / Wi-Fi Modem PS / Automatic Light-sleep 规划。

## Product State Boundary

V1 只保留两个产品态：

```text
ACTIVE
STANDBY
```

以下都不是并列产品态：

- `LOW_BATTERY_WARN`
- `CHARGING`
- `EXTERNAL_POWER`
- `FORCE_ACTIVE`
- `Light Sleep`
- `Deep Sleep`

这些只能作为 flag、blocker、预算字段或底层执行阶段存在。

## Phase 0: 文档与现状对齐

目标：先消除执行计划和架构文档之间可能让后续 agent 误读的冲突。

执行项：

1. 以 `low-power-framework-architecture.md` 为主文档，明确 V1 的中心是 `power_budget`。
2. 明确 `LOW_BATTERY_WARN` 在 V1 低功耗框架里只作为 flag/log，不改变预算、不弹 UI、不触发 sleep。
3. 若后续重新需要低电量 UI 提示，应作为 UI/alerts owner 的独立产品需求重新评审，并同步更新 `low-power-framework-architecture.md`。
4. 标记旧的 `Light Sleep Readiness` 计划为 `superseded` 并移出 active，避免后续继续沿用容易误解的术语。

验收：

- context standard 通过。
- active plan 中只保留一份后续低功耗主执行计划。

## Phase 1: power_budget 快照成型

目标：把 `power_policy` 从“当前状态日志”推进到“统一预算快照发布者”。

当前进度（2026-06-01）：

- 已将 `power_policy_state_t` 收敛为 `ACTIVE / STANDBY` 两个产品态。
- 已将 `LOW_BATTERY_WARN / CHARGING / EXTERNAL_POWER / MAINTENANCE` 收敛为预算 flag 或 bool 字段，不再作为产品态。
- 已补 `standby_reason / display_budget / ui_budget / network_budget / background_budget / cpu_budget / power_poll_budget / sleep_permission / sleep_blockers / flags / sleep_interval_hint_ms`。
- 已在 STANDBY 且无 blocker、interval hint 有效时发布 `sleep_permission=LIGHT_ALLOWED`；当前仅用于 dry-run 观测，不触发真实 sleep。
- 已将低电量从后台 manager 自动 UI 提示链路撤回为 `POWER_POLICY_FLAG_LOW_BATTERY_WARN` 和日志字段；原 `app_alert_manager` 低电量接口暂留，但不再由 `background_service_manager` 调用。

建议 `power_budget` 至少表达这些语义：

```text
activity_state
standby_reason
display_budget
ui_budget
network_budget
background_budget
cpu_budget
power_poll_budget
sleep_permission
sleep_blockers
flags
sleep_interval_hint_ms
```

执行项：

1. 检查当前 `power_policy` snapshot 是否已经覆盖上述字段。
2. 缺字段时先补只读 snapshot，不急着让所有 owner 消费。
3. `sleep_permission` 第一版默认 `NONE`，直到 dry-run 逻辑能解释 blockers。
4. `sleep_blockers` 必须可解释，例如 `UI_FORCE_ACTIVE / AUDIO_ACTIVE / NETWORK_CRITICAL / BACKGROUND_CRITICAL / OTA_ACTIVE / PROVISIONING_ACTIVE / ALERT_ACTIVE / DEBUG_LOCK`。
5. `sleep_interval_hint_ms` 第一版只作为日志 hint，不直接触发 sleep。

V1 最小判定规则：

- `sleep_permission = NONE`：非 `STANDBY`、存在任意 blocker、`sleep_interval_hint_ms` 无效，或当前处于 debug lock。
- `sleep_permission = LIGHT_ALLOWED`：当前是 `STANDBY`、`sleep_blockers = 0`、`sleep_interval_hint_ms` 有效，且仅表示“条件允许 Light Sleep 测试”。
- `sleep_permission = DEEP_ALLOWED`：V1 正常路径不产生；只允许 Phase 5 显式测试分支单独评审。

`LIGHT_ALLOWED` 不等于立即 sleep。当前固件不保留手动 sleep 执行路径，只作为后续系统 PM owner 接入前的预算证据。

验收：

- source test 能证明 `power_policy` 不直接调用 LVGL、Wi-Fi、audio、PMIC 或 ESP sleep API。
- 日志能看到 budget 变化原因，不只是状态名变化。

## Phase 2: owner 分批消费 budget

目标：让各 owner 只在自己的资源域内执行预算，不让 `power_policy` 下场操作硬件。

### UI owner

当前已基本成立：

- `STANDBY` 后降亮到 0 或近似熄屏。
- LVGL 主循环降频。
- 触摸/按键/P0 提醒能恢复 ACTIVE。

后续只补齐：

- `display_budget = FULL / DIM / OFF` 与现有亮度行为的对应日志。
- `ui_budget = HIGH_REFRESH / LOW_REFRESH` 与现有 delay 行为的对应日志。

### network owner

当前已基本成立：

- `network_budget = POWER_SAVE / SYNC_PAUSED` 时启用 Wi-Fi modem PS。
- 暂停非关键同步，不主动断 AP。

后续只补齐：

- budget 恢复后必须恢复非关键同步。
- 日志区分 `POWER_SAVE` 和 `SYNC_PAUSED`。

### background owner

目标：

- `background_budget = PAUSE_OPTIONAL` 时只暂停可暂停任务。
- Safety Monitor 这类 P0 相关后台能力默认不因普通 `STANDBY` 被关死。
- 如果某任务不能暂停，应该发布 blocker，而不是让 `power_policy` 猜。
- Safety Monitor 运行中应发布 `BACKGROUND_CRITICAL` 或等价 blocker；手动 sleep 测试不得静默暂停它。
- 如果显式测试确实需要暂停 Safety Monitor，必须通过 `background_service_manager / safety_monitor_session` 的正式路径进入可记录的测试暂停态，不能由 `sleep_coordinator` 直接停模型 runtime。

### audio owner

V1 不关 codec/I2S，只提供 blocker：

- official_chat 输入/输出活跃 -> `AUDIO_ACTIVE`
- 普通播放活跃 -> 视业务决定是否 blocker
- P0 alert audio -> `ALERT_ACTIVE`

### power observe owner

`power_service / board_power / axp2101` 保持只读：

- 外部供电、电池、电压、SOC、charging 状态继续低频发布。
- 不写 PMIC rail。
- 不把 `AXP_IRQ/EXIO5` 当已确认 MCU wake source。

验收：

- 每个 owner 至少有 source test 或日志证明“消费 budget 但不越权”。
- `power_policy` 不知道具体硬件调用细节。

## Phase 3: sleep_coordinator dry-run

目标：新增或收敛一个窄职责 `sleep_coordinator`，但第一版只 dry-run，不进入真实 sleep。

当前进度（2026-06-01）：

- 已新增 `main/services/sleep_coordinator.[ch]`。
- 默认启动 `DRY_RUN` 后台任务，只读取 `power_policy_get_budget()`。
- dry-run 日志记录 `sleep_permission / sleep_blockers / sleep_interval_hint_ms`。
- 手动 `LIGHT_TEST / DEEP_TEST` 已移除，不调用真实 ESP sleep API。
- `idf.py build` 已通过；COM3 `app-flash-monitor` 已采集板端 dry-run 日志，STANDBY 后观察到 `sleep=LIGHT_ALLOWED blockers=none interval_ms=8000`，`sleep_coordinator` 仍只打印 `DRY_RUN`，不进入真实 sleep。证据见 `docs/context/runs/2026-06-01-attempt-low-power-framework-dry-run-board-validation.md`。

职责：

- 读取 `power_budget snapshot`。
- 打印 `sleep_permission`、`sleep_blockers`、`sleep_interval_hint_ms`。
- 在 dry-run 下说明“如果现在允许 sleep，会选择什么 interval”。
- 写入本地 sleep dry-run snapshot，方便 UI 或日志读取。

禁止：

- 不逐个读取 UI、网络、音频、PMIC 内部状态。
- 不修改 `power_budget`。
- 不解释业务 blocker。
- 不直接暂停任务。
- dry-run 模式不调用 `esp_light_sleep_start()` 或 `esp_deep_sleep_start()`。

验收：

- 默认固件只出现 dry-run 日志。
- 没有真实 sleep。
- 关掉 `sleep_coordinator` 后，现有 STANDBY 行为不受影响。

## Phase 4: 手动 Light Sleep 测试撤回

目标：撤回当前有问题的手动 Light-sleep 测试代码，避免主 USB 串口/JTAG 观测不稳定和 owner blocker 未闭环时误导后续判断。

当前进度（2026-06-01）：

- `sleep_coordinator` 只保留 `DISABLED / DRY_RUN`。
- 已移除 `SLEEP_COORDINATOR_LIGHT_TEST_ENABLED`、`SLEEP_COORDINATOR_LIGHT_TEST_OWNER_BLOCKERS_READY` 编译开关。
- 已移除 `esp_sleep_enable_timer_wakeup()`、`esp_light_sleep_start()`、`esp_sleep_get_wakeup_cause()` 调用。
- 已移除 `sleep_coordinator_sleep_test_result_t` 和测试结果读取接口。
- `wakeup_evidence_service` 只做 RTC_INT / AXP IRQ 运行态证据，不承诺 sleep wakeup。
- 官方 ESP-IDF 口径已确认：若产品态需要保持 Wi-Fi 连接，应走 `Wi-Fi Modem-sleep + Automatic Light-sleep`。

执行规则：

1. `sleep_coordinator` 不保留真实 sleep API。
2. `app_main` 不保留任何测试模式自动切换代码。
3. `CMakeLists.txt` 不接受 Light-sleep 测试环境变量。
4. source test 证明 `main/components` 中不存在手动 sleep API 调用。

注意：

- `RTC_INT(GPIO39)` 继续作为运行态证据，不等于 sleep wakeup 已闭环。
- 后续若重新需要手动 sleep 实验，必须新开计划并先闭环外部 UART/恢复路径、owner blocker 事实源和回退步骤。

验收：

- 普通 STANDBY 固件行为不变。
- source test 和 `idf.py build` 通过。
- `rg` 在 `main/components` 真实源码中找不到手动 sleep API。

## Phase 4.5: Wi-Fi 保持连接的 Automatic Light-sleep 路线

目标：规划产品态“STANDBY 不断 Wi-Fi”的省电路线。

边界：

- `network_service` / `wifi_control` 负责 Wi-Fi modem PS，当前已有 `wifi_control_set_power_save()` 映射到 `esp_wifi_set_ps(WIFI_PS_MIN_MODEM)`。
- 后续系统 PM owner 负责 `CONFIG_PM_ENABLE`、`CONFIG_FREERTOS_USE_TICKLESS_IDLE` 和 `esp_pm_configure(... light_sleep_enable=true ...)`。
- `sleep_coordinator` 不参与 Wi-Fi 保持连接路线；它不读取 Wi-Fi 连接状态，不断开 AP，不恢复 latest Wi-Fi，不手动进入 Light Sleep。
- Automatic Light-sleep 不手动配置 timer wakeup；ESP-IDF PM 会根据 tickless idle、timer、Wi-Fi driver 需求自动决定睡眠窗口。

验收：

- STANDBY 下 Wi-Fi 仍保持 AP 连接，日志显示 Wi-Fi power save 已按 budget 开启。
- 没有 `sleep_coordinator` 调用 Wi-Fi / network API。
- 后续打开 automatic Light-sleep 前，必须先 source 级确认 `CONFIG_PM_ENABLE`、`CONFIG_FREERTOS_USE_TICKLESS_IDLE`、PM lock 边界和串口观测风险。

## Phase 5: timer-based Deep Sleep 显式测试

目标：只在 Light Sleep 显式测试稳定后考虑；Deep Sleep 醒来是带上下文的冷启动，不承诺恢复原页面、原 Wi-Fi/HTTP/MQTT 会话或任务句柄。

进入前只保存最小上下文：

```text
boot_count
last_activity_state
last_standby_reason
last_sleep_reason
last_sleep_enter_time
last_battery_soc
last_battery_mv
last_charging_state
last_sleep_interval_hint_ms
last_wakeup_sequence_id
```

禁止保存：

```text
LVGL 对象
Wi-Fi handle
HTTP/MQTT 会话对象
音频 session
I2C 设备 handle
任务句柄
指针
动态内存地址
```

验收：

- 保存结构包含 magic、version、crc。
- 校验失败必须丢弃上下文。
- Deep Sleep 测试仍必须显式 opt-in。

## Phase 6: 外部唤醒增强

目标：继续推进 RTC/PMIC 外部唤醒证据，但不阻塞 timer-based 显式 sleep 主线。

PCF85063ATL：

- V1 作为时间恢复源。
- `RTC_INT(GPIO39)` 继续作为外部唤醒增强证据。
- 需要证明清 `TF` 后 GPIO 能释放。

AXP2101：

- V1 只读。
- 继续读取电池、VBUS、VBAT、VSYS、SOC、IRQ status。
- 不控制电源轨。
- 不把 `AXP_IRQ/EXIO5` 当已确认 MCU wake source。

验收：

- `AXP_IRQ/EXIO5` 最终 MCU GPIO 或不可用结论明确。
- 外部唤醒路径接入前必须单独有 evidence run。

## Rollback Strategy

- 普通功能改动默认 `idf.py -p COM3 app-flash`，不默认全量 `flash`。
- 任何 sleep 测试开关默认关闭。
- 若 sleep 测试导致串口/下载不稳定，先关闭测试入口恢复安全固件。
- 不回滚已经验证的 STANDBY、Wi-Fi budget、system_time、RTC/PMIC evidence 基础能力。

## Verification

只改 context 文档：

```powershell
uv run python scripts/context/validate_context.py --level standard --q "low-power-framework-architecture power_budget sleep_coordinator STANDBY Light Sleep" --brief
```

后续改源码：

```powershell
uv run python scripts/context/validate_context.py --level light --q "low-power-framework-architecture power_budget sleep_coordinator STANDBY Light Sleep" --brief
. D:\esp-idf\v5.5.3\esp-idf\export.ps1
idf.py build
idf.py -p COM3 app-flash
```

板端证据：

```powershell
.\scripts\board\agent_serial_monitor.ps1 -Port COM3 -Mode monitor -DurationSec 60 -Pattern "power_policy|power_budget|sleep_coordinator|ui_refresh_policy|network_service|wakeup_evidence"
```

## Source Context

- `docs/context/knowledge/project/low-power-framework-architecture.md`
- `docs/context/knowledge/project/low-power-management-baseline.md`
- `docs/context/knowledge/project/rtc-pmic-wakeup-evidence-loop.md`
- `docs/context/knowledge/project/runtime-owner-contract.md`
- `docs/context/plans/active/2026-06-01-power-time-visible-status-plan.md`
