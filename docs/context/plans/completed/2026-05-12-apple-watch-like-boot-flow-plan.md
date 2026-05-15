---
id: apple-watch-like-boot-flow-plan-20260512
tags: plan, watch, startup, boot-flow, apple-watch-like, power-policy, background-service, danger-detection
summary: Apple Watch 风格开机启动流程 Phase 1 完成归档，固定 UI-first、会话授权、后台预算和 ui_first_frame_ready gate 的阶段化启动模型。
status: active
last_reviewed: 2026-05-15
memory_type: project_plan
scope: repo
owners: main/app/app_main.c, main/app/hardware_init.c, main/services/power_service.c, main/services/power_policy.c, main/services/power_policy.h, main/services/background_service_manager.c, main/services/background_service_manager.h, main/services/safety_monitor_session.c, main/services/safety_monitor_session.h, main/services/network_service.c, main/ui/lvgl_task.c
triggers: Apple Watch, boot flow, startup, 开机, 启动流程, power_policy, background_service_manager
evidence_level: design
---

# Apple Watch 风格开机启动流程计划

## 设计目标

把当前 `app_main()` 的线性启动脚本，收敛成更接近 Apple Watch / watchOS 的阶段化启动模型：

- **先可用**：屏幕首帧、触摸和主 UI 优先完成，用户先看到表可用。
- **后后台**：网络、AI、危险识别、同步、模型等重任务不能抢 UI 首帧资源。
- **会话化**：需要长期占麦克风、模型 RAM 或网络的功能，必须由用户前台动作或系统策略授权成 session。
- **可预算**：所有后台能力都被 `power_policy` 的资源预算允许或拒绝，而不是各模块自行解释“现在能不能跑”。
- **可观测**：每个阶段有明确日志，失败可降级，不让单个后台能力拖垮整机启动。

## Apple Watch 口径映射

公开 watchOS 规则里有三条对本项目最有参考价值：

- 普通 watchOS App 主要以前台运行为主；后台执行只给少数受系统预算的任务。
- workout、audio、location、extended runtime 等长期后台运行，都需要明确 session 类型和前台启动语义。
- 后台任务要快速完成，持续高 CPU 会被系统限制或终止。

参考入口：

- Apple Developer：`Background execution`，说明 watchOS App 默认以前台为主，后台仅适用于少数受限任务或会话。
- Apple Developer：`Using extended runtime sessions`，说明 extended runtime session 必须从 active/foreground 状态启动，并且持续高 CPU 可能被系统取消。
- Apple Developer：`WKApplicationRefreshBackgroundTask`，说明后台 refresh task 受系统预算限制。

映射到当前 ESP32-S3 手表固件：

- `lvgl_task` / 主屏首帧等价于 watchOS 的 foreground-first 体验，优先级最高。
- `background_service_manager` 不等价于“所有后台功能开机即跑”，而是 session owner。
- `safety_monitor_session` 是 Safety Monitor 会话生命周期 owner，负责把“应运行/应停止”翻译成危险识别 runtime 的 start/stop、错误恢复和运行确认。
- `danger_detection_service` 是 Safety Monitor session 的能力实现；是否运行由用户开关 + `power_policy` 预算共同决定。
- `network_service` 是后台联网状态层，不能阻塞 UI，也不能作为危险识别页面能否打开的前置条件。

## 启动阶段

### Stage 0A: Board Foundation

Owner：`hardware_init()`

只准备开机必须的板级基础能力：

- `NVS`
- `resource_fs`
- `audio_app`
- `SD`
- `audio_codec`
- `board_power`
- `button`

要求：

- 不等待 Wi-Fi 连通。
- 不初始化 LVGL 对象树。
- 不启动 ESP-DL 模型。
- 不启动危险识别采集。
- 失败分级：NVS 失败阻断；资源分区、SD、codec、PMIC 失败只记录并降级，除非后续实测证明必须阻断。

