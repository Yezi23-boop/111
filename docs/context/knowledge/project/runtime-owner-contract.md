---
id: runtime-owner-contract
tags: project, architecture, framework, runtime, owner, startup, resource-management, background-service
summary: 固定当前 ESP32-S3 手表固件的运行时 owner 合同：启动阶段、资源 owner、后台能力、调用方向和禁止加层边界。
last_reviewed: 2026-06-02
memory_type: project_knowledge
scope: repo
owners: main/app/app_main.c, main/app/hardware_init.c, main/ui/lvgl_task.c, main/services/power/power_policy.c, main/services/safety/background_service_manager.c, main/services/safety/safety_monitor_session.c, main/services/network/network_service.c, main/services/official_chat_service.c, components/audio_codec, components/network_manager, main/features/danger_detection/danger_detection_service.c, main/features/alerts/app_alert_manager.c
triggers: runtime owner, owner contract, framework, 启动阶段, 资源 owner, 后台能力, power_policy, background_service_manager, safety monitor, official_chat, ui_refresh_policy
evidence_level: design
status: active
---

# Runtime Owner Contract

## 目的

本文件固定当前手表固件的运行时框架合同，供后续新增功能、重构、subagent 复查和代码生成时判断：

- 某个状态或资源由谁拥有。
- 哪一层可以推进状态，哪一层只能读快照。
- 启动阶段允许做什么，禁止做什么。
- 后台能力应如何从用户意图变成长期运行 session。
- 什么时候可以新增 owner，什么时候必须沿用现有 owner。

本文件不是新计划，也不要求新增代码层。它把已完成的启动框架、资源框架、GUI Guider 边界、FreeRTOS owner snapshot 基线和 Safety Monitor 后台化结论收敛成一张可执行合同。

## 总原则

- 一个事实只能有一个写 owner。其他模块只能通过快照、语义 API 或事件观察它。
- UI 只表达用户意图和渲染状态，不拥有后台任务、硬件资源、功耗策略或重试恢复。
- Service 负责后台生命周期、状态推进、重试、超时和跨模块编排。
- Domain owner 负责资源语义，例如音频 session、网络状态、危险识别风险状态。
- Driver adapter 负责 SDK、器件、寄存器、时序和错误码翻译。
- 不新增大而全 `ResourceManager`、`resource_policy`、`session_router` 或默认 `ui_manager`。
- V1 不新增 `runtime lease` 仲裁中心；资源可用性先通过 owner snapshot、blocker 和 `power_policy` budget 表达。
- 资源结束必须由真实 owner 自己完成：谁打开谁关闭，谁持有谁发布 released/inactive snapshot。

## Agent 写代码默认动作

后续 agent 在本仓库新增功能、跨模块改动、低功耗、OTA、后台能力、音频/网络/危险识别协作时，先按本合同回答：

1. 这次改动属于哪个启动阶段。
2. 真实资源或状态的写 owner 是谁。
3. 调用方向是否仍符合 `App/UI -> Service -> Manager/Domain -> Driver Adapter -> Vendor/SDK`。
4. 是否只需要读快照、申请 session、请求 budget，还是确实需要新增窄 owner。
5. 是否触发禁止路径，例如把后台能力塞进 `hardware_init()`，或新增大而全 manager。

若本合同能回答落点，直接沿用本合同；不要从当前代码现状重新发明框架。

## FreeRTOS Owner Snapshot 合同

详细专项合同见 `owner-snapshot-lifecycle-freertos-contract.md`。本节只保留后续写代码时必须先遵守的核心边界。

当前 V1 运行时骨架采用 `FreeRTOS owner snapshot + power_budget`，不是中心化 `runtime lease`。

长期 owner 默认形态：

```text
owner task / owner 执行上下文
  -> 写自己的内部状态
  -> 对外发布只读 snapshot
  -> 接收 budget / command / notify
  -> 在自己资源域内执行降级、恢复或释放
```

FreeRTOS 原语使用口径：

- 长期服务用 task 表达，例如 `power_service`、`power_policy`、`network_service`、`official_chat_service`、`background_service_manager`、`sleep_coordinator`。
- 轻量状态变化用 task notification 唤醒，例如电源变化、预算变化、用户熄屏、音频状态变化。
- 带参数控制用 queue，例如服务启动/退出、网络同步请求、后台能力控制。
- 组合 readiness 用 event group，例如 UI 首帧、网络就绪、电源就绪。
- snapshot 和共享状态用 mutex、critical section 或 owner 内部锁保护。
- timeout、低频兜底和释放等待用 software timer 或带超时的 FreeRTOS wait。

