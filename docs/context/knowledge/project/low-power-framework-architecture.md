---
id: project-low-power-framework-architecture
tags: project, power, low-power, architecture, standby, sleep, power_policy
summary: 当前手表项目低功耗总框架设计，固定产品状态、事实快照、预算发布、owner 执行和 sleep dry-run 边界。
last_reviewed: 2026-07-31
memory_type: project_knowledge
scope: repo
owners: main/services/power/power_policy.c, main/services/runtime/safety_monitor_policy.c, main/services/network/network_service.c, main/ui/ui_refresh_policy.c
triggers: low power framework, power_policy, standby, sleep_coordinator, power_budget, 低功耗框架
evidence_level: design
status: active
route_area: "Low power framework"
---

# 低功耗总框架架构

## 一句话结论

当前项目低功耗框架应以 `power_policy` 的预算发布为中心，而不是以“大状态机”或“大电源管理器”为中心。

核心链路：

```text
各 owner 发布只读事实快照
  -> power_policy 聚合事实并发布 power_budget snapshot
  -> 各 owner 消费预算并在自己资源域内执行
  -> sleep_coordinator 只消费 sleep 预算并 dry-run 记录
```

V1 采用 `FreeRTOS owner snapshot + power_budget` 基线，不做拥有硬件的 `runtime lease` 或中心化资源管家；`runtime_coordinator` 只协调已注册 owner 的让路协议。

## 设计目标

- 对齐手表产品语义：用户看到的是 `ACTIVE / STANDBY`，后续可由 UI owner 接入低电量提示；用户不应看到 `Light Sleep / Deep Sleep` 这类底层阶段。
- 保持嵌入式 owner 边界：策略层不直接操作屏幕、Wi-Fi、音频、PMIC、LVGL 或 ESP sleep API。
- 保持 FreeRTOS owner 模型：长期服务用独立 task 或 owner 执行上下文，跨 owner 用 snapshot、notify、queue、event group 协作。
- 让 V1 可验证、可回退：先把预算流、STANDBY 行为和 sleep dry-run 做清楚，不保留当前有问题的手动 sleep 测试代码。
- 为后续更深低功耗预留扩展点：DFS、ESP-IDF Automatic Light-sleep、Deep Sleep、RTC/PMIC 外部唤醒都能在单独评审后挂入同一框架。

## 产品层状态

V1 只保留两个产品状态：

```text
ACTIVE
STANDBY
```

`LOW_BATTERY_WARN`、`CHARGING`、`EXTERNAL_POWER`、`FORCE_ACTIVE` 这类信息是 flag，不是并列产品状态。

### ACTIVE

正常运行状态：

- UI 可以高刷新。
- 网络可以同步。
- 后台任务按用户意图和系统策略运行。
- 音频、危险识别、OTA、配网等模块按各自 owner 规则声明事实和 blocker。

### STANDBY

非活跃产品态：

- 自动空闲进入 `STANDBY`。
- 后续用户主动熄屏按钮也进入 `STANDBY`。
- V1 不因 `standby_reason` 使用更激进档位；原因只用于日志和未来扩展。
- V1 不进入 ESP sleep，不做 CO5300 sleep-in，不关显示电源轨，不直接关 codec/I2S。

推荐原因字段：

```text
AUTO_IDLE
USER_SCREEN_OFF
LOW_BATTERY_POLICY
SYSTEM_REQUEST
```

V1 行为：

- 屏幕亮度降到 0 或近似熄屏。
- UI 刷新降低。
- Wi-Fi 使用 power save，暂停非关键同步，不断 AP。
- 可暂停后台任务降频或暂停。
- AXP2101 继续作为只读事实源，低频轮询。

## STANDBY 内部功耗档位

文档保留多档设计，但 V1 只落一个统一 `STANDBY budget`。

后续可扩展档位：

```text
STANDBY_L1_DIM
STANDBY_L2_RUNTIME_SAVE
STANDBY_L3_LIGHT_SLEEP_ELIGIBLE
STANDBY_L4_DEEP_SLEEP_ELIGIBLE
```

档位不应写死成纯计时状态机。空闲时间只是输入之一，最终由 `power_policy` 根据事实快照计算：

- idle duration
- standby reason
- battery / charging / external power
- UI force active
- audio active
- network critical
- background critical
- OTA / provisioning
- alert active
- debug lock

V1 规则：

```text
standby_reason 仅用于观测和日志。
LOW_BATTERY_WARN 仅作为 flag 和日志字段。
STANDBY 多档只写设计，不改变执行行为。
```