### Stage 0B: Display Foundation

Owner：`lvgl_task` 内的 `lv_port_init_small()`，以及 `components/lvgl_port` / `components/co5300_panel` / `components/touch_ft5x06`

只准备显示和触摸基础设施：

- `lv_init()`
- `CO5300` panel / QSPI / DMA flush path
- LVGL display buffer / flush callback
- `FT5x06/FT3168` touch input registration
- LVGL tick / input device

要求：

- Display Foundation 是板级 bring-up 的一部分，不是业务 UI。
- 这里可以初始化面板、触摸、显示缓冲，但不能启动网络、模型、危险识别采集。
- 任何可能增加 internal/DMA 压力的重任务都必须在 Display Foundation 完成之后再启动。
- 当前由 `startup_readiness` 发布 `ui_first_frame_ready` 只读标志，后台 Safety Monitor manager 等该标志后才进入策略循环。

### Stage 1: UI First Frame

Owner：`app_main()` 创建 `lvgl_task`，`lvgl_task` 内完成业务 UI 构建和事件绑定。

顺序：

1. `setup_ui()`
2. `ai_ui_controller_init()`
3. `danger_detection_controller_init()`
4. `wifi_management_controller_init()`
5. `events_init()`
6. `ui_refresh_policy_init()`

要求：

- 后台服务不得在此阶段直接操作 `lv_obj_t *`。
- 高压任务不得与首帧 flush 同时启动。
- UI 层在业务 UI 构建、控制器初始化、事件绑定和刷新策略初始化完成后发布 `ui_first_frame_ready` readiness 标志。

### Stage 2: Core Policy Ready

Owner：`power_service` + `power_policy`

职责：

- `power_service` 只做 PMIC / 电量快照数据源。
- `power_policy` 只发布预算：UI、网络、维护、危险识别、低电量、外部供电。
- `power_policy` 不启动模型、不抢麦、不改 LVGL 对象。

默认：

- 先保持现有 `ACTIVE / LOW_BATTERY_WARN / CHARGING` 薄骨架。
- 后续把 UI idle、standby、maintenance window 逐步纳入。

### Stage 3: Service Managers Ready

Owner：`background_service_manager`

职责：

- 启动后台 manager task。
- 记录用户开关和策略许可。
- 不在 task 创建瞬间启动重任务。
- 只计算 Safety Monitor session 的目标状态，不直接理解危险识别 runtime 的后端、错误恢复或 running 判定。

危险识别默认口径：

- `danger_enabled_by_user` 默认应为 `false`。
- 用户进入危险识别页时，表达的是“启动 Safety Monitor session”。
- session 启动后由 `background_service_manager` 接管；页面退出不直接 stop。
- 后续需要增加显式停止入口或持久化设置时，再单独设计。

### Stage 4: Deferred Background Work

Owner：各 service manager / domain owner

默认延后或受条件启动：

- `network_service_start()`：后台联网、状态轮询、云端可用性探测。
- `official_chat_service_init()`：只初始化服务入口，不主动开始前台会话。
- `danger_detection_service_start_with_backend()`：只有用户开启 Safety Monitor session 且 `power_policy` 允许时启动。
- 模型自检、模型替换、日志导出、OTA：必须申请 `MAINTENANCE` 语义，不与危险识别和首帧 flush 叠加。

当前已有真机证据：

- 危险识别若开机过早启动，会与 LVGL/CO5300 首帧 flush 争用 internal/DMA 资源并触发 `ESP_ERR_NO_MEM`。
- 延后到 UI 首帧后启动，可消除该启动期 flush 错误。

## 建议启动顺序

