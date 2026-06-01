---
id: project-framework
tags: project, framework, architecture, owner, startup, runtime, context
summary: 当前 ESP32-S3 手表固件的整体项目框架总图，串联启动阶段、分层边界、owner、资源预算、后台能力和上下文更新规则。
last_reviewed: 2026-06-01
memory_type: project_knowledge
scope: repo
owners: docs/context/knowledge/project/project-framework.md, docs/context/knowledge/project/runtime-owner-contract.md, docs/context/knowledge/project/layering-boundary-map.md
triggers: 项目框架, 整体框架, 总框架, architecture, framework, owner, startup, runtime, 框架变化
evidence_level: design
status: active
---

# 项目框架

## 一句话结论

当前仓库是 `ESP32-S3 + ESP-IDF` 手表固件，整体框架不是“大总管”模式，而是：

```text
App/UI 表达用户意图
  -> Service 推进后台生命周期和策略
  -> Manager/Domain 拥有资源语义与 session
  -> Driver Adapter 适配 ESP-IDF / 器件 / 协议
  -> Vendor/SDK 提供原始能力
```

框架核心规则是：先找写 owner，再看调用方向；策略层发布预算或请求，真实资源只能由自己的 owner 执行。

## 本文定位

本文是整体框架总图，用来回答：

- 当前项目分哪几层。
- 启动链路如何推进。
- 主要资源由谁拥有。
- 后台能力如何长期运行。
- 低功耗和资源预算如何穿过系统。
- 什么变化算“框架变化”，必须同步更新本文。

本文不替代专项卡：

- 运行时 owner 细节：`runtime-owner-contract.md`
- 分层边界：`layering-boundary-map.md`
- 低功耗总框架：`low-power-framework-architecture.md`
- 网络配网架构：`network-provisioning-custom-upper-architecture.md`
- Safety Monitor / hearing-assist 映射：`hearing-assist-danger-alert-firmware-mapping.md`
- 目录职责：`main-directory-map.md`

## 启动阶段总图

正式入口在 `main/app/app_main.c`。启动阶段按下面顺序理解：

```text
Board Foundation
  -> Display Foundation
  -> UI First Frame
  -> Core Policy
  -> Service Managers
  -> Deferred Services
```

### Board Foundation

Owner：

- `main/app/app_main.c`
- `main/app/hardware_init.c`

职责：

- NVS、资源文件系统、SD、audio codec、board power、button 等板级基础能力。
- 不启动 LVGL 对象树，不等待联网，不启动危险识别推理。

### Display Foundation

Owner：

- `main/ui/lvgl_task.c`
- `components/lvgl_port`
- `components/co5300_panel`
- `components/touch_ft5x06`

职责：

- 初始化 LVGL、显示 buffer、CO5300、FT5x06/FT3168 input device。
- 不承载网络、模型、后台重任务。

### UI First Frame

Owner：

- `main/ui`
- `main/ui/generated`
- `main/ui/custom`

职责：

- `setup_ui()`、controller init、事件绑定、首帧可见。
- UI 只表达用户 intent 和展示快照，不直接操作底层 Wi-Fi、PMIC、I2S、LCD panel 或模型 runtime。

### Core Policy

Owner：

- `main/services/power_service.c`
- `main/services/power_policy.c`
- `main/services/wakeup_evidence_service.c`
- `main/services/system_time_service.c`

职责：

- 发布电源事实、系统时间事实、整机预算、RTC/PMIC 证据。
- 策略层不直接操作屏幕、Wi-Fi、音频、模型或 ESP sleep API。

### Service Managers

Owner：

- `main/services/background_service_manager.c`
- `main/services/safety_monitor_session.c`

职责：

- 等待 UI first frame 后，再推进长期后台能力。
- 保存用户意图，读取资源快照和 power budget，合成 `should_run`。
- 不变成模型 owner、音频仲裁器、提醒策略 owner 或通用任务调度器。

### Deferred Services

Owner：

- `main/services/network_service.c`
- `main/services/official_chat_service.c`

职责：

- UI 首帧后再推进网络、AI 服务、云端 ready 探测。
- 当前正式模型是“先起 UI，联网后台继续”，不要回退到“联网成功后再进 UI”。

## 分层与目录

当前 `main` 目录按四类职责组织：

```text
main/app       启动入口与板级初始化
main/services  后台 service、策略、ready gate、长期 session
main/features  业务 feature 和产品语义
main/ui        LVGL UI runtime、generated 页面、custom 控制器
```

`components` 目录承载可复用组件、domain owner、driver adapter 和 vendor 适配。

逻辑分层：

