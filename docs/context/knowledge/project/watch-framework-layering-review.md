---
id: watch-framework-layering-review
tags: project, architecture, framework, layering, owner, resource-gate, review
summary: 记录 2026-07-07 多 agent 对当前 ESP32-S3 手表固件框架分层的审查结论：主干分层成立，但 network getter 副作用、BLE UI 重动作、memory_watch_service 膨胀、runtime gate 边界和天气模块混层需要优先收敛。
last_reviewed: 2026-07-07
memory_type: project_knowledge
scope: repo
owners: docs/context/knowledge/project/watch-framework-layering-review.md
triggers: 框架审查, 分层审查, owner 边界, 解耦, 架构问题, runtime gate, memory_watch_service, network_manager getter
evidence_level: review
status: active
---

# Watch Framework Layering Review

## 一句话结论

当前嵌入式框架主干仍成立：

```text
App/UI -> Service -> Manager/Domain -> Driver Adapter -> Vendor/SDK
```

不建议推倒重来，也不建议新增大 `ResourceManager`、`session_router` 或默认 `ui_manager`。真正的问题是：若继续加功能不收口，部分 service 会变成局部总管家，getter/状态推进边界会越来越粘，UI 点击路径会继续承载重资源动作。

## 审查范围

本次由主线程和多个只读 subagent 分别审查：

- 总框架与 owner 文档一致性。
- `power_policy`、`foreground_runtime_gate`、BLE/Hermes/ESP-DL 资源冲突。
- AI Memory Watch / Hermes 固件、server、endpoint 边界。
- `network_manager`、`network_service`、BLE provisioning、SoftAP portal 边界。
- UI/LVGL/generated/custom/runtime 越界风险。

本文件只记录架构判断，不代表已经完成代码整改。

## 当前合理的边界

- 启动主线基本正确：board foundation -> display foundation -> UI first frame -> core policy -> service managers -> deferred services。
- Safety Monitor 链路合理：UI 写用户意图，`background_service_manager` 合成目标态，`safety_monitor_session` 执行生命周期，`danger_detection_service` 管风险状态，`app_alert_manager` 管提醒编排。
- Hermes 大方向正确：ESP32 固件只面向 watch endpoint 和 device token，不直连 Hermes Dashboard、Hermes API Server 或 MiMo API key。
- `audio_codec` session owner 边界基本正确：official_chat、Hermes recorder、ESP-DL 不应绕过 input/output session。
- `power_policy` / `sleep_coordinator` 当前保持克制：只发布预算和 dry-run，不直接控制硬件、模型或 `esp_sleep_*`。
- `foreground_runtime_gate` 当前仍是薄 gate，没有直接 start/stop 业务 task 或释放硬件资源；`background_https_gate` 已于 2026-07-14 撤除，后台网络恢复为各 owner 自治。

## 主要问题

### P1: `network_manager_get_status()` 不是纯快照

`network_manager_get_status()` 会调用 `network_manager_refresh_runtime_state(false)`，而 refresh 内部会推进 `s_state`、清理或保存 pending provisioning 凭据，并处理 latest Wi-Fi 失败后的状态迁移。

这与项目规则“UI 高频路径只读快照，getter 不推进状态机”冲突。

风险：

- UI 或 `network_service` 只是读状态，却可能触发状态变化。
- 后续排查网络问题时，难区分“谁改变了状态”。
- `network_manager` owner 语义被 getter 模糊。

优先建议：

- 增加纯快照接口，例如 `network_manager_get_snapshot(out)`。
- UI 和 `network_service` 高频轮询只读 snapshot。
- 状态推进只保留在 `network_manager_task()`、provisioning event callback 和显式用户命令路径。

### P1: BLE enable 仍在 UI 点击路径做重资源动作

主界面 Bluetooth 点击路径会触碰 foreground runtime gate、`network_manager_set_ble_enabled(true)`，并在 `ESP_ERR_NO_MEM` 后延时重试。

这不是 Wi-Fi SDK 越层，但 UI 已经承载了重资源过程和等待。

风险：

- UI 线程被 BLE/NVS/internal RAM 峰值拖住。
- BLE 失败恢复策略散落在 UI。
- 后续其他页面也可能复制这种“UI 直接做重动作”的模式。

优先建议：

- UI 只提交“打开蓝牙”意图并展示 snapshot/toast。
- BLE enable 和失败重试放到 network owner 或 service task。
- 保留当前 fail-closed 语义，但不要让 UI 执行延时和重试。

### P1: `memory_watch_service` 已接近产品服务总线

`memory_watch_service` 当前同时承载：

- endpoint/NVS 配置。
- foreground gate。
- WebSocket 前台状态。
- HTTP upload/cancel/health。
- inbox store/worker。
- sync/pending conversation。
- 多个 queue 和 worker。

功能方向正确，但文件和 owner 内职责已经膨胀。

风险：

- 后续 V2.x/V3 功能继续塞入后，service 会变成局部总管家。
- worker、store、foreground、network client 的错误恢复互相缠绕。
- 小改动难以做 source test 和板端定位。

优先建议：

- 不改 public API，先拆 owner 内部文件。
- 保持 `memory_watch_service` 是唯一对外 owner。
- 可先拆为：

```text
memory_watch_service.c          对外 owner API / 主状态
memory_watch_inbox_store.c      inbox 缓存与摘要
memory_watch_sync_worker.c      sync / pending reply
memory_watch_upload_worker.c    upload / cancel / health job
memory_watch_foreground.c       foreground gate / WS 状态协调
```

### P2: foreground runtime gate 边界没有被框架文档充分吸收