```text
app_main
  -> hardware_init                      // Stage 0A: Board Foundation
  -> create lvgl_task
       -> lv_port_init_small            // Stage 0B: Display Foundation
       -> setup_ui / events_init        // Stage 1: UI First Frame
  -> power_service_init/start           // Core Policy data source
  -> power_policy_start                 // Core Policy budget
  -> background_service_manager_start   // Service manager ready, no heavy session yet
  -> network_service_start              // Deferred background network
  -> official_chat_service_init         // Service entry only

danger_detection_ui_open
  -> background_service_manager_set_danger_detection_enabled(true)
  -> manager waits for UI/resource budget
  -> danger_detection_service_start_with_backend(ESP-DL)
```

## 初始化文件重构边界

建议做薄重构，但不建议现在引入新的大而全 `boot_manager` 或复杂初始化框架。

当前最值得重构的是 `app_main.c` 的启动编排可读性，而不是把 `hardware_init.c` 过早拆散：

- `app_main.c` 可拆成本地 stage 函数：`start_board_foundation()`、`start_display_and_ui()`、`start_core_policy()`、`start_service_managers()`、`start_deferred_services()`。
- 每个 stage 只表达启动顺序和失败降级，不持有长期业务循环。
- 每个 stage 增加 `boot_stage:*` 日志，用于冷启动日志对齐和板端复盘。
- `hardware_init()` 暂时继续作为 `Board Foundation` owner，收敛 NVS、资源分区、SD、codec、board_power 和 button。
- 不把 LVGL、网络等待、ESP-DL 模型、危险识别采集或后台 session 启动塞进 `hardware_init()`。
- 只有当 `hardware_init.c` 后续继续膨胀，或出现清晰的重复初始化/错误恢复边界时，再考虑按 `board_storage_init()`、`board_audio_foundation_init()`、`board_power_foundation_init()` 这类私有 helper 继续瘦身。

重构优先级：

1. 先落危险识别页 `安全监听` 开关，确保开机默认不启动 heavy session。
2. 再薄重构 `app_main.c`，让代码结构和本计划的启动阶段一一对应。
3. 保持 `hardware_init.c` 当前文件边界，只做必要注释和日志补强，不为了“看起来分层”新增跨文件跳转。

## 关键语义

### `Foreground-authorized session`

用户在前台页面启动的后台会话。它不是“页面生命周期功能”：

- 页面负责表达用户意图和展示状态。
- manager 负责保存开关、申请资源和启动/停止能力。
- service 负责状态机和对外快照。
- 页面退出不自动停止 session。

当前危险识别应按这个语义推进。

## 危险识别页 UI 开关

危险识别页需要新增一个明确的 UI 开关，名称建议为：

- `安全监听`

开关语义：

- 默认关闭：开机后不自动启动麦克风、ESP-DL 模型和推理 runtime。
- 打开开关：调用 `background_service_manager_set_danger_detection_enabled(true)`，请求启动 Safety Monitor session。
- 关闭开关：调用 `background_service_manager_set_danger_detection_enabled(false)`，请求停止 Safety Monitor session 并释放麦克风/模型资源。
- 退出页面：不改变开关状态，不调用 stop；若开关保持开启，后台继续监听。
- 再次进入页面：读取 `background_service_manager_get_snapshot()`，同步显示当前开关和运行状态。

UI 状态文案建议：

- `未开启`
- `正在启动`
- `正在监听`
- `资源占用，暂时等待`
- `低电量降级`
- `维护中暂停`
- `危险提醒中`
- `正在停止`

第一版边界：

- 不做 NVS 持久化；重启后恢复默认关闭。
- 不做全局设置页入口；先只放在危险识别页。
- 不把“进入页面”本身当作授权；必须由用户显式打开 `安全监听` 开关。
- 页面只表达用户意图和展示状态，不直接调用 `danger_detection_service_start/stop()`。

### `Boot service`

开机必须启动的轻量服务。它可以失败降级，但不得长时间阻塞 UI。

当前候选：

- `power_service`
- `power_policy`
- `background_service_manager`
- `network_service`

### `Heavy session`

会占用麦克风、模型 RAM、Flash IO、Wi-Fi 高吞吐或大量 internal/DMA 资源的工作。

