---
id: hearing-assist-danger-alert-firmware-mapping
tags: project, hearing-assist, danger-detection, firmware, mapping, esp-dl, alerts
summary: 将听障危险提醒设计参数映射到当前固件实现，明确各参数的归属模块、当前代码行为、实现状态与和目标架构的差距。
last_reviewed: 2026-05-25
memory_type: semantic
scope: repo
owners: main/features/danger_detection/danger_detection_service.c, main/features/alerts/app_alert_manager.c, components/espdl_inference, main/ui/custom/danger_detection_controller.c
triggers: firmware-mapping, danger-detection, app_alert_manager, espdl_runtime, threshold, cooldown, module-mapping
evidence_level: observed
route_area: "Hearing assist / danger alerts"
status: active
---

# 听障危险提醒设计参数到当前固件实现的映射

## 目标

- 本文将以下设计层文档继续下钻到当前固件实现：
  - [hearing-assist-danger-alert-system-architecture.md](D:\esp32S3\111\docs\context\knowledge\project\hearing-assist-danger-alert-system-architecture.md)
  - [hearing-assist-danger-alert-state-machine-and-notification-policy.md](D:\esp32S3\111\docs\context\knowledge\project\hearing-assist-danger-alert-state-machine-and-notification-policy.md)
  - [hearing-assist-danger-alert-parameter-defaults-table.md](D:\esp32S3\111\docs\context\knowledge\project\hearing-assist-danger-alert-parameter-defaults-table.md)
- 目标不是再定义一遍“应该怎样”，而是明确：
  - 当前哪些参数已经有明确代码归属
  - 哪些只做了一半
  - 哪些还停留在设计层
  - 下一步真正该改哪个模块

## 本次核对的代码范围

- 风险融合与状态快照：
  - `main/features/danger_detection/danger_detection_service.c`
  - `main/features/danger_detection/danger_detection_service.h`
- 提醒编排：
  - `main/features/alerts/app_alert_manager.c`
  - `main/features/alerts/audio_alert_player.c`
  - `main/features/alerts/display_alert_adapter.c`
- 推理运行时与模型门限：
  - `components/espdl_inference/espdl_audio_runtime.cpp`
  - `components/espdl_inference/espdl_model_runner.cpp`
  - `components/espdl_inference/include/espdl_model_runner.h`
- 页面级展示层：
  - `main/ui/custom/danger_detection_controller.c`
  - `main/ui/custom/danger_detection_view.c`

## 当前模块分工

### `espdl_model_runner`

- 负责模型加载、INT8/Float 输入输出桥接、危险阈值判定和二分类标签落点。
- 当前 `alert_threshold` 的真正归属在这里，而不是 `danger_detection_service`。
- 当前已经按模型类型内置阈值：
  - `DS-CNN` 默认 `0.90`
  - `DS-TCN` 默认 `0.35`

### `espdl_audio_runtime`

- 负责板端实时音频采集、重采样、窗口滑动、Fbank 生成、模型推理和回调输出。
- 当前 `window_size_ms`、`stride_ms`、音频输入会话归属都落在这里。
- 不负责提醒策略，也不负责用户可见状态机。

### `danger_detection_service`

- 负责把单窗推理结果变成连续窗口确认、清除、hold 和对外快照。
- 当前最接近“风险融合层”的归属模块就是这里。
- 当前已经把 `Monitoring / Suspicious / Alerting / Cooldown` 作为 `risk_state` 显式公共状态发布出来。
- 当前已经发布部署策略 profile：`deployment_profile_id`、`danger_class_profile`、`confirm_windows`、`clear_windows`、`alert_hold_ms`、`cooldown_ms`。

### `app_alert_manager`

- 负责真正拉起用户提醒动作。
- 当前能力是：
  - 首次危险强震（`haptic_alert_player` 异步调用 DS2413 马达）
  - 一次性 warning 音频播放
  - 红色危险覆盖层