## 事实输入

V1 选择显式 snapshot API，不做通用 fact registry，也不做 `runtime lease` 仲裁中心。

每个 owner 保留自己的状态写权限，并通过只读 snapshot API 暴露当前状态副本。`power_policy` 由独立 FreeRTOS task 维护最新预算快照：关键事件通过 notify 唤醒 task，周期 timeout 负责兜底一致性，最终事实仍通过 snapshot API 读取。

示例：

```text
ui_refresh_policy_get_activity_snapshot()
power_service_get_state()
audio_codec_get_session_snapshot()
network_service_get_snapshot()
safety_monitor_policy_get_snapshot()
```

规则：

- snapshot API 返回状态副本，不暴露内部可写对象。
- `power_policy` 不偷看 owner 内部变量。
- 不做动态通用 fact registry。
- 不做动态 `runtime lease` 表或 TTL 租约账本。
- 不允许多个模块写同一个 fact。

## power_policy 职责

`power_policy` 是预算聚合与发布者，不是硬件执行者。

它负责：

- 读取各 owner snapshot。
- 聚合内部 `power_fact_snapshot_t`。
- 计算 `power_budget_t`。
- 发布只读 `power_budget snapshot`。
- 维护 `budget_version`，只在有效预算变化时递增。
- 记录最近触发重算的 notify reason，帮助判断预算变化来源。
- 记录决策变化日志。

它不负责：

- 不直接调用 LVGL。
- 不直接设置屏幕亮度。
- 不直接调用 Wi-Fi API。
- 不直接停音频或 codec/I2S。
- 不直接 suspend task。
- 不直接读写 AXP2101 控制寄存器。
- 不直接调用 `esp_light_sleep_start()` 或 `esp_deep_sleep_start()`。

## power_budget 语义

`power_budget` 只能表达目标强度和许可，不能携带具体硬件调用。

推荐字段：

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
budget_version
last_notify_reasons
```

示例语义：

```text
display_budget = FULL / DIM / OFF
ui_budget = HIGH_REFRESH / LOW_REFRESH
network_budget = FULL / POWER_SAVE / SYNC_PAUSED
background_budget = FULL / THROTTLED / PAUSE_OPTIONAL
cpu_budget = PERFORMANCE / BALANCED / LOW
power_poll_budget = NORMAL / SLOW
sleep_permission = NONE / LIGHT_ALLOWED / DEEP_ALLOWED
```

禁止把具体动作塞进预算：

```text
set_brightness(0)
wifi_control_set_power_save(true)
vTaskSuspend()
esp_light_sleep_start()
axp2101_write_reg()
```

## owner 消费预算

各 owner 读取 `power_budget snapshot` 后，只执行自己资源域内的降级或恢复：

- UI owner：亮度、刷新、触摸恢复和显示策略。
- network owner：Wi-Fi PS、同步节流、连接保持策略。
- background owner：可暂停任务的降频/暂停，关键任务 blocker 发布。
- audio owner：音频 session 事实、音频活跃 blocker，不在 V1 直接关 codec/I2S。
- system runtime power owner：后续消费 `cpu_budget` 执行 DFS / pm lock。
- sleep coordinator：消费 `sleep_permission` 和 `sleep_interval_hint_ms`，只做 dry-run 观测，不调用 ESP sleep API。

owner 可以反馈执行状态，例如：

```text
APPLIED
PENDING
FAILED
PARTIAL
```

执行状态只能作为下一轮 fact，不能反向修改 `power_budget`。

## 资源释放与结束流程

低功耗预算只能表达目标，不直接释放资源。资源结束必须由真实 owner 执行：

```text
power_policy 发布 budget / blocker 结论
  -> owner 通过 task notification 或 queue 收到预算变化/结束意图
  -> owner 停止新工作
  -> owner 等当前关键动作短收尾
  -> owner 释放 session / 关闭自己资源域内硬件
  -> owner 发布 inactive / released snapshot
  -> power_policy 下一轮聚合看到资源已释放
