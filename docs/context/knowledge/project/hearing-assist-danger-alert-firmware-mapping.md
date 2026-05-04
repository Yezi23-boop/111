---
id: hearing-assist-danger-alert-firmware-mapping
tags: project, hearing-assist, danger-detection, firmware, mapping, esp-dl, alerts
summary: 将听障危险提醒设计参数映射到当前固件实现，明确各参数的归属模块、当前代码行为、实现状态与和目标架构的差距。
last_reviewed: 2026-05-05
memory_type: semantic
scope: repo
owners: main/features/danger_detection/danger_detection_service.c, main/features/alerts/app_alert_manager.c, components/espdl_inference, main/ui/custom/danger_detection_controller.c
triggers: firmware-mapping, danger-detection, app_alert_manager, espdl_runtime, threshold, cooldown, module-mapping
evidence_level: observed
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
  - `DS-CNN` 默认 `0.80`
  - `DS-TCN` 默认 `0.35`

### `espdl_audio_runtime`

- 负责板端实时音频采集、重采样、窗口滑动、Fbank 生成、模型推理和回调输出。
- 当前 `window_size_ms`、`stride_ms`、音频输入会话归属都落在这里。
- 不负责提醒策略，也不负责用户可见状态机。

### `danger_detection_service`

- 负责把单窗推理结果变成连续窗口确认、清除、hold 和对外快照。
- 当前最接近“风险融合层”的归属模块就是这里。
- 但它还没有把 `Suspicious / Alerting / Cooldown` 作为显式公共状态发布出来。

### `app_alert_manager`

- 负责真正拉起用户提醒动作。
- 当前能力仍是：
  - 一次性 warning 音频播放
  - 红色危险覆盖层
- 还不是“震动优先、持续提醒、分级提醒”的 hearing-assist 产品提醒层。

### `danger_detection_controller`

- 负责危险识别页的页面级 UI 表现。
- 当前页面会：
  - 进入时启动 ESP-DL 后端
  - 离开时停止服务
  - 关闭全局 overlay，改由页面自己显示短时告警态
- 因此当前实现更像“专页功能”，而不是后台长期运行的系统级危险提醒。

## 映射表

### 第一组：功能启停与运行前提

| 参数名 | 设计定义 | 当前归属 / 文件 | 当前状态 | 当前代码行为 | 主要差距 / 下一步 |
| --- | --- | --- | --- | --- | --- |
| `feature_enabled` | 控制危险提醒功能是否整体开启 | `danger_detection_controller.c` + `danger_detection_service.c` | 部分实现 | 当前主要由“进入危险识别页就 start、离开就 stop”体现，没有独立的系统级功能开关 | 后续应提升为真正的用户设置项，而不是页面生命周期副作用 |
| `runtime_ready_required` | 运行时未就绪时不得进入监听 | `danger_detection_service.c` / `espdl_audio_runtime.cpp` | 已实现 | `danger_detection_service_start_with_backend()` 会检查 init 和 backend；`espdl_audio_runtime_start()` 会检查 runner、audio codec、input session 是否成功 | 已有基础，但仍缺少更明确的“用户看到功能开启/未就绪”的状态映射 |
| `mic_resource_required` | 麦克风资源不可用时不得启动 | `espdl_audio_runtime.cpp` | 已实现 | 启动时显式 `audio_codec_acquire_input(AUDIO_CODEC_OWNER_ESPDL_INFERENCE, 0U)`，失败则启动失败 | 后续可以把“资源被占用”单独映射成更可解释的错误状态 |
| `background_run_allowed` | 离开专页后是否允许后台继续工作 | `danger_detection_controller.c` | 未实现 | 当前离开危险识别页就调用 `danger_detection_service_stop()` 停掉能力 | 若按 hearing-assist 产品主线推进，应把后台运行变成默认主路径 |
| `ui_page_required` | 功能是否必须绑定专页可见 | `danger_detection_controller.c` | 当前实现等价于“是” | 当前用户必须进入危险识别页才会启动识别链路 | 产品目标是不依赖专页；后续应把页面改成“查看状态”，而不是“承载功能本体” |

### 第二组：状态机参数