项目文档强调 V1 不做 runtime lease 或中心化资源管家，但代码中仍存在 `foreground_runtime_gate`。它是强前台 owner 事实，不是硬件 owner；原 `background_https_gate` 已撤除。问题在于后续仍可能把 foreground gate 误用成通用仲裁层。

建议：

- 更新 `project-framework.md` 或新增 gate 专项卡。
- 明确 gate 不是 owner，不释放资源，不替业务 start/stop。
- gate 只表达 foreground owner、quiet window、acquire 失败原因和可观测状态。
- 禁止把 gate 扩展成通用资源账本。

### P2: `network_service` 兼容面偏宽

文档定义 `network_service` 是 service-ready / probe / legacy shim，但头文件仍暴露 connect、disconnect、reprovision、default transport 等产品动作入口。

风险：

- 新代码可能继续把网络产品语义写回 `network_service`。
- `network_manager` 与 `network_service` owner 边界长期重叠。

建议：

- 冻结 `network_service` 新增产品动作 API。
- 新 UI 和新产品网络动作继续走 `network_manager`。
- `network_service` 保留 service-ready、probe、power-save、legacy shim。
- source test 锁定 `network_service` 不直接调用 provisioning adapter / AP portal / BLE transport lifecycle。

### P2: `lvgl_port` 对 UI runtime 有反向调用味道

`lvgl_port` 通过前向声明调用 `ui_refresh_policy_notify_touch()`。这绕过了 include 依赖，但实际仍是 Driver Adapter -> UI/runtime policy 的反向调用。

另有已知例外：`ui_refresh_policy` 直接调 `co5300_panel_set_brightness_percent()`。

建议：

- 先不大改。
- 后续改为 input event callback 注册，由 UI/runtime policy 注册触摸活动回调。
- 将亮度控制例外明确记录为过渡路径，后续可收敛为 display runtime owner。

### P2: 天气模块混合 feature、service 和 HTTP adapter

天气模块当前直接读网络状态、直接调用 `esp_http_client`、持有具体云端请求逻辑。它更像 feature + service + SDK adapter 混写。

建议：

- 收敛为 `weather_service` 快照。
- HTTP 细节放入内部 `weather_http_client` 或 adapter。
- UI 只读天气 snapshot，不直接触发 HTTP SDK 路径。
- API key、location、URL 配置不要继续散落在 HTTP 函数里。

### P3: `power_policy` blocker 事实源仍不完整

当前 owner snapshot + budget 模式能支撑运行态 STANDBY，但还不能直接支撑自动 Light/Deep Sleep。

原因：

- `AUDIO_ACTIVE`、`NETWORK_CRITICAL`、`OTA_ACTIVE`、`PROVISIONING_ACTIVE`、`ALERT_ACTIVE` 等 blocker 语义仍偏预留。
- RTC/PMIC 唤醒证据还不等于 sleep wake cause 完整闭环。

建议：

- 继续保持 sleep dry-run。
- 真实 Light/Deep Sleep 前，先补 blocker 事实源和板端唤醒证据。
- 不要为了 sleep 提前引入 runtime lease。

## 推荐整改顺序

1. 收敛 `network_manager` getter 副作用。
   - 新增纯 snapshot。
   - UI 和 `network_service` 高频读取改为只读。
   - 状态推进迁回 owner task/event/command。

2. 将 BLE enable 从 UI 重路径移到 owner/task。
   - UI 提交意图。
   - network owner/service task 执行 enable、quiet window 和 retry。
   - UI 只展示 snapshot/toast。

3. 定义 runtime gate 边界。
   - 文档明确 gate 不是 owner、不是 lease、不是 ResourceManager。
   - 补 source test 或文档，避免后续误扩展。

4. 内部拆分 `memory_watch_service`。
   - 不改 public API。
   - 按 inbox、sync、upload、foreground 分文件。
   - 保持唯一对外 owner。

5. 收敛天气模块。
   - `weather_service` snapshot + 内部 HTTP client。
   - UI 不直接参与 HTTP 细节。

6. 清理 UI/runtime 反向依赖。
   - `lvgl_port` touch notify 改 callback 注册。
   - 亮度控制例外单独规划。

7. 补 `power_policy` blocker 和板端高压证据。
   - 公网 HTTPS 成功路径。
   - ESP-DL running -> Hermes/BLE foreground yield。
   - BLE internal heap low-water。
   - audio session release timeout。
   - sleep 继续 dry-run。

## 不建议做的事

- 不推倒现有 `App/UI -> Service -> Manager/Domain -> Driver Adapter -> Vendor/SDK` 分层。
- 不新增大 `ResourceManager`、`session_router`、默认 `ui_manager`。
- 不把 `foreground_runtime_gate` 升级成通用资源账本。
- 不让 `power_policy` 直接停 task、关硬件或释放 session。
- 不把 Hermes/MiMo/API key 下放到 ESP32 固件。
- 不把 UI 页面退出解释为长期后台能力停止。

## 后续判断口径

新增或重构功能时，优先回答：

```text
1. 这是不是用户 intent 或展示？是则在 UI。
2. 这是不是长期 task / retry / ready / timeout？是则在 Service。
3. 这是不是某个资源或领域状态的唯一真相？是则在 Domain owner。
4. 这是不是 SDK / 协议 / 寄存器 / 总线时序？是则在 Driver Adapter。
5. 这是不是板级接线、GPIO、I2C 地址、电源轨事实？是则在 Board BSP。
```

跨层只使用：

```text
command / request
snapshot
FreeRTOS queue / notification / event group / mutex
```

不要让 getter 推进状态，不要让 UI 做重资源动作，不要让兼容 shim 继续长出新产品语义。