| 层 | 典型目录/模块 | 负责 | 不负责 |
| --- | --- | --- | --- |
| App/UI | `main/app`、`main/ui`、`main/features/*` | 启动入口、页面交互、用户 intent、状态展示 | 直接操作硬件、后台 runtime、功耗策略 |
| Service | `main/services/*` | 后台生命周期、状态推进、策略合成、任务协作 | 直接操作 LVGL 对象、解释寄存器、持有 SDK 细节 |
| Manager/Domain | `components/network_manager`、`components/audio_codec`、`board_power`、`danger_detection_service` | 资源语义、session、领域状态机、快照 | 页面对象、启动阶段编排、跨领域总调度 |
| Driver Adapter | `wifi_control`、`lvgl_port`、`co5300_panel`、`touch_ft5x06`、`axp2101` | SDK/器件适配、时序、错误码翻译 | 产品状态机、页面文案、后台策略 |
| Vendor/SDK | ESP-IDF、LVGL、ESP-DL、official_chat、芯片手册 | 原始能力 | 项目语义 |

## 主要 owner 地图

| 资源/事实 | 写 owner | 主要读者 | 框架规则 |
| --- | --- | --- | --- |
| UI 活跃度、刷新、STANDBY 事实 | `ui_refresh_policy` | `power_policy`、UI | 只发布 activity snapshot，不让 `power_policy` 控制 LVGL/亮度 |
| 整机资源预算 | `power_policy` | resource owners | 聚合 facts，发布 budget，不直接操作硬件 |
| PMIC/电源事实 | `board_power` + `power_service` | `power_policy`、UI | AXP2101 V1 只读，不写电源轨/sleep 寄存器 |
| RTC/系统时间事实 | `system_time_service` + `components/system_time` | UI、official_chat、日志 | RTC bootstrap、SNTP、RTC 写回，不让 UI 直接操作 SNTP/RTC |
| RTC/PMIC 唤醒证据 | `wakeup_evidence_service` | 低功耗计划、日志 | 只做证据，不拥有低功耗策略 |
| 网络连接/配网语义 | `network_manager` | UI、`network_service` | 项目网络统一门面 |
| Wi-Fi STA runtime | `wifi_control` | `network_manager`、网络预算消费者 | 只处理 STA runtime control 和 power save |
| 网络 ready 探测 | `network_service` | official_chat、后台服务 | 兼容 shim + service-ready，不继续承载新产品网络语义 |
| 音频 input/output session | `components/audio_codec` | official_chat、ESP-DL、alerts | 所有读麦/放音路径走 session |
| AI 对话服务 | `official_chat_service` | UI、background manager | 前台 AI 生命周期，不拥有网络底层 |
| Safety Monitor 目标态 | `background_service_manager` | `safety_monitor_session`、UI | 保存用户开关，合成 should_run，不拥有模型 runtime |
| Safety Monitor 生命周期 | `safety_monitor_session` | background manager | 把 should_run 翻译成 start/stop/retry |
| 危险识别风险状态 | `danger_detection_service` | UI、`app_alert_manager` | 负责风险状态机和连续证据融合 |
| 危险提醒编排 | `app_alert_manager` | UI、audio alert | 负责提醒动作，不解释模型阈值 |
| 模型推理 runtime | `components/espdl_inference` | `danger_detection_service` | 模型加载、预处理、推理、后处理 |
| ESP sleep 执行 | 后续 `sleep_coordinator` | `power_policy` budget | 唯一允许调用 `esp_sleep_*` 的窄 owner，默认 dry-run |

## 资源预算链路

当前资源预算不是一个大 manager，而是一条单向链：

```text
owner snapshot
  -> power_policy 聚合 facts
  -> power_budget snapshot
  -> 各 owner 消费预算
  -> 各 owner 发布下一轮执行状态或 blocker
```

规则：

- `power_policy` 是预算发布者，不是硬件执行者。
- `LOW_BATTERY_WARN`、`CHARGING`、`EXTERNAL_POWER` 是 flag 或预算修饰，不是独立产品主状态。
- 产品层 V1 主状态只保留 `ACTIVE / STANDBY`。
- `STANDBY` V1 是运行态省电，不进入 ESP sleep。
- `sleep_permission / sleep_blockers / sleep_interval_hint` 属于预算输出；真实 sleep 只能由 `sleep_coordinator` 显式测试执行。

## 后台能力链路

长期后台能力必须走“用户意图 -> service manager -> session owner -> domain/runtime”的链路。

Safety Monitor 当前正式链路：

```text
危险识别页 UI 开关
  -> background_service_manager_set_danger_detection_enabled()
  -> background_service_manager 读取 power budget + audio session snapshot
  -> safety_monitor_session should_run
  -> danger_detection_service
  -> espdl_inference
  -> app_alert_manager / UI snapshot
```

规则：

- UI 不能直接 start/stop 长期后台 runtime。
- 页面退出不等于停止后台能力。
- AI 前台音频、低电量、维护窗口、麦克风占用等必须表现为可解释 blocker。
- 新后台能力接入前必须先明确用户授权、资源 owner、snapshot、budget 字段和 session/lifecycle owner。

## 网络框架

当前网络长期主线：

```text
UI 网络入口
  -> network_manager
  -> network_provisioning_adapter / ap_portal_adapter / wifi_control
  -> ESP-IDF network_provisioning / esp_wifi / httpd
```