```

示例：

- 音频活跃时，audio/official_chat owner 发布 `AUDIO_ACTIVE` fact；`power_policy` 只能据此阻止 sleep，不直接关 codec/I2S。
- 网络进入 `STANDBY` 预算时，network owner 自己启用 Wi-Fi PS、暂停非关键同步；`power_policy` 不调用 Wi-Fi API。
- 后台可暂停任务收到 `PAUSE_OPTIONAL` 后，由对应 session owner 自己 stop、退避或保持 blocker。

V1 不做：

- 中心 runtime 直接删除资源占用。
- `power_policy` 抢占或强杀 owner。
- 通过外部改 flag 假装资源已经释放。

## notify 与更新机制

V1 使用 FreeRTOS task + 低频定时 + 关键事件 notify 混合模式。

- `power_policy` 拥有独立 task，负责维护最新 `power_budget snapshot`。
- 低频定时负责兜底一致性，例如 1s 聚合一次 facts。
- `power_policy_notify(reason)` 只请求尽快重算，不携带最终事实。
- 最终事实仍从 snapshot API 读取。
- `power_policy_get_budget()` 只返回最近已发布的预算快照；task 未启动时允许同步兜底计算。

规则：

```text
notify 是触发器，不是事实源。
snapshot API 才是事实源。
budget_version 是消费者判断是否应用到最新预算的轻量序号。
```

关键 notify 来源：

- 触摸/按键
- 用户主动熄屏
- 音频开始/结束
- 充电/外部电源变化
- OTA/配网开始/结束
- 告警状态变化
- 后台关键任务状态变化

V1 不做统一低功耗事件总线。

## sleep_permission 与 sleep_blockers

`sleep_permission` 是结论，表示当前允许睡多深：

```text
NONE
LIGHT_ALLOWED
DEEP_ALLOWED
```

当前 V1 的 `LIGHT_ALLOWED` 只是 dry-run readiness 结论，不会触发真实 ESP sleep。发布门槛：

- 已进入运行态 `STANDBY`。
- `ui_refresh_policy` 的只读快照显示屏幕/触摸无交互满 `5` 分钟。
- `sleep_blockers=none`。
- `sleep_interval_hint_ms>0`。

注意：`STANDBY` 仍可在 `30s` 无交互后进入；`LIGHT_ALLOWED` 是更深一层的候选条件，不等同于 STANDBY 入口。

`sleep_blockers` 是原因，表示当前为什么不能睡：

```text
UI_FORCE_ACTIVE
AUDIO_ACTIVE
NETWORK_CRITICAL
BACKGROUND_CRITICAL
OTA_ACTIVE
PROVISIONING_ACTIVE
ALERT_ACTIVE
DEBUG_LOCK
```

blocker 的写 owner 是对应资源 owner，不是 `power_policy`。`power_policy` 只聚合 blocker 并发布结果。

示例：

```text
audio owner 发布 AUDIO_ACTIVE
  -> power_policy 聚合 sleep_blockers=AUDIO_ACTIVE
  -> sleep_permission=NONE