| 参数名 | 设计定义 | 当前归属 / 文件 | 当前状态 | 当前代码行为 | 主要差距 / 下一步 |
| --- | --- | --- | --- | --- | --- |
| `suspicious_threshold` | 进入可疑观察态的门槛 | 无明确 owner | 未实现 | 当前没有独立 `Suspicious` 阈值或状态，只有模型二分类结果和服务内计数 | 后续若要做更完整的 hearing-assist 状态机，需要先把 `Suspicious` 从内部概念变成显式状态 |
| `alert_threshold` | 单窗正式危险证据门槛 | `espdl_model_runner.cpp` / `espdl_model_runner.h` | 已实现 | `fill_binary_threshold_result()` 内用 `danger_prob >= threshold` 决定 danger 标签；DS-CNN 默认阈值为 `0.80` | 当前门槛仍是模型 runner 内常量；若后续做 profile 化，应提升为部署策略的一部分 |
| `enter_alert_rule` | 连续证据确认进入告警 | `danger_detection_service.c` | 已实现（固定规则） | 当前写死为 `2` 个连续 danger 窗口后才触发告警 | 规则已存在，但仍是写死常量，不是可配置 profile |
| `clear_rule` | 连续安全证据确认退出告警 | `danger_detection_service.c` | 已实现（固定规则） | 当前写死为 hold 到期后连续 `3` 个 non-danger 窗口才允许清除 | 已具备基础回滞，但还没有显式 `Cooldown` 阶段 |
| `min_alert_hold_ms` | 正式告警最短保持时长 | `danger_detection_service.c` | 已实现 | 当前 `ESPDL_ALERT_HOLD_MS = 2000U`，未到 hold 时间不会清除 | 已实现，但仍是硬编码常量 |
| `cooldown_ms` | 清除后短暂抑制重复强提醒 | 无明确 owner | 未实现 | 当前只有 hold + clear，没有单独 cooldown 状态或计时 | 后续需要由 `danger_detection_service` 或更高层风险状态机显式接管 |
| `realert_rule` | 持续危险时如何继续补提醒 | `app_alert_manager.c`（弱相关） | 未实现 | 当前同源 active 告警会被 dedupe，不会形成“持续提醒节奏” | 产品目标是持续提醒；后续应引入明确的 sustain/realert 策略 |
| `hysteresis_enabled` | 进入与退出采用非对称稳态机制 | `danger_detection_service.c` | 部分实现 | 当前通过“2 窗进入 + 3 窗清除 + hold”形成隐式回滞 | 效果已有，但没有显式 profile，也没有 `Suspicious/Cooldown` 完整状态发布 |

### 第三组：提醒参数

| 参数名 | 设计定义 | 当前归属 / 文件 | 当前状态 | 当前代码行为 | 主要差距 / 下一步 |
| --- | --- | --- | --- | --- | --- |
| `suspicious_user_visible` | 可疑态是否直接对用户可见 | 无明确 owner | 未实现（当前等效为 false） | 当前用户界面没有 `Suspicious` 公共状态，只在正式告警时看到明显变化 | 与设计建议一致，但只是“缺失”而不是“有意识实现” |
| `initial_alert_vibration_pattern` | 首次正式告警的强震模式 | 未在本次主路径中发现明确 owner | 未实现 | 当前提醒主路径是 warning 音频 + 红色 overlay，没有看到清晰的震动/haptic 实现接入 | 若产品继续走 hearing-assist 主线，应优先补 vibration-first owner |
| `sustain_alert_vibration_pattern` | 持续危险时的后续补提醒模式 | 无明确 owner | 未实现 | 当前没有持续提醒概念，因此也没有持续震动模式 | 后续应和 `realert_rule` 一起设计，而不是只在 `app_alert_manager` 堆逻辑 |
| `sustain_alert_repeat_interval_ms` | 持续提醒节奏间隔 | 无明确 owner | 未实现 | 当前没有周期性补提醒时钟 | 后续需要显式 owner，建议不要塞进模型层 |
| `alert_screen_style` | 正式危险态的屏幕样式 | `display_alert_adapter.c` + `danger_detection_view/controller.c` | 部分实现 | 当前已有红色危险 overlay，以及页面内的 danger 可见态 | 已有“危险态视觉信号”，但还不是统一产品样式规范 |
| `alert_screen_persistence_policy` | 屏幕危险态如何保持与退出 | `danger_detection_controller.c` + `app_alert_manager.c` | 部分实现 | 全局 overlay 跟随 `danger_overlay_active`；页面内 `s_alert_visible` 通过 `2000ms` 定时器短暂显示 | 当前页面逻辑和全局逻辑是两套，仍需统一到状态机驱动 |

### 第四组：用户可配置项

| 参数名 | 设计定义 | 当前归属 / 文件 | 当前状态 | 当前代码行为 | 主要差距 / 下一步 |
| --- | --- | --- | --- | --- | --- |
| `sensitivity_mode` | 保守/标准/敏感模式 | 无明确 owner | 未实现 | 当前没有用户级灵敏度模式映射 | 后续应通过 profile 映射一组内部阈值，而不是直接暴露原始数字 |
| `notification_mode` | 仅震动或震动+屏幕等策略 | 无明确 owner | 未实现 | 当前实装路线更像“音频 + 屏幕”，且没有用户切换入口 | 与 hearing-assist 目标存在明显偏差，后续应改成震动优先 |
| `sustain_alert_enabled` | 是否允许持续提醒 | 无明确 owner | 未实现 | 当前只有首次 raise，不存在“持续提醒开关” | 这是 hearing-assist 产品差异点之一，值得进入后续实现优先级 |
| `event_log_enabled` | 是否记录最近危险事件 | 无明确 owner | 未实现 | 当前只看到实时日志输出，没有明确事件日志能力 | 若后续需要建立用户信任或做回放，这一项很有价值 |