规则：

- `network_manager` 是项目网络统一门面。
- `wifi_control` 只负责 STA runtime control。
- `network_provisioning_adapter` 负责官方 provisioning manager 适配。
- `ap_portal_adapter` 负责自定义 AP 门户页面和 HTTPD 壳。
- `network_service` 是兼容 shim + ready 探测，不继续堆新产品网络语义。
- 旧 `components/wifi_provision` 已退场。

## UI 框架

UI 分三层理解：

```text
generated    GUI Guider 生成页面和资源
custom       手写控制器、视图桥接、字体、状态展示
runtime      lvgl_task、lvgl_port、ui_refresh_policy
```

规则：

- `generated` 不承载产品状态机、后台服务或跨任务同步。
- 手写 UI 逻辑放 `custom`。
- UI 高频路径默认只读快照，不做重 IO、网络启停、阻塞等待或硬件状态推进。
- 触摸/按键等输入事实由 UI/input owner 更新，再供策略层读取。

## 电源与低功耗框架

当前低功耗总线：

```text
ui_refresh_policy activity snapshot
  + power_service 电源快照
  + network/audio/background facts
  -> power_policy budget
  -> UI / network / background / future sleep owner 消费
```

当前已成立：

- AXP2101 / board power / power service 只读电源事实。
- `STANDBY` 运行态省电已落地。
- Wi-Fi PS / 非关键同步节流已接入预算。
- PCF85063ATL 和 AXP2101 IRQ bank 仍以证据链为主。

当前未成立：

- 自动 Light Sleep。
- 自动 Deep Sleep。
- PMIC rail 控制。
- AXP_IRQ 作为 MCU GPIO wake source。
- RTC_INT 作为 V1 主 wake source。

V1 sleep 路线：

- `sleep_coordinator` dry-run。
- ESP32-S3 internal RTC timer 显式 Light/Deep Sleep 实验。
- `RTC_INT(GPIO39)` / `AXP_IRQ(EXIO5)` 作为后续外部唤醒增强，不阻塞 timer-based 显式实验。

## 禁止路径

- 不新增大而全 `ResourceManager`、`resource_policy`、`system_power_manager`、`session_router` 或默认 `ui_manager`。
- 不让 UI 直接操作 Wi-Fi、I2S、PMIC、LCD panel、touch driver、ESP-DL runtime。
- 不让 `power_policy` 直接操作硬件、LVGL、Wi-Fi、音频、模型或 ESP sleep API。
- 不让 `background_service_manager` 变成通用任务调度器、模型 owner、提醒策略 owner 或音频仲裁器。
- 不让 `wakeup_evidence_service` 变成低功耗策略 owner。
- 不从历史 superseded 文档反推当前框架。

## 框架变化更新规则

发生下面任一变化，必须同步更新本文，并更新 `docs/context/CHANGELOG.md`：

- 启动阶段顺序、ready gate 或 UI first frame 策略变化。
- 新增、删除或替换长期 owner。
- 某个资源/事实的写 owner 改变。
- 跨层调用方向改变。
- 新增长期后台能力或长期 session。
- `power_policy` budget 字段、产品主状态或 sleep 许可语义变化。
- 网络配网长期主线变化。
- UI generated/custom/runtime 边界变化。
- 低功耗、Light Sleep、Deep Sleep、PMIC/RTC 唤醒策略变化。
- Safety Monitor / official_chat / audio session 仲裁边界变化。
- 上下文管理入口或“先读哪个框架文档”的规则变化。

不需要更新本文的变化：

- 单个函数内部 bugfix，且不改变 owner 或调用方向。
- 日志文案或 source test 调整，且不改变框架语义。
- driver adapter 内部寄存器修复，且不改变上层接口。
- 临时实验代码，且没有成为正式 owner 或长期路线。

更新本文时同时检查：

```powershell
uv run python scripts/context/validate_context.py --level standard --q "project framework runtime owner layering low power network ui background" --brief
```

如果框架变化同时改了入口路由或检索基准，再运行 routing 级校验。

## 阅读顺序

新 agent 遇到整体架构、跨模块改动、低功耗、后台能力、网络配网、UI/runtime 边界时，建议顺序：

1. `docs/context/INDEX.agent.md`
2. `docs/context/knowledge/project/project-profile.md`
3. 本文 `project-framework.md`
4. 命中的专项卡，例如 `runtime-owner-contract.md`、`low-power-framework-architecture.md`、`network-provisioning-custom-upper-architecture.md`
5. 只有专项卡无法回答 owner 或层级时，再读代码。

## 当前权威关系

- 本文是总图。
- `runtime-owner-contract.md` 是 owner 合同的细则。
- `layering-boundary-map.md` 是分层边界的细则。
- `low-power-framework-architecture.md` 是低功耗框架的细则。
- `project-profile.md` 是低 token 首读画像。
- 如果本文和专项卡冲突，以最近 `last_reviewed` 且状态为 `active` 的专项卡为事实源，然后回头更新本文消除冲突。