snapshot API 合同：

- 推荐形态为 `xxx_get_snapshot(out)` 或返回小型 snapshot 值。
- getter 返回副本，不暴露内部可写对象。
- getter 不做 I/O、不阻塞等待硬件、不推进状态机、不顺手重试。
- 如果 task 未启动，可返回默认安全快照或明确错误，不在 getter 内创建 task。

资源结束合同：

```text
policy / UI / service 发出结束意图
  -> owner 收到 command / notify
  -> owner 停止接收新工作
  -> owner 等当前关键动作短收尾
  -> owner 释放 session / 关闭自己域内硬件
  -> owner 更新 snapshot 为 inactive / released
  -> power_policy 下一轮聚合看到资源已释放
```

禁止把资源结束实现为：

- `power_policy` 直接关闭硬件或 suspend task。
- 其他模块直接改 owner 内部 flag。
- 中心 runtime 直接删除“资源占用记录”并假装硬件已释放。
- UI 页面退出时直接 stop 长期后台 runtime。

## 分层合同

| 层 | 当前目录/模块 | 负责 | 不负责 |
| --- | --- | --- | --- |
| App/UI | `main/app`、`main/ui`、`main/features/*` | 启动入口、页面交互、用户 intent、状态展示 | 直接控制 Wi-Fi、I2S、PMIC、LCD panel、后台 runtime |
| Service | `main/services/*` | 后台生命周期、策略合成、任务编排、ready gate | 直接操作 LVGL 对象、解释底层寄存器、持有 SDK 细节 |
| Manager/Domain | `components/network_manager`、`components/audio_codec`、`components/espdl_inference`、`board_power`、`danger_detection_service`、`app_alert_manager` | 资源语义、状态机、session、领域快照 | 页面对象、启动阶段编排、跨领域总调度 |
| Driver Adapter | `lvgl_port`、`co5300_panel`、`touch_ft5x06`、`wifi_control`、`network_provisioning_adapter`、`ap_portal_adapter`、`axp2101` | SDK/器件适配、总线细节、时序和错误翻译 | 产品状态机、页面文案、后台策略 |
| Vendor/SDK | ESP-IDF、LVGL、ESP-DL、official_chat、芯片手册 | 原始能力 | 项目语义 |

## 启动阶段合同

| 阶段 | Owner | 允许做 | 禁止做 | 验收信号 |
| --- | --- | --- | --- | --- |
| Board Foundation | `hardware_init()` | NVS、资源文件系统、SD、audio codec、board power、button | LVGL 对象树、联网等待、ESP-DL 模型、危险识别采样、official_chat 前台会话 | `boot_stage: board_foundation_done` |
| Display Foundation | `lvgl_task` + `lv_port_init_small()` | LVGL、CO5300、显示 buffer、FT5x06/FT3168 touch、input device | 网络、模型、危险识别采集、后台重任务 | `boot_stage: display_foundation_done` |
| UI First Frame | `lvgl_task` | `setup_ui()`、controller init、events、`ui_refresh_policy_init()` | 后台服务直接改 `lv_obj_t *`、高压 IO/模型与首帧 flush 并发 | `boot_stage: ui_first_frame_ready` |
| Core Policy | `power_service` + `power_policy` | 电源快照、整机预算发布、wakeup evidence 服务 | 启停模型、抢麦、改 UI 亮度、启动联网 | `boot_stage: policy_ready` |
| Service Managers | `background_service_manager` | 创建后台 manager task、等待 `ui_first_frame_ready`、计算后台能力目标态 | task 创建瞬间启动重任务、直接理解模型后端 | `boot_stage: managers_ready`、`background_gate_ready` |
| Deferred Services | `network_service`、`official_chat_service` | 后台联网入口、AI 服务入口初始化 | 阻塞首屏、默认开启麦克风或模型 | `boot_stage: network_service_ready`、`official_chat_ready` |

## 资源 Owner 表