- 当前决策：AI 对话页前台期间由 `official_chat_service -> runtime_coordinator -> safety_monitor_policy` 暂停 Safety Monitor，退出 AI 对话后按安全监听开关恢复；不在 `app_alert_manager` 增加 official_chat speaking 特判。
- 已补齐“首次震动优先”的最小链路，但还不是“持续提醒、分级提醒、用户可配置”的完整 hearing-assist 产品提醒层。

### `danger_detection_controller`

- 负责危险识别页的页面级 UI 表现。
- 当前页面会：
  - 展示后台 Safety Monitor 状态
  - 提供 `安全监听` 用户开关
  - 通过 `safety_monitor_policy_set_enabled()` 表达用户意图
  - 页面退出时只返回主界面，不再停止危险识别 runtime
- 因此当前实现已经从“专页功能”提升为后台 Safety Monitor 的 UI 入口。

## 映射表

### 第一组：功能启停与运行前提

| 参数名 | 设计定义 | 当前归属 / 文件 | 当前状态 | 当前代码行为 | 主要差距 / 下一步 |
| --- | --- | --- | --- | --- | --- |
| `feature_enabled` | 控制危险提醒功能是否整体开启 | `danger_detection_controller.c` + `safety_monitor_policy.c` | 已实现（默认关闭、非持久化） | 固件启动后默认 `enabled_by_user=false`，Safety policy 默认不运行 Safety Monitor；用户在危险识别页开启 `安全监听` 开关后，才按 power budget 和麦克风 owner 运行 | 后续若需要重启后保持用户选择，再单独接 NVS 持久化 |
| `runtime_ready_required` | 运行时未就绪时不得进入监听 | `danger_detection_service.c` / `espdl_audio_runtime.cpp` | 已实现 | `danger_detection_service_start_with_backend()` 会检查 init 和 backend；`espdl_audio_runtime_start()` 会检查 runner、audio codec、input session 是否成功 | 已有基础，但仍缺少更明确的“用户看到功能开启/未就绪”的状态映射 |
| `mic_resource_required` | 麦克风资源不可用时不得启动 | `safety_monitor_policy.c` + `espdl_audio_runtime.cpp` + `audio_codec` | 已实现 | policy 读取 `audio_codec` input owner 快照并在前台音频占用时阻塞 Safety Monitor；ESP-DL runtime 启动时显式申请 `AUDIO_CODEC_OWNER_ESPDL_INFERENCE` input session | 已补 `block_reason`，后续若 UI 需要展示具体 owner 再补窄字段 |
| `background_run_allowed` | 离开专页后是否允许后台继续工作 | `safety_monitor_policy.c` + `safety_monitor_session.c` | 已实现 | 用户打开 `安全监听` 后，页面退出不再 stop；policy 按用户开关、power budget、麦克风资源和 coordinator block 继续运行或恢复，并通过 `should_run / block_reason` 发布目标态 | 后续重点转向 stop timeout、提醒层并发安全、持续提醒和事件记录 |
| `ui_page_required` | 功能是否必须绑定专页可见 | `danger_detection_controller.c` | 已解除专页依赖 | 页面只做状态展示和开关入口，不再拥有 start/stop 生命周期 | 产品目标已对齐；后续可考虑全局设置页或持久化入口 |

### 第二组：状态机参数