```

## sleep_coordinator 边界

`sleep_coordinator` 当前只做 sleep 预算 dry-run。手动 Light-sleep 测试路径已移除，因为当前主 USB 串口/JTAG 观测链路和 owner blocker 闭环不足以支撑可靠测试。

它负责：

- 读取 `power_budget snapshot` 中的 `sleep_permission`、`sleep_blockers` 和 `sleep_interval_hint_ms`。
- 写入本地 dry-run snapshot。
- 打印当前是否具备未来 sleep 条件的预算证据。

它不负责：

- 不逐个询问 owner 内部状态。
- 不解释音频、网络、UI、后台任务语义。
- 不修改 `power_budget`。
- 不拥有 readiness 事实。
- 不调用 `esp_sleep_enable_timer_wakeup()`、`esp_light_sleep_start()` 或 `esp_deep_sleep_start()`。
- 不保留 `LIGHT_TEST / DEEP_TEST` 手动测试模式。

V1 默认模式：

```text
DRY_RUN
```

只打印 readiness、blockers 和 interval hint，不调用真实 sleep API。

## Light Sleep 与 Deep Sleep 路线

V1 只定义路线，不默认进入真实 sleep。

当前不保留手动 `esp_light_sleep_start()` 测试路线。后续如果需要真实 Light Sleep，应优先走产品态 Wi-Fi 保持连接路线：ESP-IDF `Wi-Fi Modem-sleep + Automatic Light-sleep`，由网络 owner 和系统 PM 配置协同，不由 `sleep_coordinator` 手动断网、重连或配置 timer wakeup。

### Light Sleep

- 当前不进入真实 Light Sleep。
- 不保留手动 `esp_light_sleep_start()` harness。
- 后续若要打开 Light Sleep，应先新建或指定系统 PM owner，确认 `CONFIG_PM_ENABLE`、`CONFIG_FREERTOS_USE_TICKLESS_IDLE`、PM locks、串口观测风险和 owner blocker 事实源。

### Wi-Fi 保持连接的 Automatic Light-sleep

该路线是后续产品态省电方向：

- `network_service` 按 `network_budget` 启用 `wifi_control_set_power_save(true)`，底层映射到 `esp_wifi_set_ps(WIFI_PS_MIN_MODEM)`。
- 系统 PM owner 后续显式启用 `CONFIG_PM_ENABLE`、`CONFIG_FREERTOS_USE_TICKLESS_IDLE`，并通过 `esp_pm_configure(... light_sleep_enable=true ...)` 允许 ESP-IDF 自动进入 Light-sleep。
- Wi-Fi driver 通过 DTIM/listen interval 自动唤醒维护 AP 连接；业务任务不在 sleep 中运行，只在唤醒窗口处理数据。
- 该路线不得使用 `sleep_coordinator` 手动调用 `esp_sleep_enable_timer_wakeup()` 来维持 Wi-Fi 连接；ESP-IDF automatic Light-sleep 自己管理 timer wakeup。

### Deep Sleep

Deep Sleep wake 是带上下文的冷启动，不承诺恢复原页面、原任务、原网络会话。

进入前只保存最小可验证上下文：

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

保留上下文必须包含 magic、version 和 crc，启动后校验失败则丢弃。

## PCF85063ATL 与 AXP2101 职责

V1 职责划分：

```text
Sleep wake source = ESP32-S3 internal RTC timer
Time recovery source = PCF85063ATL
Power recovery source = AXP2101
```

PCF85063ATL：

- V1 作为时间恢复源。
- 后续可验证 `RTC_INT(GPIO39)` 的 Light Sleep 外部唤醒能力。
- 不作为 Deep Sleep V1 主唤醒源。

AXP2101：

- V1 只读，作为电源事实源。
- 读取电池、VBUS、充电、VBAT、VSYS、SOC 和 IRQ status 证据。
- 不控制电源轨，不写 sleep/wakeup 寄存器，不把 `AXP_IRQ/EXIO5` 当已确认 MCU wake source。

## V1 验收标准

V1 验收以框架边界、预算流、STANDBY 行为和 dry-run 可观测为主。

必须成立：

- `power_policy` 能读取各 owner snapshot。
- `power_policy` 能发布 `power_budget snapshot`。
- owner 能通过 snapshot 表达资源 active / inactive / released。
- `STANDBY` 自动进入/退出稳定。
- 用户主动熄屏也进入 `STANDBY`。
- Wi-Fi PS / 同步节流按 budget 执行。
- UI dim/off / 低刷新按 budget 执行。
- 可暂停后台任务按 budget 暂停或降频。
- `sleep_permission` / `sleep_blockers` 能 dry-run 打印。
- `LOW_BATTERY_WARN` 只记录 flag/log，不改预算、不弹 UI。
- 不进入真实 Light/Deep Sleep。

V1 不要求：

- 真实 Deep Sleep 稳定恢复。
- AXP_IRQ 即时唤醒。
- RTC_INT 外部唤醒。
- CO5300 sleep-in。
- 关显示电源轨。
- 关 codec/I2S。
- Wi-Fi 保持连接的 Automatic Light-sleep。
- 手动 Light-sleep 测试 harness。
- 最终续航小时数。

## 禁止路径

- 不新增大而全 `power_manager`、`resource_manager` 或通用低功耗事件总线。
- 不新增 `runtime lease` 仲裁中心、通用资源账本或 TTL 租约表。
- 不让 `power_policy` 直接操作硬件。
- 不让 `sleep_coordinator` 逐个理解各 owner 内部状态。
- 不让 `sleep_coordinator` 断开、重连或配置 Wi-Fi；Wi-Fi 省电属于 network owner，Automatic Light-sleep 属于后续系统 PM owner。
- 不在 `sleep_coordinator` 恢复手动 Light-sleep 测试代码。
- 不在 V1 做动态 fact registry。
- 不在 V1 把 `LOW_POWER_MODE` 做成独立产品状态。
- 不在 V1 让 `LOW_BATTERY_WARN` 改变预算或弹 UI。
- 不在 V1 默认进入真实 sleep。
- 不把 `AXP_IRQ/EXIO5` 作为已确认 MCU GPIO 使用。

## 与现有文档关系

- `low-power-management-baseline.md`：记录当前已落地能力和缺口。
- `power-wakeup-control-map.md`：记录硬件/唤醒链路证据。
- `runtime-owner-contract.md`：记录全仓 owner 合同。
- 本文：记录低功耗总框架设计和 V1/V2 边界。