| 资源/事实 | 写 owner | 读者 | 合同 |
| --- | --- | --- | --- |
| UI 活跃度、STANDBY、刷新节奏 | `ui_refresh_policy` | `power_policy`、UI | `power_policy` 只读 activity snapshot，不调用 UI poll、亮度、LVGL 或面板接口 |
| 整机资源预算 | `power_policy` | `background_service_manager`、service/domain owner | 发布 `ACTIVE / STANDBY / CHARGING / LOW_BATTERY_WARN / MAINTENANCE` 等预算，不直接操作具体资源 |
| 后台安全监听开关 | `background_service_manager` | UI、`safety_monitor_session` | 保存用户意图，合成 `should_run`，不拥有危险识别 runtime |
| Safety Monitor 会话生命周期 | `safety_monitor_session` | `background_service_manager` | 把 `should_run` 翻译成 start/stop、FAILED 恢复、退避和运行确认 |
| 危险识别风险状态 | `danger_detection_service` | UI、`app_alert_manager` | 负责风险状态机、连续证据、hold/clear/cooldown 等产品语义 |
| ESP-DL 模型 runtime | `components/espdl_inference` | `danger_detection_service` | 负责模型加载、音频预处理、推理、后处理和模型资源 |
| 麦克风 input session | `audio_codec` | `official_chat`、ESP-DL、安全监听 manager | 所有读麦路径必须申请 input session；真实占用以 session snapshot 为准 |
| 喇叭 output session | `audio_codec` + `app_alert_manager` | official_chat、mp3/alert player | 普通播放走 output session；P0 危险提醒由 `app_alert_manager` 组织抢占 |
| AI 前台音频 | `official_chat_service` | `background_service_manager` | AI 对话页前台期间声明 foreground audio，Safety Monitor 暂停；退出后按用户开关恢复 |
| 网络连接/配网语义 | `network_manager` | `network_service`、UI | `network_service` 是 service-ready/兼容层，不继续承载新的产品联网语义 |
| Wi-Fi STA runtime | `wifi_control` | `network_manager`、official_chat helper | 只处理 STA runtime control，不处理 UI 文案或产品流程 |
| 显示/触摸驱动 | `lvgl_port`、`co5300_panel`、`touch_ft5x06` | `lvgl_task` | UI 通过 LVGL 线程和 port 层使用，不直接调用 panel/touch driver |
| PMIC/电源快照 | `board_power` + `power_service` | `power_policy`、UI | `power_policy` 不直接读 PMIC 寄存器；低功耗策略必须基于快照和证据 |

## 后台能力合同

### Safety Monitor

当前 Safety Monitor 是唯一已经接入后台 manager 的长期能力。

```text
危险识别页 UI 开关
  -> background_service_manager_set_danger_detection_enabled()
  -> background_service_manager task notification
  -> background_service_manager 读取 power budget + 音频资源快照
  -> safety_monitor_session should_run -> runtime lifecycle
  -> danger_detection_service
  -> espdl_inference
  -> app_alert_manager / UI snapshot
```

规则：

- Safety Monitor 默认按用户意图开启，但仍必须等 `ui_first_frame_ready`
  后由 `background_service_manager` 按 power budget 与麦克风 owner 合成
  `should_run`；不得在 `hardware_init()` 或 UI 首帧前直接启动模型。
- 进入危险识别页只展示和修改后台开关状态，不直接启动模型。
- 打开 `安全监听` 开关才表达后台监听意图。
- 页面退出不停止 Safety Monitor。
- AI 对话页前台期间暂停 Safety Monitor；退出后若开关仍开启再恢复。
- 低电量、维护窗口、麦克风占用必须表现为可解释的阻塞或降级，不吞掉用户开关。

#### 已确认骨架

截至 2026-05-25，Safety Monitor 后台骨架按以下口径视为当前正式实现边界，后续 agent 写代码必须沿用：

| 环节 | Owner | 固定职责 | 禁止扩张 |
| --- | --- | --- | --- |
| 用户入口 | `danger_detection_controller` | 显示状态、写入 `安全监听` 用户开关、退出页面不停止后台监听 | 直接调用 `danger_detection_service_start/stop()` 或拥有模型生命周期 |
| 后台目标态 | `background_service_manager` | 保存用户意图，通过 task notification 响应用户开关/前台音频/power budget 变化，读取 `power_policy` budget 与 `audio_codec` session snapshot，合成 Safety Monitor 是否应运行 | 变成模型 owner、提醒策略 owner、音频仲裁器或通用任务调度器 |
| 会话生命周期 | `safety_monitor_session` | 把 `should_run` 翻译成 start/stop、FAILED 恢复、退避和运行确认 | 解释 UI 文案、power budget、麦克风优先级或风险状态 |
| 风险语义 | `danger_detection_service` | 管 `MONITORING / SUSPICIOUS / ALERTING / COOLDOWN` 风险状态、连续证据、hold/clear/cooldown | 判断页面生命周期或后台运行授权 |
| 推理 runtime | `components/espdl_inference` | 模型加载、音频预处理、推理、后处理和 input session 占用 | 维护用户开关、power policy 或提醒编排 |
| P0 提醒 | `app_alert_manager` | 在正式 `ALERTING` 后组织 overlay/audio fallback 与普通音频抢占 | 判断模型阈值、累计连续窗口或管理 Safety Monitor 会话 |

