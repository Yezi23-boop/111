---
id: watch-resource-framework-plan-20260512
tags: plan, watch, resource-management, power-policy, background, audio, display, network, sensor, haptic, maintenance
summary: 规划 ESP32-S3 手表整体资源框架，固定 power_policy 总状态机、资源 owner、状态预算表、优先级、后台功能预算和第一阶段最小落地路径。
status: active
last_reviewed: 2026-05-12
owners: main/services/power_policy, main/services/background_service_manager, components/audio_codec, components/network_manager, components/lvgl_port, components/co5300_panel, components/touch_ft5x06, main/features/danger_detection
triggers: watch, resource, framework, background, power_policy, audio_resource, danger_detection, low_power
evidence_level: design
---

# 手表整体资源框架计划

## 目标

把当前手表固件里的屏幕、触摸、音频、网络、传感器、AI 推理和后台任务统一纳入一个可解释、可验证、可回退的资源框架。

本计划不是马上实现大而全 `ResourceManager`，而是先固定：

- 谁是整机状态 owner。
- 哪些资源必须独占。
- 哪些后台功能可以长期运行。
- 哪些场景必须降频、暂停或让路。
- 后续代码应该落在哪一层。

## 当前依据

- `low-power-management-baseline.md`：当前已落地 `Power Observe / UI Idle-Dim / Scene-based Wi-Fi PS`，但还没有统一 `power_policy`。
- `watch-low-power-management-architecture.md`：推荐按 `Active -> Idle-Dim -> Standby -> Deep Sleep / Power Off` 分阶段推进，不直接深睡。
- `display-touch-audio-bus-map.md`：显示、触摸、音频和控制面 I2C/I2S/QSPI 已有明确硬件 owner。
- `display-render-touch-transfer-pipeline.md`：`audio + sd + wifi + lvgl` 并发会造成内部 DMA/内存压力，资源框架必须考虑并发预算。
- `hearing-assist-danger-alert-firmware-mapping.md`：危险识别应从页面生命周期转向系统级后台服务，`danger_detection_service` 负责状态机，`audio_codec` 负责麦克风 session。

## 总体分层

```text
UI / Feature
  -> background_service_manager / power_policy
  -> resource policy / domain owner
  -> driver adapter
  -> ESP-IDF / LVGL / ESP-DL / device driver
```

### `power_policy`：整机资源状态机 owner

建议新增或补全 `main/services/power_policy.[ch]`。

负责：

- 维护整机资源状态：`ACTIVE / IDLE_DIM / STANDBY / LOW_BATTERY_WARN / CHARGING / MAINTENANCE`。
- 根据电源、屏幕、用户交互、后台任务和高优先级活动下发资源预算。
- 决定哪些后台任务可以运行、降频、暂停或恢复。
- 统一调用 UI 亮度/刷新、Wi-Fi 省电、音频 session、传感器采样节流等策略接口。

不负责：

- 直接读 PMIC 寄存器。
- 直接操作 LVGL 对象。
- 直接跑模型推理。
- 直接处理某个页面按钮逻辑。

### `background_service_manager`：后台功能开关 owner

建议新增 `main/services/background_service_manager.[ch]`，第一阶段可以很薄。

负责：

- 保存后台功能开关，例如危险识别、网络同步、语音助手待机、传感器采样。
- 把“用户允许后台运行”和“当前资源允许运行”分开。
- 向 `power_policy` 暴露后台功能需求。

不负责：

- 不直接抢麦克风。
- 不直接改模型阈值。
- 不直接播放提醒。

### 资源 owner