当前候选：

- ESP-DL 危险识别 runtime
- 语音助手/录音
- 模型自检/替换
- OTA / 日志导出
- 大文件 SD / LittleFS IO

重任务必须延后到 UI 首帧后，并由 `power_policy` 或明确 session owner 许可。

## 与当前实现的差异

- 已将 `background_service_manager` 默认 `danger_enabled_by_user` 改为 false，冷启动不再自动启动麦克风、模型和推理 runtime。
- 已将原 5s defer 替换为 `ui_first_frame_ready` readiness gate；后台 Safety Monitor manager 不再用固定时间猜测 Display Foundation / UI First Frame 边界。
- 已将危险识别页改为显式 `安全监听` 开关；进入页面只刷新状态，不再自动开启 Safety Monitor session。
- 当前尚无设置项持久化；第一版先保持重启默认关闭，避免一次引入 NVS 和全局设置页决策。

## 最小落地顺序

1. 修改 `background_service_manager` 默认开关：`danger_enabled_by_user=false`。
2. 在危险识别页新增 `安全监听` 开关；开关打开时调用 `background_service_manager_set_danger_detection_enabled(true)`，关闭时调用 `background_service_manager_set_danger_detection_enabled(false)`。
3. 修改 `danger_detection_ui_open()`：进入页面只创建页面、刷新快照和显示当前开关状态，不自动开启 session。
4. 保留页面退出不 stop 的行为。
5. 已将 5s boot defer 替换成 `ui_first_frame_ready` readiness gate。
6. 新增 `safety_monitor_session` Module，将危险识别 runtime 的启动、停止、错误恢复、运行确认和失败退避从 `background_service_manager` 中收敛出来。
7. 薄重构 `app_main.c`：将启动编排拆成本地 stage 函数，并让函数名、日志名与本计划阶段一致；暂不新建大框架，不大拆 `hardware_init.c`。
8. 增加或调整日志：
   - `boot_stage: board_foundation_done`
   - `boot_stage: display_foundation_done`
   - `boot_stage: ui_task_created`
   - `boot_stage: ui_first_frame_ready`
   - `boot_stage: policy_ready`
   - `boot_stage: managers_ready`
   - `safety_monitor_session: enabled_by_user=1`
   - `background_allowed_change`

## Progress

- `[x]` 2026-05-12：`background_service_manager` 默认关闭 Safety Monitor session，冷启动不自动启动危险识别 runtime。
- `[x]` 2026-05-12：危险识别页新增 `安全监听` 开关；进入页面只刷新快照和开关状态，打开/关闭开关才调用 `background_service_manager_set_danger_detection_enabled()`。
- `[x]` 2026-05-12：薄重构 `app_main.c` 为本地 stage 函数，并在 `app_main.c` / `lvgl_task.c` 增加 `boot_stage:*` 启动边界日志。
- `[x]` 2026-05-12：`idf.py build` 通过；COM3 烧录后抓取 35s 冷启动日志，确认无自动 `background danger detection started`、无 `INFERENCE`、无 `Display flush failed`、`ESP_ERR_NO_MEM`、panic 或 Guru。
- `[x]` 2026-05-12：新增 `safety_monitor_session` Module，加深 Safety Monitor 会话生命周期；`background_service_manager` 不再直接调用 `danger_detection_service_start/stop` 或持有 ESP-DL 后端细节。
- `[x]` 2026-05-13：危险识别页状态文案继续对齐后台快照：`STOPPING` 优先显示 `正在停止`，用户已开启且策略允许但 `danger_runtime_running=false` 时显示 `正在启动`，避免页面把后台过渡态误报成 `未开启`。
- `[x]` 2026-05-13：上板交互验证通过：用户确认进入危险识别页、打开/关闭 `安全监听`、页面文案和后台运行语义无问题；同轮冷启动日志确认默认不自启危险识别。
- `[x]` 2026-05-13：新增 `startup_readiness`，由 `lvgl_task` 在 `ui_first_frame_ready` 边界置位，`background_service_manager` 等该 readiness gate 后再进入 Safety Monitor 策略循环，替代固定 5s boot defer。
- `[x]` 2026-05-13：复烧 readiness gate 版本并抓取 `board_logs/2026-05-13-startup-readiness-gate-coldboot.log`：日志顺序为 `background_gate_wait: ui_first_frame_ready -> boot_stage: ui_first_frame_ready -> background_gate_ready: ui_first_frame_ready`；35s 内未出现自动 `background danger detection started`、`INFERENCE #`、`Display flush failed`、`ESP_ERR_NO_MEM`、panic 或 Guru。