当前骨架已经通过框架复查：未发现需要新增总管家、`ResourceManager`、`session_router` 或默认 `ui_manager` 的证据。后续扩展低功耗、OTA、haptic、事件日志或新后台能力时，应先在对应 owner 内补最小能力；只有出现两个以上真实调用方重复实现同一生命周期或错误恢复时，才允许按“允许新增抽象的门槛”重新评估。

### 新后台能力接入条件

新能力只有同时满足以下条件，才允许接入 `background_service_manager`：

- 有明确用户开关或系统授权来源。
- 有明确资源 owner 和可读快照。
- 有 `power_policy` 预算字段或能复用现有预算。
- 有独立 session/lifecycle owner，不把具体 runtime 细节塞进 manager。
- 有 source test、启动日志或真机日志能证明不会抢 UI 首帧和关键资源。

## 禁止路径

- UI 直接调用 `danger_detection_service_start/stop()` 管长期后台监听。
- UI 直接调用 `esp_wifi_*`、`wifi_prov_mgr_*`、`httpd_*`、`i2s_*`、`axp2101_*`、`co5300_panel_*`、`touch_ft5x06_*`。
- `hardware_init()` 初始化 LVGL 对象树、网络服务、official_chat 前台会话、ESP-DL runtime 或 Safety Monitor。
- `power_policy` 直接控制亮度、LVGL、Wi-Fi、音频 session、模型或具体维护任务。
- 任何非 owner 模块直接结束、删除、抢占或重置别的 owner 的资源状态。
- V1 新增 `runtime lease`、通用资源账本或中心化资源仲裁器来绕过 owner snapshot。
- `background_service_manager` 变成通用任务调度器、模型后端 owner、提醒策略 owner 或音频仲裁器。
- `LocalAudioCodecAdapter` 绕过 `audio_codec` session 直接读写麦克风/喇叭。
- GUI Guider generated 层承载产品状态机、资源生命周期、后台服务或跨任务同步。

## 允许新增抽象的门槛

默认不新增 owner 或中间层。只有出现以下证据时才重新评估：

- 两个以上真实调用方重复实现同一生命周期或错误恢复。
- 当前 owner 的 public API 已无法表达资源语义，只能靠调用方猜测内部状态。
- 多个页面或服务出现同类竞态、重复 start/stop、悬空对象或资源泄露。
- 新抽象能减少现有 owner 的职责，而不是把更多职责集中到一个“总管家”。

若触发重评估，应先新增 active plan，明确 owner、调用方向、禁止项和验收证据，再改代码。

## 验证合同

框架相关改动至少执行：

```powershell
uv run python scripts/context/validate_context.py --level light --q "runtime owner contract FreeRTOS owner snapshot power_policy safety monitor" --brief
```

只改 context 文档时执行：

```powershell
uv run python scripts/context/validate_context.py --level standard --q "runtime owner contract FreeRTOS owner snapshot power_policy safety monitor" --brief
```

改源码时按影响面补充对应 source test，并在确认 `export.ps1` 可用后运行：

```powershell
. D:\esp-idf\v5.5.3\esp-idf\export.ps1
idf.py build
```

板端冷启动至少检查：

- `boot_stage: board_foundation_done`
- `boot_stage: display_foundation_done`
- `boot_stage: ui_first_frame_ready`
- `background_gate_wait: ui_first_frame_ready`
- `background_gate_ready: ui_first_frame_ready`
- 无自动危险识别推理日志，除非用户已开启 `安全监听`
- 无 `Display flush failed`、`ESP_ERR_NO_MEM`、panic 或 Guru

## 关联文档

- `docs/context/knowledge/project/layering-boundary-map.md`
- `docs/context/knowledge/project/owner-snapshot-lifecycle-freertos-contract.md`
- `docs/context/knowledge/project/gui-guider-visual-editor-runtime-boundary.md`
- `docs/context/plans/completed/2026-05-12-watch-resource-framework-plan.md`
- `docs/context/plans/completed/2026-05-12-apple-watch-like-boot-flow-plan.md`
- `docs/context/knowledge/project/hearing-assist-danger-alert-firmware-mapping.md`
- `docs/context/knowledge/project/low-power-management-baseline.md`