| 资源 | 当前或建议 owner | 资源类型 | 策略 |
| --- | --- | --- | --- |
| 屏幕亮度/刷新节奏 | `ui_refresh_policy` + `co5300_panel` | 可降级 | 由 `power_policy` 下发 active/idle/standby 预算 |
| LVGL 对象生命周期 | `main/ui/* controller` + `lvgl_task` | UI 内部资源 | 后台服务不得直接改 LVGL 对象 |
| 触摸输入 | `touch_ft5x06` + `lvgl_port` | 唤醒/交互资源 | 第一阶段继续轮询，后续再评估中断唤醒 |
| 麦克风/I2S 输入 | `audio_codec` | 独占资源 | 通过 input session 申请，语音助手优先于后台危险监听 |
| 喇叭/I2S 输出 | `audio_codec` + `app_alert_manager` | 可抢占资源 | 危险提醒可抢占普通播放，普通播放不可抢占 P0 提醒 |
| 震动/触觉提醒 | haptic driver owner + `app_alert_manager` | P0 提醒资源 / 当前未接入 | 听障危险提醒产品目标默认以震动为第一通道；当前硬件未增加，第一阶段只保留预算占位和接口边界 |
| ESP-DL 模型 RAM | `espdl_model_runner` | 昂贵常驻资源 | active 模型单实例，候选模型不得和 active 并行常驻 |
| Wi-Fi/BLE | `network_manager` / `wifi_control` / BLE owner | 可降级资源 | active 场景正常，idle/standby 降低活跃度 |
| 共享 I2C | `i2c_manager` + 各 driver | 共享总线 | 新增 PMIC/RTC/IMU 轮询前必须评估触摸和 codec 控制面影响 |
| SD / 文件系统 | storage owner | 突发资源 | 避免和高频 LCD flush、音频播放、模型加载同时做大 IO |
| CPU 推理预算 | `power_policy` + feature owner | 可调度资源 | 后台推理低优先级，前台交互/提醒优先 |
| 内部 DMA RAM | 各 driver + `power_policy` 观测 | 稀缺资源 | 高压路径要避免屏幕大 flush、音频、Wi-Fi、SD 同时峰值 |

## 整机状态预算表

本表是第一版可落地预算口径。后续实现时，各模块只能消费 `power_policy` 输出的预算，不应自己解释“当前状态能不能跑”。

状态关系：

- `CHARGING` 是外部供电主状态，表示资源相对宽松。
- `MAINTENANCE` 是维护窗口，不等价于充电；它可以由 `CHARGING` 触发进入，但进入后与危险识别、普通音频和模型运行时互斥。
- `LOW_BATTERY` 是保护叠加状态，优先级高于 `ACTIVE / IDLE_DIM / STANDBY` 的普通预算。
- 第一阶段不进入 light sleep / deep sleep，只做运行态预算、允许/拒绝和日志闭环。

### `ACTIVE`

用户正在看屏幕、触摸交互、前台音频/录音运行，或 P0 告警正在处理。

| 资源 | 预算 |
| --- | --- |
| UI | 高亮，刷新延时保持当前 active 策略。 |
| Wi-Fi/BLE | 允许正常连接、配网、同步和前台请求。 |
| 音频输入/输出 | 允许播放/录音，但必须走 session 仲裁。 |
| 震动/触觉提醒 | 当前硬件未接入，只记录预算占位；未来接入后允许普通通知，P0 危险提醒可使用强震。 |
| 危险识别 | 允许正常后台监听；若麦克风被 P1 前台录音/语音助手占用，进入 `resource_blocked`。 |
| 传感器 | 允许正常采样；第一阶段仍按现有实现，后续再接统一 sensor policy。 |
| 维护任务 | 默认延后；用户显式触发的前台维护除外。 |

### `IDLE_DIM`

短时间无交互，但仍保持快速恢复。它不是待机，不应明显牺牲危险提醒及时性。

| 资源 | 预算 |
| --- | --- |
| UI | 降亮，降低 LVGL 调度频率；触摸、按键、告警立即回到 `ACTIVE`。 |
| Wi-Fi/BLE | 普通同步延后，连接保持；前台连接请求仍可唤回 `ACTIVE`。 |
| 音频输入/输出 | 停止非必要播放；前台音频继续按 P1 处理。 |
| 震动/触觉提醒 | 当前硬件未接入，只记录预算占位；未来接入后普通通知可降级，P0 危险提醒仍允许强震并唤回 `ACTIVE`。 |
| 危险识别 | 默认保持正常监听，不因短暂 dim 降频；仅在低电量、麦克风被占用、用户关闭后台监听时暂停或降级。 |
| 传感器 | 预算目标为降采样或事件驱动；当前实现不足时必须标记为“后续 sensor policy”。 |
| 维护任务 | P3/P4 延后。 |

### `STANDBY`

长时间无交互，屏幕关闭或最低亮度，但仍要求可快速唤醒。

| 资源 | 预算 |
| --- | --- |
| UI | 灭屏或最低亮，不做动画；只保留唤醒入口和必要状态。 |
| Wi-Fi/BLE | 只保留必要连接、低频心跳或用户显式要求的后台连接。 |
| 音频输入/输出 | 普通播放停止；P0 告警可唤醒输出。 |
| 震动/触觉提醒 | 当前硬件未接入，只记录预算占位；未来接入后 P0 危险提醒和低电量关键提醒仍允许，普通通知降级或聚合。 |
| 危险识别 | 普通模式默认暂停或极低频；只有用户显式打开“安全监听模式”且电量允许时才低频监听。 |
| 传感器 | 优先事件/中断，避免轮询；无统一 sensor policy 前不得假设已实现。 |
| 维护任务 | 默认禁止；只允许短小、可中断的后台收尾。 |