| 参数名 | 设计定义 | 当前归属 / 文件 | 当前状态 | 当前代码行为 | 主要差距 / 下一步 |
| --- | --- | --- | --- | --- | --- |
| `suspicious_threshold` | 进入可疑观察态的门槛 | `danger_detection_service.c`（基于模型 danger 标签） | 部分实现 | 当前第一个 ESP-DL danger 窗口进入 `SUSPICIOUS`，尚未单独引入低于正式阈值的可疑阈值 | 后续若模型输出分布稳定，可引入独立 suspicious threshold 或 sensitivity profile |
| `alert_threshold` | 单窗正式危险证据门槛 | `espdl_model_runner.cpp` / `espdl_model_runner.h` | 已实现 | `fill_binary_threshold_result()` 内用 `danger_prob >= threshold` 决定 danger 标签；DS-CNN 默认阈值为 `0.90` | 当前门槛仍是模型 runner 内常量；若后续做 profile 化，应提升为部署策略的一部分 |
| `enter_alert_rule` | 连续证据确认进入告警 | `danger_detection_service.c` | 已实现（固定规则） | 当前写死为 `2` 个连续 danger 窗口后才触发告警 | 规则已存在，但仍是写死常量，不是可配置 profile |
| `clear_rule` | 连续安全证据确认退出告警 | `danger_detection_service.c` | 已实现（固定规则） | 当前写死为 hold 到期后连续 `3` 个 non-danger 窗口才允许清除 | 已具备基础回滞，但还没有显式 `Cooldown` 阶段 |
| `min_alert_hold_ms` | 正式告警最短保持时长 | `danger_detection_service.c` | 已实现 | 当前 `ESPDL_ALERT_HOLD_MS = 2000U`，未到 hold 时间不会清除 | 已实现，但仍是硬编码常量 |
| `cooldown_ms` | 清除后短暂抑制重复强提醒 | `danger_detection_service.c` | 已实现 | `danger_detection_policy_profile_t.cooldown_ms` 当前为 `3000ms`，解除告警后进入 `COOLDOWN` 并抑制重复强提醒 | 后续需结合真机误报/漏报调参 |
| `realert_rule` | 持续危险时如何继续补提醒 | `app_alert_manager.c`（弱相关） | 未实现 | 当前同源 active 告警会被 dedupe，不会形成“持续提醒节奏” | 产品目标是持续提醒；后续应引入明确的 sustain/realert 策略 |
| `hysteresis_enabled` | 进入与退出采用非对称稳态机制 | `danger_detection_service.c` | 已实现 | 当前通过 `2` 窗确认、`3` 窗清除、`2000ms` hold 和 `3000ms` cooldown 形成显式回滞 | 后续通过 profile 调整，不直接暴露原始参数给用户 |

### 第三组：提醒参数

| 参数名 | 设计定义 | 当前归属 / 文件 | 当前状态 | 当前代码行为 | 主要差距 / 下一步 |
| --- | --- | --- | --- | --- | --- |
| `suspicious_user_visible` | 可疑态是否直接对用户可见 | 无明确 owner | 未实现（当前等效为 false） | 当前用户界面没有 `Suspicious` 公共状态，只在正式告警时看到明显变化 | 与设计建议一致，但只是“缺失”而不是“有意识实现” |
| `initial_alert_vibration_pattern` | 首次正式告警的强震模式 | `haptic_alert_player.c` + `board_ds2413_motor.c` + `app_alert_manager.c` | 已实现（首版） | `app_alert_manager` 在新的 danger raise 时调用 `haptic_alert_player_play_initial_danger_once()`；haptic player 用短生命周期 task 执行 `220ms on -> 90ms off -> 220ms on`，退出前兜底关马达；重复同源 active 告警不重复强震 | 后续需真机确认体感强度，并和持续提醒策略统一 |
| `sustain_alert_vibration_pattern` | 持续危险时的后续补提醒模式 | 无明确 owner | 未实现 | 当前没有持续提醒概念，因此也没有持续震动模式 | 后续应和 `realert_rule` 一起设计，而不是只在 `app_alert_manager` 堆逻辑 |
| `sustain_alert_repeat_interval_ms` | 持续提醒节奏间隔 | 无明确 owner | 未实现 | 当前没有周期性补提醒时钟 | 后续需要显式 owner，建议不要塞进模型层 |
| `alert_screen_style` | 正式危险态的屏幕样式 | `display_alert_adapter.c` + `danger_detection_view/controller.c` | 部分实现 | 当前已有红色危险 overlay，以及页面内的 danger 可见态 | 已有“危险态视觉信号”，但还不是统一产品样式规范 |
| `alert_screen_persistence_policy` | 屏幕危险态如何保持与退出 | `danger_detection_controller.c` + `app_alert_manager.c` | 已部分统一 | 危险识别页红色危险态跟随 `risk_state == ALERTING`；全局 overlay 仍由 `app_alert_manager` 编排 | 后续需继续验证页面内展示与全局 overlay 在后台场景下的一致性 |

### 第四组：用户可配置项