### 第五组：工程内部参数

| 参数名 | 设计定义 | 当前归属 / 文件 | 当前状态 | 当前代码行为 | 主要差距 / 下一步 |
| --- | --- | --- | --- | --- | --- |
| `window_size_ms` | 单次推理音频窗口长度 | `espdl_audio_runtime.cpp` | 已实现（固定） | 当前 runtime 维护固定长度滑窗并在窗口填满后推理 | 建议后续通过 profile 名称对外表达，而不是散落在实现细节中 |
| `stride_ms` | 相邻推理窗口步长 | `espdl_audio_runtime.cpp` | 已实现（固定） | 当前 `kStrideMs = 300U` | 已有基础，但尚未 profile 化 |
| `feature_pipeline_profile` | 当前特征提取方案标识 | `espdl_audio_runtime.cpp`（隐式） | 部分实现 | 当前实际上是固定 Fbank 流水线，但没有正式 profile 名称 | 若后续允许换特征，应先引入稳定 profile ID，而不是只改实现 |
| `model_profile_id` | 当前启用模型版本标识 | `espdl_audio_runtime.cpp` + `espdl_model_runner.cpp` | 部分实现 | 当前通过 `"dscnn_v3.3"` 和内置 rodata runner 间接表达 active 模型 | 已能识别当前模型，但还不是统一版本治理接口 |
| `danger_class_profile` | 当前 active danger 事件边界 | 设计层文档 + `danger_detection_service` 二分类语义 | 部分实现 | 当前服务层只区分 `danger/non_danger`，训练和产品边界在文档中已固定为 `siren/horn/alarm` | 产品定义已稳定，但固件侧没有独立 profile 名称或切换接口 |
| `deployment_threshold_profile` | 当前整套后处理参数配置标识 | 无明确 owner | 未实现 | 当前阈值、confirm windows、clear windows、hold 分散在 runner 和 service 常量里 | 后续若做版本化和 A/B，建议显式引入 profile 而不是继续散落常量 |

## 当前实现和目标产品之间的关键差距

### 1. 当前更像“专页音频识别功能”，不是系统级 hearing-assist 提醒

- 进入危险识别页时才启动服务。
- 离开页面时就停止服务。
- 这说明当前实现依赖专页存在，而不是后台长期守护。

### 2. 当前已经有连续确认、清除和 hold，但还不是完整的公共状态机

- 已有：
  - `alert_threshold`
  - 连续 `2` 个 danger 窗口确认
  - 连续 `3` 个 non-danger 窗口清除
  - `2000ms` hold
- 仍缺：
  - 显式 `Suspicious`
  - 显式 `Cooldown`
  - 可持续补提醒的 `realert_rule`

### 3. 当前提醒层还不是“震动优先”

- 当前主路径是：
  - `app_alert_manager_raise()`
  - danger overlay
  - `audio_alert_player_play_warning_once()`
- 本次核对范围内没有看到清晰的 vibration/haptic 主提醒链。
- 这与 hearing-assist 产品路线存在明显偏差。

### 4. 当前参数已经分布在多个归属模块中，但还没有统一 profile

- `alert_threshold` 在 `espdl_model_runner`
- 连续窗口确认/清除/hold 在 `danger_detection_service`
- UI 呈现和短时 alert pulse 在 `danger_detection_controller`
- 全局音频与 overlay 在 `app_alert_manager`
- 这套分布本身没有问题，但缺少统一的部署策略 profile，会让后续调参容易漂。

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

### 第二优先级：把当前“隐式状态机”提升成显式状态发布

- 在 `danger_detection_service` 中补出统一状态：
  - `Monitoring`
  - `Suspicious`
  - `Alerting`
  - `Cooldown`
- 再让 UI 和提醒层都只消费这个状态，而不是自己拼局部逻辑。

### 第三优先级：把提醒层从“音频+overlay”推进到“震动优先+持续提醒”

- 首先补 vibration/haptic 归属模块
- 再补：
  - 首告警模式
  - 持续提醒节奏
  - 用户级开关和模式映射

## 一句话结论

- 当前固件已经具备“单模型 danger 概率 -> 连续确认 -> 一次性告警”的最小闭环。
- 但它距离“面向听障用户的系统级危险提醒产品”还差三步：
  - 从专页能力变成后台系统能力
  - 从隐式 confirm/clear 逻辑变成完整公共状态机
  - 从音频+红屏提示变成震动优先、持续提醒、用户可配置的 hearing-assist 提醒层