### `LOW_BATTERY_WARN`

低电量预警档，用于提前收缩后台资源。第一版只保留这一档，避免状态机过细。

| 资源 | 预算 |
| --- | --- |
| UI | 降亮，减少刷新；避免长时间亮屏。 |
| Wi-Fi/BLE | 限制主动同步；用户前台请求仍允许。 |
| 音频输入/输出 | 只允许前台必要音频和 P0 告警。 |
| 震动/触觉提醒 | 当前硬件未接入，只记录预算占位；未来接入后保留短促关键震动，普通通知弱化。 |
| 危险识别 | 默认继续但允许保守降频；若用户未开启安全监听，可暂停后台监听。 |
| 传感器 | 降到最低可用采样。 |
| 维护任务 | 禁止。 |

首版阈值先作为可调占位：`LOW_BATTERY_WARN` 可从 `20%` 起步；最终必须根据 AXP2101 SOC、电压曲线和真机续航回归修正。

### `CHARGING`

外部供电或充电中。它表示资源宽松，但不自动授权维护任务与安全监听并发抢资源。

| 资源 | 预算 |
| --- | --- |
| UI | 可恢复正常策略，但必须避免常亮烧屏；长时间充电仍可进入 idle dim。 |
| Wi-Fi/BLE | 允许同步、下载和日志上传。 |
| 音频输入/输出 | 正常，但仍走 session 仲裁。 |
| 震动/触觉提醒 | 当前硬件未接入，只记录预算占位；未来接入后正常使用，P0 危险提醒仍优先。 |
| 危险识别 | 允许完整运行；模型替换、Model::test、日志大 IO 不得与它并发。 |
| 传感器 | 正常或维护任务需要的采样。 |
| 维护任务 | 允许进入 `MAINTENANCE` 窗口，但进入后按 `MAINTENANCE` 互斥规则执行。 |

### `MAINTENANCE`

OTA、模型替换、模型验证、日志导出、数据整理等维护场景。它是互斥工作窗口，不是普通后台状态。

| 资源 | 预算 |
| --- | --- |
| UI | 显示明确状态、进度和不可用原因。 |
| Wi-Fi/BLE | 按维护任务需要使用，禁止无关扫描/同步叠加。 |
| 音频输入/输出 | 默认暂停；P0 告警可中断维护任务。 |
| 震动/触觉提醒 | 当前硬件未接入，只记录预算占位；未来接入后普通通知暂停，P0 告警仍允许。 |
| 危险识别 | 默认暂停或退出运行时，避免模型 RAM/Flash 和麦克风冲突。 |
| 传感器 | 只保留维护任务需要的最小采样。 |
| 模型 RAM/Flash | 维护任务独占，禁止运行时同时加载替换。 |

## 状态切换条件与滞回

| 状态 | 进入条件 | 退出条件 | 滞回 / 最小保持 | 必须打点 |
| --- | --- | --- | --- | --- |
| `ACTIVE` | 触摸、按键、前台音频/录音、前台联网、P0 告警、用户主动点亮 | 无交互超时且无 P0/P1 活动 | P0/P1 活动期间不得降级 | `policy_state_change` |
| `IDLE_DIM` | `ACTIVE` 下无交互达到当前 idle 超时，且无 P0/P1 活动 | 触摸/按键/告警/前台请求；或继续无交互达到 standby 超时 | 首版沿用现有 5s idle-dim 经验，后续可调 | `ui_budget_change` |
| `STANDBY` | `IDLE_DIM` 下长时间无交互，且无前台音频、配网、OTA、告警 | 触摸/按键/告警/前台请求/充电策略要求 | standby 超时先作为可调占位，例如 30s/60s；真机验证后固定 | `policy_state_change` |
| `LOW_BATTERY_WARN` | SOC/电压连续满足 warn 阈值 N 次 | 电量恢复到 warn 退出阈值并连续稳定 N 次 | 进入/退出必须有回滞，避免电压抖动反复切换 | `battery_budget_change` |
| `CHARGING` | 外部电源可用或 PMIC 进入充电态 | 外部电源移除，且 PMIC 状态稳定 | 充电状态变化需连续确认，避免接触抖动 | `charge_state_change` |
| `MAINTENANCE` | 用户确认、充电窗口、OTA/模型/日志任务显式申请并被 `power_policy` 批准 | 任务完成、失败、用户取消、P0 告警抢占 | 同一时间只允许一个高压维护任务 | `maintenance_window_enter/exit` |