| 参数名 | 设计定义 | 当前归属 / 文件 | 当前状态 | 当前代码行为 | 主要差距 / 下一步 |
| --- | --- | --- | --- | --- | --- |
| `sensitivity_mode` | 保守/标准/敏感模式 | `danger_detection_service.c` + `espdl_audio_runtime.cpp` + `danger_detection_controller/view` | 已实现（首版，非持久化） | 危险识别页提供 `保守 / 标准 / 敏感` 三段选择；service 持有用户级 mode 并映射 ESP-DL 单窗阈值 `0.95 / 0.90 / 0.85`，runtime/runner 只接收数值阈值；UI 不显示原始数字 | 后续若需要重启保留用户选择，再单独接 NVS；Edge Impulse 旧后端暂不接入 |
| `notification_mode` | 仅震动或震动+屏幕等策略 | 无明确 owner | 未实现 | 当前实装路线已包含首次强震 + 音频 + 屏幕，但没有用户切换入口 | 后续若做用户配置，应基于 haptic/audio/display 三通道组合映射，不直接暴露底层开关 |
| `sustain_alert_enabled` | 是否允许持续提醒 | 无明确 owner | 未实现 | 当前只有首次 raise，不存在“持续提醒开关” | 这是 hearing-assist 产品差异点之一，值得进入后续实现优先级 |
| `event_log_enabled` | 是否记录最近危险事件 | 无明确 owner | 未实现 | 当前只看到实时日志输出，没有明确事件日志能力 | 若后续需要建立用户信任或做回放，这一项很有价值 |

### 第五组：工程内部参数

| 参数名 | 设计定义 | 当前归属 / 文件 | 当前状态 | 当前代码行为 | 主要差距 / 下一步 |
| --- | --- | --- | --- | --- | --- |
| `window_size_ms` | 单次推理音频窗口长度 | `espdl_audio_runtime.cpp` | 已实现（固定） | 当前 runtime 维护固定长度滑窗并在窗口填满后推理 | 建议后续通过 profile 名称对外表达，而不是散落在实现细节中 |
| `stride_ms` | 相邻推理窗口步长 | `espdl_audio_runtime.cpp` | 已实现（固定） | 当前 `kStrideMs = 300U` | 已有基础，但尚未 profile 化 |
| `feature_pipeline_profile` | 当前特征提取方案标识 | `espdl_audio_runtime.cpp`（隐式） | 部分实现 | 当前实际上是固定 Fbank 流水线，但没有正式 profile 名称 | 若后续允许换特征，应先引入稳定 profile ID，而不是只改实现 |
| `model_profile_id` | 当前启用模型版本标识 | `espdl_audio_runtime.cpp` + `espdl_model_runner.cpp` | 部分实现 | 当前通过 `"dscnn_v3.4_t90"` 和内置 rodata runner 间接表达 active 模型 | 已能识别当前模型，但还不是统一版本治理接口 |
| `danger_class_profile` | 当前 active danger 事件边界 | 设计层文档 + `danger_detection_service` 二分类语义 | 部分实现 | 当前服务层只区分 `danger/non_danger`，训练和产品边界在文档中已固定为 `siren/horn/alarm` | 产品定义已稳定，但固件侧没有独立 profile 名称或切换接口 |
| `deployment_threshold_profile` | 当前整套后处理参数配置标识 | `danger_detection_service.c` | 已实现（service 后处理部分） | `danger_detection_policy_profile_t` 已发布 confirm windows、clear windows、hold、cooldown 和 danger class profile；模型阈值仍归 `espdl_model_runner` | 后续若做版本化和 A/B，应把 runner 阈值 profile 与 service profile 建立统一版本映射 |

## 当前实现和目标产品之间的关键差距

### 1. 后台化主链路已完成，可观测性已补第一层

- 当前已经由 `safety_monitor_policy` 和 `safety_monitor_session` 承接后台运行。
- 危险识别页不再直接 start/stop runtime。
- `safety_monitor_policy_snapshot_t` 已发布 `should_run` 和 `block_reason`，UI 状态文案优先消费 policy 语义快照，不再重复组合多个布尔字段推导目标态。
- 后续如果需要展示具体麦克风 owner，可在不改变 owner 链路的前提下补一个窄的只读字段。