## Completion Status

2026-05-15：本框架书按 Phase 1 范围完成。完成口径是“开机阶段边界、UI 首帧 gate、Safety Monitor 会话授权和后台重任务延后模型已落地”，不是“所有后续低功耗、持久化和唤醒能力都已实现”。

验收范围：

- `app_main.c` 已按本计划收敛为本地 stage 函数，启动日志能对齐 `Board Foundation / UI Task / Core Policy / Managers / Deferred Services`。
- `hardware_init()` 仍作为 `Board Foundation` owner，没有为了分层新增大而全 `boot_manager` 或跨文件初始化框架。
- Safety Monitor 冷启动默认关闭；危险识别页通过 `安全监听` 开关表达用户授权，页面退出不直接 stop。
- `safety_monitor_session` 已承接 runtime start/stop、FAILED 恢复、退避和运行确认。
- `startup_readiness` 已用真实 `ui_first_frame_ready` 替代固定 5s defer，后台 Safety Monitor manager 等 UI 首帧后再进入策略循环。
- 已有冷启动板端日志证明 `background_gate_wait -> ui_first_frame_ready -> background_gate_ready` 顺序正确，且未出现自动推理、显示 flush 失败、`ESP_ERR_NO_MEM`、panic 或 Guru。

后续 NVS 持久化 Safety Monitor 开关、STANDBY 唤醒、deep sleep、RTC/PMIC 唤醒和更复杂后台任务预算，应作为独立后续 gate 推进，不再阻塞本启动流程框架书完成。

## 验证闭环

以下清单作为后续复烧、回归和新增 gate 的验证入口。

### 冷启动

- 开机 10s 内主 UI 可见且触摸可用。
- 冷启动不应自动出现 `background danger detection started`。
- 冷启动不应自动出现 `INFERENCE #...`。
- 不出现 `Display flush failed`、`ESP_ERR_NO_MEM`、panic 或 Guru Meditation。

### 进页开启

- 进入危险识别页后不自动出现 `background danger detection started`。
- 打开 `安全监听` 开关后出现用户开关日志。
- 随后出现 `ESP-DL 单模型运行时已启动` 和 `background danger detection started`。
- `non_danger` 心跳约 3.2s 一条；`danger` 窗口即时输出。

### 关闭开关

- 关闭 `安全监听` 开关后，后台 manager 调用 stop。
- 日志出现危险识别停止和 audio input session release。
- 页面留在危险识别页时显示 `未开启`。

### 页面退出

- 从危险识别页返回主页后，推理心跳继续。
- 日志中不应出现页面退出触发的 `danger_detection_service_stop()`。

### 麦克风冲突

- 危险识别运行时打开录音/语音入口。
- 预期看到可解释的 resource blocked / acquire denied 日志。
- 不允许后台危险识别强抢前台 P1 麦克风。

## 不做什么

- 不把危险识别重新退回“页面退出就停”。
- 不把所有后台能力开机即跑。
- 不在 `power_policy` 里直接调用模型、音频或 LVGL。
- 不引入大而全 `ResourceManager`。
- 不直接上 deep sleep；仍按运行态省电、standby、RTC/PMIC、deep sleep 的阶段路线推进。