## 危险识别整机状态策略

危险识别必须区分用户开关、整机策略和资源阻塞，不能只用一个 `enabled` 表示全部状态。

| 整机状态 | `enabled_by_user` | `allowed_by_policy` | 资源阻塞处理 | 运行策略 | 提醒策略 |
| --- | --- | --- | --- | --- | --- |
| `ACTIVE` | 用户设置决定 | 默认允许 | 麦克风被 P1 占用时进入 `resource_blocked` | 正常监听 | 2/3 连续确认后进入 P0 提醒 |
| `IDLE_DIM` | 用户设置决定 | 默认允许 | 同 `ACTIVE` | 默认不降频；低电量或资源压力时才降级 | P0 唤回 `ACTIVE`，震动优先 |
| `STANDBY` | 用户设置决定 | 普通模式暂停或极低频；安全监听模式才允许低频 | 麦克风/内部 RAM 不足时暂停 | 低频或暂停，必须有可见状态 | P0 可唤醒屏幕和震动 |
| `LOW_BATTERY_WARN` | 用户设置决定 | 降频允许 | 资源不足直接暂停 | 保守降频或暂停 | 保留短促关键提醒 |
| `CHARGING` | 用户设置决定 | 默认允许 | 维护任务申请时让路 | 正常监听 | 正常 P0 策略 |
| `MAINTENANCE` | 用户设置不改变 | 默认不允许 | 释放麦克风、模型 RAM/Flash | 暂停或退出运行时 | P0 告警可中断维护任务 |

## 优先级

| 优先级 | 类型 | 示例 | 资源策略 |
| --- | --- | --- | --- |
| P0 | Emergency | 已确认危险后的提醒、低电量关键告警 | 可抢占普通播放和普通后台任务 |
| P1 | Foreground User | 语音助手、通话、录音、用户正在操作的页面 | 可暂停 P2 后台监听 |
| P2 | Safety Monitor | 危险声音后台监听 | 可长期运行，但必须服从 P0/P1 和低电量策略 |
| P3 | Maintenance | 同步、日志、缓存、模型预加载 | 只在资源宽松或充电时运行 |
| P4 | Cosmetic | 动画、非必要刷新、装饰效果 | idle/standby 立即降级或停止 |

## 冲突规则

### 麦克风

- 语音助手/录音前台持有麦克风时，危险识别必须暂停或进入 `resource_blocked`。
- 危险识别后台不可强抢前台麦克风。
- 麦克风被占用要有日志：申请者、当前 owner、拒绝原因。

### 喇叭

- 危险提醒可抢占普通播放。
- 普通播放不得抢占危险提醒。
- 提醒播放结束后，是否恢复普通播放由播放 owner 决定，不由 `app_alert_manager` 盲目恢复。

### 震动/触觉提醒

- 当前板级硬件尚未增加震动/马达，因此第一阶段不得假设存在可调用的 haptic driver。
- 听障危险提醒的产品目标仍默认以震动为第一提醒通道，屏幕和音频只作为当前过渡期辅助确认。
- `Alerting` 首次进入允许强震；持续危险期间只允许按固定周期补较轻提醒，不能每个推理窗口重复强震。
- `LOW_BATTERY_WARN` 下仍保留短促关键震动，但禁止连续重震轰炸。
- 未接入硬件前，应在预算表中继续保留 owner 占位和状态字段，但实现层只能记录 `haptic_unavailable`，不能调用不存在的驱动。

### 屏幕

- 后台服务不得直接持有 `lv_obj_t *`。
- 后台服务只发布状态；UI controller 读取快照并渲染。
- `power_policy` 只控制亮度、刷新预算和屏幕开关语义，不直接改页面控件。

### Wi-Fi/BLE

- 前台联网、配网、OTA 优先于普通后台同步。
- standby 下只保留必要心跳或显式用户需求。
- charging 下允许批量同步。

### 模型与 AI 推理

- active 模型只保留一个常驻实例。
- 未上板验证模型只能在 sample app 或维护场景验证，不得自动并入主固件 active。
- 推理耗时、PSRAM、内部 RAM 要作为 `power_policy` 的预算输入。
- 模型替换、Model::test、日志大 IO 必须申请 `MAINTENANCE` 窗口；进入维护窗口后危险识别默认暂停或退出运行时。