### 2. 风险状态机已显式化，但还缺持续提醒和事件记录

- 已有：
  - `alert_threshold`
  - 连续 `2` 个 danger 窗口确认
  - 连续 `3` 个 non-danger 窗口清除
  - `2000ms` hold
-  - 显式 `Suspicious`
-  - 显式 `Cooldown`
- 仍缺：
  - 可持续补提醒的 `realert_rule`
  - 最近事件记录
  - 用户级 sensitivity / sustain alert 策略

### 3. 当前提醒层已补首次强震，但仍缺持续提醒

- 当前主路径是：
  - `app_alert_manager_raise()`
  - danger overlay
  - `haptic_alert_player_play_initial_danger_once()`
  - `audio_alert_player_play_warning_once()`
- `haptic_alert_player` 只负责首次危险强震，不负责持续提醒、用户模式或事件记录。
- 与 hearing-assist 完整产品路线相比，后续重点从“有没有震动”转为“持续危险时如何补提醒、如何配置提醒模式”。

### 4. 当前参数 profile 已开始收敛，但模型阈值和 service 后处理仍需统一版本治理

- `alert_threshold` 在 `espdl_model_runner`
- 连续窗口确认/清除/hold/cooldown 在 `danger_detection_service`
- UI 呈现和短时 alert pulse 在 `danger_detection_controller`
- 全局音频与 overlay 在 `app_alert_manager`
- 这套分布本身没有问题，但后续如果要做 A/B、灵敏度模式或模型替换，需要让模型 runner 阈值 profile 与 service 后处理 profile 一起版本化。

## 建议的下一步实现顺序

### 第一优先级：先把模块归属关系固定清楚

- `espdl_model_runner`
  - 只负责模型级 danger 概率和门限决策
- `danger_detection_service`
  - 负责公共状态机、连续证据融合、hold/cooldown/realert
- `app_alert_manager`
  - 负责提醒编排，不负责推理或分类解释
- `danger_detection_controller`
  - 只负责页面展示，不再承载功能启停归属

### 第二优先级：处理后台服务质量风险

- 保持 `safety_monitor_policy` 当前薄边界：只发布 `should_run / block_reason`，不扩大成音频仲裁器或模型 owner。
- 单独处理嵌入式复查指出的质量 gate：
  - `espdl_audio_runtime_stop()` 超时时，service 层不能误认为底层资源一定已释放。当前已补第一层：stop 失败不清 `runtime_started/callback_registered`，session 不发布已停止；ESP-DL runtime 支持后续 stop/start 前补清资源。
  - `app_alert_manager` / `audio_alert_player` 的跨任务共享状态已补临界区保护，且显示/音频外部调用保持在锁外；`app_alert_manager_clear()` 已改为 hide 成功后提交 inactive，并用 generation 丢弃过期 raise/show 结果。
  - ESP-DL 回调在真正 raise/clear 前会二次检查 service 是否仍允许提交提醒，避免 stop/AI 前台抢麦期间 stale callback 重新触发告警。
  - ESP-DL 推理循环内 `pcm_float` 已从每窗动态分配改为任务启动时一次性分配；长期运行仍需板端堆余量证据。

### 第三优先级：把提醒层从“首次强震”推进到“持续提醒+用户策略”

- 后续应补：
  - 持续提醒节奏
  - 持续提醒开关
  - 用户级通知模式映射

## 一句话结论

- 当前固件已经具备“单模型 danger 概率 -> 连续确认 -> 一次性告警”的最小闭环。
- 也已经具备 Safety Monitor 后台运行主链路和显式风险状态机。
- 但它距离“面向听障用户的完整产品闭环”还差三步：
  - 长期运行内存证据、真实板端 stop/start 循环证据和 P0 提醒可感知效果证据
  - 持续提醒、事件记录和用户策略 profile
  - 从首次强震 + 音频 + 红屏推进到持续提醒、用户可配置的 hearing-assist 提醒层