### 内部 DMA RAM

- 避免同时触发大屏幕 flush、音频采集/播放、Wi-Fi 高吞吐、SD 大 IO、模型加载。
- 后续应增加轻量资源日志，至少记录进入高压组合时的可用 internal heap。

## 第一阶段最小落地

第一阶段只做框架闭环，不碰 deep sleep。

1. 新增 `power_policy` 计划接口和状态枚举。
   - 输入：`power_service` 快照、UI 交互时间、后台功能需求。
   - 输出：UI 刷新预算、网络省电建议、震动预算、后台任务运行许可。
2. 新增轻量 `background_service_manager`。
   - 先只管理危险识别后台开关。
   - 区分 `enabled_by_user` 与 `allowed_by_policy`。
3. 改造危险识别页面语义。
   - 页面只显示状态和开关。
   - 页面退出不再拥有 stop 生命周期。
4. 接入 `audio_codec` input session 的资源阻塞状态。
   - 麦克风被前台占用时，危险识别进入 `resource_blocked` 或暂停。
5. 增加资源事件日志。
   - `policy_state_change`
   - `ui_budget_change`
   - `battery_budget_change`
   - `charge_state_change`
   - `background_allowed_change`
   - `resource_acquire_denied`
   - `resource_preempt`
   - `resource_release`
   - `maintenance_window_enter/exit`

## 第二阶段

1. 把 UI `Active / Idle-Dim` 纳入 `power_policy` 统一输出。
2. 网络省电从 `official_chat` 局部策略扩展为整机策略输入。
3. 补 `STANDBY`：灭屏、停动画、降网络活跃、停普通音频。
4. 为危险识别增加 standby 安全监听模式和低频策略。
5. 增加震动硬件后，把震动预算从文档占位推进到 haptic driver owner 与 `app_alert_manager`。

## 第三阶段

1. 接入 RTC/PMIC 事件证据后，再讨论 light sleep/deep sleep。
2. 做 sensor manager 或 sensing policy。
3. 将 IMU/RTC/PMIC/触摸中断纳入唤醒策略。

## 不做什么

- 不直接上 deep sleep。
- 不新增一个大而全、所有模块都依赖的 `ResourceManager`。
- 不让 UI 直接抢麦克风、喇叭、Wi-Fi、PMIC 或模型资源。
- 不让危险识别页面继续作为后台危险识别功能 owner。
- 不把候选模型和 active 模型并行常驻。
- 不把 `glass_break / crash / impact` 静默并入当前 active 危险识别主线。
- 不把 `CHARGING` 当成所有维护任务都可并发运行的许可；高压维护必须进入 `MAINTENANCE`。

## 验证闭环

### 文档和检索

- `uv run python scripts/context/validate_context.py --level standard --q "手表 整体资源框架 power_policy background resource" --brief`

### 代码阶段验证

- 普通改动：`idf.py build`
- 修改 `sdkconfig`：必须 `idf.py fullclean` 后再 `idf.py build`
- 板端日志：
  - 电源状态变化日志
  - 后台功能 allow/deny 日志
  - audio input/output session owner 日志
  - haptic unavailable / budget / trigger 日志
  - danger detection `resource_blocked / monitoring / alerting` 状态日志
  - maintenance window enter/exit 日志
  - internal heap / psram 关键点日志

### 真机场景

1. 打开危险识别后台，退出页面，确认推理仍按策略运行。
2. 打开语音助手或录音，确认危险识别让出麦克风。
3. 播放普通音频时触发危险提醒，确认 P0 提醒可抢占。
4. 进入 idle/standby，确认 UI 降频/灭屏、危险识别按策略保持、降频或暂停。
5. 低电量预警模拟，确认后台任务收缩，不出现反复启停。
6. 充电状态，确认普通同步可以恢复，但模型验证/替换进入 `MAINTENANCE` 后会暂停危险识别。
7. 危险提醒触发时，确认当前记录 `haptic_unavailable`；增加硬件后再验证震动优先策略。

## 回滚策略

- `power_policy` 出问题：回退到现有 `ui_refresh_policy` 和局部 Wi-Fi 省电策略。
- `background_service_manager` 出问题：危险识别恢复页面生命周期启动/停止。
- 音频资源仲裁出问题：保留 `audio_codec` session owner，不启用抢占策略。
- standby 出问题：回退到 `Idle-Dim`，不进入灭屏或深睡。
