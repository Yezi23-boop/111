---
id: watch-foreground-session-lifecycle-plan
tags: context, plans, foreground-session, lifecycle, resource-arbitration, freertos, hermes, official-chat, ble, espdl
summary: 先盘点全系统动态资源的 owner 与生命周期，再为 Hermes、official_chat 和 BLE provisioning 建立完整的前台资源创建/销毁机制，并通过强前台 gate 和后台 quiesced ACK 完成可验证切换。
last_reviewed: 2026-07-29
memory_type: task
scope: task
owners: docs/context/plans/active/2026-07-14-watch-foreground-session-lifecycle-plan.md, main/services/memory_watch/memory_watch_service.c, main/services/official_chat_service.c, main/services/network/network_service.c, main/services/runtime_gate/foreground_runtime_gate.c, main/services/safety/background_service_manager.c
triggers: foreground session, resource create destroy, 前台资源, 创建销毁, Hermes切换, official chat切换, BLE provisioning生命周期
evidence_level: implementation
status: archived
---

# Watch Foreground Session Lifecycle 初步执行计划

## 一、目标

先让每个真实 service owner 内部具备完整、幂等、可确认的资源创建与销毁机制，再让 UI 或其他外部模块通过窄 API 表达“进入前台 / 离开前台”。

目标链路：

```text
UI/controller
  -> request_foreground(true/false)
  -> service owner task reconcile desired state
  -> acquire foreground gate
  -> 等待可抢占后台资源 quiesced
  -> create foreground runtime
  -> publish READY snapshot
  -> 页面退出或错误
  -> stop + destroy foreground runtime
  -> release foreground gate
  -> 后台 owner 按策略恢复
```

本计划不让 gate 创建或销毁资源。谁创建资源，谁负责停止、等待、释放和发布最终状态。

## 二、已确认决策

- 用户主动进入另一个强前台页面时，允许系统自动关闭旧 session，再启动新 session。
- 目标页面可以先显示，但重资源未 READY 前不能启动录音、WebSocket、BLE 或模型。
- 等待旧强前台释放最长 5 秒；超时后目标页面进入错误状态，由用户重试。
- 超时不得强制清空 gate、`vTaskDelete` 旧 task、释放仍可能被 callback 使用的内存或自动重启设备。
- service 控制 task、命令通知、snapshot 和小状态长期保留；只按 session 销毁 WS、TLS、BLE controller、模型、音频 session 和大 buffer。
- `background_https_gate` 已撤除，不在本计划恢复后台统一 token 或中央网络 job queue。
- 不新增大而全 `ResourceManager`、`session_router` 或默认全局页面管理器。

## 三、当前问题

### 1. Memory Watch / Hermes

- `memory_watch_service_set_foreground()` 当前会在调用上下文立即修改 foreground 状态，再向 service queue 投递同一状态。
- gate acquire 失败只记录日志，页面和后续业务仍可能继续，属于 fail-open。
- 多处 UI 路径会重复调用 `set_foreground(true)`，当前依赖布尔状态维持幂等，但没有统一 session state。
- WS、recorder、conversation reconcile、inbox 和 pending 的保留/销毁边界需要显式资源清单。

### 2. official_chat

- enter 命令在 service task 中申请 gate，但 acquire 失败只记录日志，仍会继续启动 instance/transport，属于 fail-open。
- 已有 create、shutdown、transport quiet period 和 audio 控制基础，但尚未统一为 foreground session state。
- leave/shutdown 命令如果投递失败，需要避免 gate 和实例长期残留。

### 3. BLE provisioning

- UI 点击回调同步执行 BLE enable 和 800ms 重试，可能阻塞 LVGL task。
- gate 只覆盖 BLE 初始化调用，enable 返回后立即 release，但 BLE controller/host/advertising 仍可能常驻，gate 生命周期和真实资源生命周期不一致。
- BLE 主要用于小程序配网，是否改成页面级 session 尚需最终确认。

### 4. foreground runtime gate

- `timeout_ms` 当前被忽略，实际是 fail-fast。
- `quiet_for()` / `is_quiet()` 已无生产调用者。
- `FUTURE_PAGE` 是过宽的预留 owner，不适合作为多个未来页面的共享身份。
- 同 owner 重复 acquire 没有引用计数；当前依靠各 service 的本地 held 状态避免嵌套。

### 5. Safety Monitor / ESP-DL 让路

- foreground acquire 后只异步通知 `background_service_manager`。
- 前台 service 当前不会等待 ESP-DL task、audio input session、model runner 和 callback 真正释放。
- `danger_detection_service_stop()` 已有约 2 秒同步停止与 cleanup 基础，但缺少面向强前台的 quiesced ACK。

## 四、资源分类

### 1. 长期控制面

开机初始化一次，正常运行中不销毁：

- service owner task。
- task notification / command queue。
- snapshot lock 和 desired state。
- session generation、last_error、轻量配置。

### 2. 前台 session 资源

随页面或交互 session 创建销毁：

| Owner | 典型资源 |
| --- | --- |
| Hermes | WebSocket/TLS、前台 conversation 状态、录音控制对象、前台 buffer、audio session |
| official_chat | official_chat instance、WS/MQTT transport、audio input/output、codec runtime、前台消息 session |
| BLE provisioning | controller、NimBLE host、GATT provisioning service、advertising、connection state |

### 3. 单次操作资源

操作完成立即释放，不等页面退出：

- recorder task 和一次录音 buffer。
- Ogg/Opus muxer 状态。
- HTTP client、上传 buffer、临时 JSON。
- 一次推理窗口和临时后处理对象。

### 4. 可抢占后台资源

- Safety Monitor / 音频 ESP-DL runtime。
- 后续是否让 Fall 模型推理让路，必须由组合实测决定；IMU 基础采样默认不停止。

### 5. 可回收缓存

- Hermes 最近对话、Inbox、表盘帧等 PSRAM 缓存。
- 缓存不等于前台 session；页面退出后可按产品需求保留，内存压力出现时允许丢弃或缩减。

### 6. 全系统资源覆盖边界

所有动态资源都必须有明确 owner 和释放路径，但不统一接入 foreground gate，也不机械套用同一组 `create/stop/destroy` 接口：

| 资源类别 | 当前代表 | 生命周期合同 | 是否进入 foreground gate |
| --- | --- | --- | --- |
| 强前台 session | Hermes、official_chat、BLE provisioning | 页面或交互进入时创建，离开时停止、等待并销毁 | 是 |
| 可抢占后台 runtime | Safety Monitor / ESP-DL、Fall 模型 | 强前台进入时 quiesce 并确认重资源已释放，结束后按策略恢复 | 不占 gate，由 gate 状态驱动让路 |
| 共享硬件 session | audio codec、麦克风、扬声器 | 由真实 domain owner 按 input/output session 申请和释放 | 不直接进入 gate |
| 基础常驻控制面 | UI/LVGL、Wi-Fi STA、IMU 基础采样、时间、电源及 service 控制 task | 开机初始化并常驻，不随页面退出销毁 | 否 |
| 单次操作资源 | HTTP client、录音 task/buffer、Opus muxer、临时 JSON、推理窗口 | 操作内部创建，成功、失败或取消后立即清理 | 否 |
| 可回收缓存 | 对话记录、Inbox、表盘帧及其他 PSRAM 缓存 | 独立提供 trim/clear 边界，不与 session stop 混为一体 | 否 |
| 维护模式资源 | OTA | 独立 maintenance 生命周期，执行时排斥强前台 | 见 `2026-07-30-standalone-https-ota-maintenance-plan.md` |

阶段 0 必须让每个相关 owner 回答：谁创建、谁停止、如何确认 worker/callback 已退出、谁释放、部分创建失败如何清理、重复停止是否安全，以及页面退出后哪些资源继续保留。

## 五、目标状态机

```text
STOPPED
  -> WAITING_OWNER
  -> WAITING_BACKGROUND_QUIESCED
  -> STARTING
  -> READY
  -> STOPPING
  -> STOPPED

任意创建失败：STARTING -> STOPPING -> ERROR
等待旧 owner 超时：WAITING_OWNER -> ERROR
后台释放超时：WAITING_BACKGROUND_QUIESCED -> ERROR
```

建议状态：

```c
typedef enum {
    FOREGROUND_SESSION_STOPPED = 0,
    FOREGROUND_SESSION_WAITING_OWNER,
    FOREGROUND_SESSION_WAITING_BACKGROUND_QUIESCED,
    FOREGROUND_SESSION_STARTING,
    FOREGROUND_SESSION_READY,
    FOREGROUND_SESSION_STOPPING,
    FOREGROUND_SESSION_ERROR,
} foreground_session_state_t;
```

状态类型可以先在各 service 内部定义；只有两个以上 owner 确认需要同一公开协议后，才提取公共 header。

## 六、外部 API 合同

外部 API 只表达意图：

```c
esp_err_t memory_watch_service_request_foreground(bool active);
memory_watch_service_snapshot_t memory_watch_service_get_snapshot(void);
```

`ESP_OK` 只表示意图已接受，不表示资源已经 READY。UI 必须读取 snapshot 决定是否启用交互。

snapshot 最少包含：

```c
bool desired_foreground;
foreground_session_state_t session_state;
bool resource_ready;
uint32_t session_generation;
esp_err_t last_error;
```

进入/退出只关心最新期望状态，优先使用：

```text
desired state + task notification
```

task notification 只唤醒 owner task；owner task 读取最新 desired state 并 reconcile。无需在 queue 中保存多次重复 enter/leave，避免 queue 满导致 release 意图丢失。

录音、发送、取消等必须逐条处理的命令继续使用 queue。

## 七、Service 内部合同

每个 owner 内部至少形成以下私有边界：

```c
static esp_err_t foreground_runtime_create(void);
static esp_err_t foreground_runtime_stop(uint32_t timeout_ms);
static void foreground_runtime_destroy(void);
static void foreground_runtime_reconcile(void);
```

### 创建规则

1. service task 独占写 runtime handle。
2. acquire foreground gate 成功后才允许创建重资源。
3. 等后台 quiesced 成功后才开始创建。
4. 先分配 queue/buffer/context，最后创建可能立即运行的 task 或 transport。
5. 任一步失败都进入同一个 cleanup 路径。
6. 创建全部成功后才发布 `READY/resource_ready=true`。

### 销毁规则

1. 先设置 `STOPPING`，禁止新操作。
2. 注销 callback，阻止新回调进入。
3. 请求 worker/transport 停止并等待 STOPPED ACK。
4. 关闭 socket/WS/BLE/audio runtime。
5. 释放 audio session、model、buffer、queue 和 callback context。
6. handle 清空后发布 `STOPPED`。
7. 最后 release foreground gate，并通知后台 manager 重新评估。

### 幂等与迟到回调

- `destroy()` 必须能安全处理完整运行、部分创建和重复调用。
- 每次 session 创建递增 `generation`。
- 异步 callback 带创建时 generation；generation 不匹配时只丢弃，不修改新 session snapshot，也不访问旧 context。

## 八、自动前台切换

已确认流程：

```text
用户进入目标页面
  -> 目标页面显示 WAITING_OWNER
  -> 旧 owner desired_foreground=false
  -> 旧 owner stop + destroy + release
  -> 新 owner 在自己的 service task 中重试 try_acquire
  -> 5 秒内成功则继续
  -> 5 秒超时则 ERROR，等待用户重试
```

不新增中央切换 manager。第一版由明确的页面导航路径先通知旧 service leave，再通知目标 service enter；目标 service 自己负责非阻塞等待和超时。

等待期间用户退出目标页面时，`desired_foreground=false` 必须立即取消重试。

## 九、后台 Quiesced ACK

目标流程：

```text
foreground acquire
  -> notify background_service_manager
  -> manager 合成 Safety Monitor should_run=false
  -> safety_monitor_session_apply(false)
  -> danger_detection_service_stop()
  -> task/audio/model/callback cleanup 完成
  -> manager 发布 quiesced ACK
  -> foreground create
```

ACK 不能只依赖一个可能残留的 event bit。完成条件至少包括：

- `danger_blocked_by_foreground_runtime=true`。
- `danger_runtime_running=false`。
- `last_error=ESP_OK`。

建议等待上限 2500ms，因为当前 ESP-DL stop 默认约 2000ms。该值仍是待确认项；失败时前台不启动，release gate 并发布 quiesce timeout。

## 十、分阶段执行

### 阶段 0 审计结果（2026-07-14）

#### 1. Memory Watch / Hermes

| 分类 | 当前资源与 owner | 当前释放事实 | 审计结论 |
| --- | --- | --- | --- |
| 长期控制面 | `memory_watch`、`mw_upload`、`mw_cancel`、`mw_health`、`mw_conv`、Inbox worker；静态 queue/event group/lock；endpoint snapshot | 当前仅初始化，不随页面退出销毁 | 可以继续常驻，但初始化中途失败没有统一回滚；已创建 task 和 PSRAM staging 可能残留，下一次 init 会因“部分 handle 存在”返回错误 |
| 前台意图 | `s_foreground_active`、`s_foreground_runtime_gate_held` | `set_foreground()` 在调用线程立即改状态和申请 gate，随后又投递 queue | owner 边界未闭合；gate 失败只记录日志，后续录音/WS 仍可能继续，属于 fail-open |
| 单次语音操作 | recorder PCM/Opus buffer、Ogg muxer、聚合 audio buffer、WS client、临时响应 | recorder buffer 和 audio buffer 有局部 cleanup；WS 在当前 turn 的结束/错误路径 close | 当前 WS/TLS 更接近“单次交互资源”，不能在没有新证据时直接改成长驻连接；阶段 1 应先统一 create/stop/cleanup 合同 |
| 离页后台能力 | pending conversation `/sync`、Inbox worker/store、最近对话缓存、endpoint 配置 | 页面退出后继续保留 | 这些不属于前台 session，不能随 gate release 销毁 |
| 可回收缓存 | conversation staging、Inbox store/staging 位于 PSRAM | 当前没有统一 trim API | 暂不阻塞阶段 1；后续只在实测内存压力成立时增加 owner 内部 trim |

必须在阶段 1 修复：foreground desired state 只由 service task 推进；gate acquire 失败不得启动录音/WS；初始化与 session 部分创建失败统一回滚；pending/Inbox/最近对话继续保留。

#### 2. official_chat

| 分类 | 当前资源与 owner | 当前释放事实 | 审计结论 |
| --- | --- | --- | --- |
| 长期控制面 | `official_chat_service` task、静态 command queue、snapshot lock、文本 mutex | 初始化后常驻 | 边界成立，不随页面销毁 |
| 前台 session | `s_chat_handle`；其内部 `Application`、transport、audio worker、codec input/output session | service task 执行 prepare shutdown、等待 idle、transport quiet period、解绑 callback、`official_chat_destroy()` | 已有较完整 owner 内部销毁基础，可作为第一个成熟参考 |
| 前台 gate | `s_foreground_runtime_gate_held` | enter 时申请；begin shutdown 时先 release，然后底层才进入 prepare/quiet/destroy | 两个缺口：acquire 失败仍保留 foreground requested 并可能继续创建；release 早于重资源真实销毁，存在切换重叠窗口 |
| 消息缓存 | 固定 8 条历史和最近 user/assistant 文本 | session destroy 时清空 | 当前容量受控；是否跨 session 保留属于产品决定，不应混入资源安全修复 |
| 迟到 callback | stop pending 后事件回调直接丢弃；destroy 前解绑 callback | 已有保护 | 仍需用 session generation 或等价事实覆盖“旧 callback 迟到到新 session”的边界 |

必须在阶段 2 修复：gate acquire 失败 fail closed；只有 `official_chat_destroy()` 和 audio/transport teardown 完成后才 release gate；所有启动失败和 command 投递失败都收敛到一致 snapshot。

#### 3. BLE presence 与 BLE provisioning

当前代码存在两套不同产品语义，必须分别管理，不能继续统称为一个 BLE 开关：

| 资源 | 当前 owner | 当前创建/销毁 | 审计结论 |
| --- | --- | --- | --- |
| 普通 BLE presence | `ble_presence`，由主界面“蓝牙总开关”通过 `network_manager_set_ble_enabled()` 控制 | `nimble_port_init()` + host task + advertising；stop advertising、`nimble_port_stop()`、等待 task、`nimble_port_deinit()` | 当前 UI 在 LVGL 回调同步启动并做 800ms 重试；gate 只包住 start 调用，host/advertising 常驻后立即 release，生命周期不一致 |
| BLE provisioning | `network_provisioning_adapter`，由 Wi-Fi 配网页面显式启动 | `wifi_prov_mgr_init/start`；失败时 deinit；stop 后 `wifi_prov_mgr_deinit()` | adapter 自身 create/stop/deinit 边界较完整，但尚未成为页面级异步 foreground session，也没有持有完整活跃期 gate |
| BLE 偏好与状态 | `ble_control` + `network_manager` | 静态状态/mutex/monitor task 常驻 | 只作为控制面；不应随 BLE session 销毁 |

额外风险：`ble_presence_stop()` 等待 host task 超时时当前只记录警告，随后仍调用 `nimble_port_deinit()` 并清空 runtime；需要在阶段 3 明确超时后的 fail-closed 行为，不能把“未确认退出”发布成已销毁。

阶段 3 开始前先确认产品边界：如果 BLE 只用于小程序配网，应删除“普通 presence 长期开关”这一运行模式，收敛为页面级 provisioning session；在确认前不直接删除现有 presence 路径。

#### 4. 可抢占后台与共享资源

| 资源 | 当前 owner | 当前合同 | 后续要求 |
| --- | --- | --- | --- |
| Safety Monitor / ESP-DL | `background_service_manager -> safety_monitor_session -> danger_detection_service` | `danger_detection_service_stop()` 同步等待 runtime stop，失败时不伪装为已停止 | 增加可等待的 quiesced generation/ACK；前台创建前确认 runtime、model、audio 和 callback 均已释放 |
| Fall 模型 | `fall_detection_service` | `destroy()` 断开 IMU queue 并通知 task；task 活跃时函数会在真实资源释放前返回 | 默认不加入 foreground handover；若组合实测要求让路，必须等待 snapshot 从 STOPPING 到已销毁，IMU 基础采样保持运行 |
| Audio codec | `components/audio_codec` | input/output session 分别记录 active 与 owner，调用方负责成对 acquire/release | 现有 domain owner 继续保留；foreground gate 不重复接管 codec，只验证 session 在 teardown 后已释放 |
| HTTP/SNTP/天气/Inbox | 各网络 owner | 单次 client/请求，按自身周期和错误语义清理 | 不恢复全局 HTTPS gate；只在可重复冲突证据出现时做具体 owner 错峰 |
| OTA | 尚未形成完整 maintenance owner | gate 枚举已有预留，但无完整生命周期合同 | 独立计划建立 maintenance session，不复用 official_chat OTA |

#### 5. 阶段 0 测试基线与后续断言

现有 source tests 可继续锁定目录 owner、foreground gate 调用、Safety/Fall/audio session 基础合同。后续阶段必须新增的行为断言：

- Hermes gate acquire 失败后不能进入录音或 WS create。
- Memory Watch 部分初始化/部分 session 创建失败必须回到可重试状态。
- official_chat 在底层 destroy 完成前不能 release gate。
- BLE start/stop 不在 LVGL callback 内阻塞，且 host 未确认退出时不能发布 STOPPED。
- Safety quiesced ACK 必须对应当前 generation，旧 event bit 不能放行新 session。
- Fall 若未来参与让路，必须等待异步 destroy 完成，而不是只检查 `destroy()` 返回值。

### 阶段 0：资源清单与基线锁定

- 先建立全系统资源生命周期清单，覆盖强前台 session、可抢占后台 runtime、共享硬件 session、基础常驻控制面、单次操作资源、可回收缓存和维护模式资源。
- 为 Hermes、official_chat、BLE 列出实际 handle、task、queue、buffer、audio/model/transport owner，作为本计划首批实施对象。
- 同步审计 Safety Monitor / ESP-DL、Fall、audio codec、网络临时资源、Hermes/Inbox 缓存和 OTA 的现有 owner 与释放合同；审计不等于把它们全部接入 gate。
- 记录当前 create/stop/destroy 路径和遗漏的错误出口。
- 增加或调整 source tests，只锁 owner、状态与清理合同，不复述实现细节。

验收：全系统覆盖矩阵完整；Hermes、official_chat、BLE 三份详细清单能回答“谁创建、谁销毁、何时确认 STOPPED”；其他资源已明确采用常驻、session、quiesce、单次释放或缓存回收中的哪一种合同。

### 阶段 1：Memory Watch owner 内部生命周期

- 将 foreground desired state 的真实推进收回 service task。
- 建立 Hermes foreground state、generation、resource_ready 和单一 cleanup。
- 保留 pending `/sync`、Inbox 和最近对话作为离页后台能力，不误销毁。
- acquire 失败必须 fail closed，不启动 WS/录音。

验收：Hermes create 失败、快速进出页面、重复 enter/leave 均回到一致状态。

### 阶段 2：official_chat owner 内部生命周期

- 将 instance、transport、audio 和 shutdown 收敛到 session state。
- acquire 失败禁止继续启动 instance。
- leave、初始化失败、网络错误和 shutdown 统一走 cleanup/release。
- 保留已有 MQTT/WS transport quiet period，避免重现 lwIP mutex 停机问题。

验收：official_chat 任意失败路径不会留下 instance、audio session 或 gate。

### 阶段 3：BLE provisioning 异步生命周期

- UI 只提交 BLE desired state，不执行同步 enable 和 `vTaskDelay()`。
- network owner task 创建、停止和销毁 BLE controller/host/GATT/advertising。
- gate 持有时间覆盖 BLE 真实活跃期，而不是只覆盖 init 调用。
- 配网完成、用户关闭或页面退出后释放 BLE 资源和 gate。

验收：LVGL 点击不阻塞；BLE active 与 gate snapshot 一致；停止后连续 internal block 可恢复。

### 阶段 4：精简 foreground gate

- 删除无生产调用的 quiet-window API。
- 删除或改造无语义的 `timeout_ms`，明确 `try_acquire` fail-fast。
- 移除宽泛 `FUTURE_PAGE`；未来新增真实 owner 时再增加枚举。
- 评估未接入的 OTA 枚举是否保留；OTA 更适合独立 maintenance 生命周期。

验收：gate 只表达真实强前台 owner 和 acquire/release/current snapshot。

### 阶段 5：自动切换与 5 秒超时

- 旧 owner 有序 stop/destroy/release。
- 新 owner 非阻塞等待 gate，检查最新 desired state。
- 5 秒后 fail closed，不强制释放旧 owner。
- UI 根据 snapshot 显示 waiting/ready/error。

验收：Hermes <-> official_chat 双向切换无资源重叠和 UI 长阻塞。

### 阶段 6：Safety Monitor quiesced ACK

- background manager 发布当前 foreground 变更已经完成应用的事实。
- 前台 owner 等待 ESP-DL/audio/model 真正停止后再创建。
- stop timeout 或 cleanup 失败时前台 fail closed。

验收：日志顺序必须是 ESP-DL stopped/model destroyed/audio released 在前，前台 WS/TLS/audio create 在后。

### 阶段 7：组合真机回归

- Hermes -> official_chat -> Hermes 快速切换。
- ESP-DL running -> Hermes/official_chat。
- BLE provisioning -> Hermes。
- 页面进入后立即退出、STARTING 中退出、网络错误时退出。
- 观察 internal free、largest block、PSRAM、task high-water、audio owner 和 gate owner。

验收：无 panic/Guru/WDT/stack overflow/`esp-aes NO_MEM`；无 gate 泄漏、迟到 callback 和半创建 runtime。

## 十一、非目标

- 不恢复后台 HTTPS 统一 gate。
- 不建立所有页面共用的资源 manager。
- 不把所有动态资源都纳入 foreground gate。
- 不为了接口形式统一而让基础常驻 service 随页面销毁。
- 不让 gate 直接释放其他 owner 的资源。
- 不让 UI 等待 task、关闭 socket、销毁 BLE 或执行延迟重试。
- 不因为未来可能新增页面而预先建立通用 plugin/lease 框架。
- 不在本计划修改 Hermes server 协议、Inbox/sync 语义或 official_chat 业务协议。

## 十二、验证命令

每个涉及 FreeRTOS/RAM/PSRAM 的代码阶段必须按仓库规则新增 run、更新 CHANGELOG/本计划并执行 standard context 校验。

基础验证：

```powershell
uv run python scripts/context/check_layering.py --verbose
uv run python -m pytest tests -q
git diff --check
. D:\esp-idf\v5.5.3\esp-idf\export.ps1
idf.py build
```

如修改 `sdkconfig`，先 `idf.py fullclean`。真机使用 `scripts/board/agent_serial_monitor.ps1`，默认 `app-flash` 后限时 monitor。

## 进度

### 本轮闭环状态（2026-07-29）

- `[x]` 阶段 3 代码实现：BLE/SoftAP provisioning 已由 network owner task 异步管理，包含创建、停止、销毁、generation、gate ownership 与 UI snapshot；真实 Wi-Fi 页面交互仍未验证。
- `[x]` 阶段 4：foreground gate 已收敛为 fail-fast `try_acquire(owner)`。
- `[x]` 阶段 5：Hermes/official_chat owner task 内最多等待 5 秒，支持离页取消并 fail-closed。
- `[x]` 阶段 6：background quiesce generation/ACK 已完成，前台 owner 在确认后才创建重资源。
- `[x]` 阶段 7：代码与观测入口完成，COM7 app-flash-monitor 冷启动回归已复跑通过；真实 Wi-Fi 页面 BLE/SoftAP 交互仍需单独人工覆盖。

以上状态以本节为准；下方历史阶段流水中的旧未勾选项仅保留为历史记录。

- `[x]` 初步计划落地。
- `[x]` 阶段 0：资源清单与基线锁定。已完成全系统覆盖矩阵、三类强前台详细审计以及后续 source-test 断言清单。
- `[x]` 阶段 1：Memory Watch owner 内部生命周期。desired state 已收回 owner task，foreground snapshot/generation、gate acquire fail-closed、离页 operation stop ACK 和初始化部分失败统一 cleanup 已落地。
- `[x]` 阶段 2：official_chat owner 内部生命周期。gate acquire 已 fail closed，start 失败进入统一 shutdown，gate/audio blocker 延后到 callback/transport/audio/instance destroy 完成后释放。
- `[x]` 2026-07-29：修复 BLE transition worker 极早完成时通知空 `s_network_task_handle` 的冷启动 panic；network owner task 入口发布真实 handle，finish 路径通知前判空。
- `[x]` 2026-07-29：阶段 3 局部推进。Wi-Fi 管理页 BLE/SoftAP provisioning 入口已从 LVGL 回调同步启动迁到 `network_service` 异步 owner worker；UI 只提交 `network_service_request_ble()` / `network_service_request_portal()`，source tests 锁住不再直连 `network_manager_start_*_provisioning()`。
- `[x]` 2026-07-29：阶段 3 局部推进。`ble_presence_stop()` 在 host task 未确认退出时返回 `ESP_ERR_TIMEOUT`，不继续 `nimble_port_deinit()` 或清空 runtime，避免把未停止的 BLE owner 伪装成已销毁。
- `[x]` 2026-07-29：阶段 3 局部推进。新增 provisioning 异步 stop 路径：`network_manager_stop_provisioning()` 只收口 active provisioning transport，`network_service_request_stop_provisioning()` 由 worker 执行 stop，Wi-Fi 管理页返回/删除只提交 stop 请求。
- `[x]` 2026-07-29：阶段 3 局部推进。BLE provisioning gate 改为独立 `s_ble_provisioning_gate_held` 标记，自动结束后的周期对账不再依赖最后一次 operation 类型；SoftAP start 前会先释放已结束的 BLE provisioning gate，避免 operation 覆盖导致 gate 泄漏。
- `[x]` 2026-07-29：阶段 3 局部推进。Wi-Fi 已连接且 BLE provisioning transport 仍 active 时，`network_service` 自动投递 stop provisioning；真实 stop/deinit 仍走 internal-stack worker，避免 PSRAM 栈 monitor task 直接收尾 NimBLE/provisioning manager。
- `[x]` 2026-07-29：阶段 3 局部推进。`network_service_snapshot_t` 增加 provisioning pending/error/generation，Wi-Fi 管理页 300ms 刷新可显示“启动中/配网失败”，不再把请求入队成功误当作 transport 启动成功。
- `[x]` 2026-07-29：阶段 3 局部推进。修复旧 provisioning worker 结果覆盖新请求 snapshot 的竞态；finish 路径只有当前 generation 才写 UI pending/error，保持“最新请求 wins”。
- `[x]` 2026-07-29：阶段 3 局部推进。Wi-Fi 管理页不再直接调用有副作用的 `network_manager_get_status()`；`network_service_task` 轮询 manager 后发布纯 `network_service_wifi_status_t` 快照，UI timer 只复制快照并渲染。
- `[x]` 2026-07-29：阶段 3 支撑项。Memory Watch / notification center 的 command queue backing storage、conversation cache、Hermes UI conversation/inbox/detail 缓存和通知中心 scratch 已迁到 PSRAM，释放 internal 静态占用，给 BLE provisioning/TLS 留出更大的 internal heap 连续块。
- `[ ]` 阶段 3：BLE provisioning 异步生命周期。代码缺口已修复，仍待 COM7 真实 Wi-Fi 页面 BLE/SoftAP provisioning 进入、退出、配网完成和普通 BLE 恢复验证。
- `[x]` 阶段 4：精简 foreground gate。`acquire(owner, timeout_ms)` 已收敛为 `try_acquire(owner)` fail-fast；quiet window API 与泛化 `FUTURE_PAGE` owner 已删除。
- `[ ]` 阶段 5：自动切换与 5 秒超时。
- `[ ]` 阶段 6：Safety Monitor quiesced ACK。
- `[ ]` 阶段 7：组合真机回归。

### 阶段 0 验证

- 生命周期相关 source tests：`149 passed / 2 failed`。
- 两项失败均为本阶段开始前已存在的基线问题：official_chat model partition 偏移断言、Fall 默认阈值 `0.30f` 与当前代码 `0.60f` 的合同差异。
- 阶段 0 只更新资源审计文档，没有修改固件运行行为，因此不要求本阶段单独执行 build 或 COM3 真机验证。

### 阶段 1 当前验证

- Memory Watch / foreground 相关 source tests：`44 passed`。
- 全量 source tests：`422 passed / 7 failed`；7 项均为阶段开始前已有漂移。
- ESP-IDF build：通过；`111.bin=0xac5f30`，最小 app 分区余量 `0x33a0d0`（23%）。
- 未执行 app-flash/COM3；组合真机行为仍由阶段 7 验收。

### 阶段 2 当前验证

- official_chat / foreground 相关 source tests：`28 passed`。
- ESP-IDF build：通过；`111.bin=0xac6040`，最小 app 分区余量 `0x339fc0`（23%）。
- 未执行 app-flash/COM3；连接中离页、start 失败与 gate 冲突的真机顺序留待阶段 7。

### 阶段 3 局部验证

- 2026-07-29 BLE transition 空句柄 panic 修复：网络/BLE 相关 source tests 26 passed；`git diff --check` 无 whitespace error；`idf.py build` 通过，`111.bin=0xabd7a0`，app free `0x342860`/23%；COM7 `app-flash-monitor` 30 秒 `panic_log_seen=false`，日志显示 `BLE transition complete: enabled=0 generation=1 result=ESP_OK gate_held=0` 后继续到 IMU/Fall disabled by default。
- 2026-07-29 BLE provisioning start/stop 异步化与 presence stop fail-closed：复查 subagent 确认 P1 缺口为 Wi-Fi 页面 provisioning 入口同步和 gate 未覆盖 active 全期，P2 缺口为 `ble_presence_stop()` 超时后仍继续 deinit。本轮完成入口迁移、页面退出异步 stop 路径，并让 presence stop 超时返回 `ESP_ERR_TIMEOUT`。聚焦 BLE/network source tests 26 passed；layering warning 0；全量 tests 427 passed / 5 个既有基线失败；`git diff --check` 无 whitespace error；`idf.py build` 通过，`111.bin=0xabdb50`，app free `0x3424b0`/23%；COM7 三次 `app-flash-monitor` 30 秒均 `panic_log_seen=false`，日志显示 BLE transition 完成后 Wi-Fi 到 `SERVICE_READY`。
- 2026-07-29 BLE provisioning gate 来源标记：复查 subagent 发现 P1 竞态，BLE provisioning 自动结束但 gate 未被 poll 释放时，若用户切 SoftAP 会覆盖 operation 并可能泄漏旧 gate。本轮新增 `s_ble_provisioning_gate_held`，自动对账和 SoftAP 切换前释放均基于 gate 来源标记。聚焦 BLE/network/UI source tests 28 passed；layering warning 0；全量 tests 429 passed / 5 个既有基线失败；`git diff --check` 无 whitespace error；`idf.py build` 通过，`111.bin=0xabdc20`，app free `0x3423e0`/23%；COM7 `ble-provisioning-gate-owner-flag` 30 秒 `panic_log_seen=false`，Wi-Fi 到 `SERVICE_READY`。
- 2026-07-29 BLE provisioning 配网完成自动收口：当 `network_service` 观察到 Wi-Fi connected、BLE active 且 BLE provisioning gate 仍由本轮持有时，自动投递 `STOP_PROVISIONING`，由 internal-stack worker 调 `network_manager_stop_provisioning()` 完成 stop/deinit。聚焦 BLE/network/UI source tests 29 passed；全量 tests 430 passed / 5 个既有基线失败；`git diff --check` 无 whitespace error；`idf.py build` 通过，`111.bin=0xabdd10`，app free `0x3422f0`/23%；COM7 `ble-provisioning-auto-stop-on-wifi-connected` 30 秒 `panic_log_seen=false`，Wi-Fi 到 `SERVICE_READY`。本次未触发真实 provisioning 完成事件，真实路径仍需 Wi-Fi 页面验证。
- 2026-07-29 BLE provisioning UI 可见结果：`network_service_snapshot_t` 发布 provisioning pending/error/generation，Wi-Fi 管理页额外读取该 snapshot 并显示启动中/配网失败，同时锁住重复点击。聚焦 BLE/network/UI source tests 30 passed；全量 tests 431 passed / 5 个既有基线失败；`git diff --check` 无 whitespace error；`idf.py build` 通过，`111.bin=0xabde30`，app free `0x3421d0`/23%；COM7 `ble-provisioning-snapshot-ui-status` 30 秒 `panic_log_seen=false`，Wi-Fi 到 `SERVICE_READY`。本次未人工打开 Wi-Fi 管理页，真实 UI 文案仍待页面验证。
- 2026-07-29 BLE provisioning snapshot generation guard：复查 subagent 发现旧 worker 完成时会无条件清新请求 pending；本轮改为只有 `s_ble_provision_request_generation == generation` 时才写 UI snapshot，旧 worker 仍更新 applied generation 和完成通知。聚焦 BLE/network/UI source tests 30 passed；全量 tests 431 passed / 5 个既有基线失败；`idf.py build` 通过，`111.bin=0xabde30`。
- 2026-07-29 Wi-Fi 管理页纯快照读取：`network_service_task` 发布 `network_service_wifi_status_t`，`network_service_get_wifi_status()` 和 `network_service_is_wifi_connected()` 只复制服务层缓存，不再在 UI getter 路径触发 `network_manager_get_status()`。复查 subagent 发现旧 provisioning error 可能压过后续已连接文案，已让错误分支额外要求 `!status.wifi_connected`。聚焦 BLE/network/UI source tests 34 passed；layering warning 0；全量 tests 431 passed / 5 个既有基线失败；`git diff --check` 无 whitespace error，仅 line ending warning；`idf.py build` 通过，`111.bin=0xabdf90`，app free `0x342070`/23%；COM7 `ble-provisioning-pure-wifi-snapshot` 30 秒 `panic_log_seen=false`，Wi-Fi 到 `SERVICE_READY`。本次仍未人工打开 Wi-Fi 管理页。
- 2026-07-29 Memory Watch / notification center PSRAM 缓存迁移：`memory_watch_service` 的 queue item storage 与 conversation cache 改为运行期 PSRAM 分配；Hermes UI controller 的 conversation/inbox/detail 缓存和通知中心 inbox scratch 同步迁移，避免大块业务缓存继续占用 internal `.bss`。Memory Watch source tests 38 passed；layering warning 0；`git diff --check` 无 whitespace error，仅 line ending warning；`idf.py build` 通过，`111.bin=0xabe230`，app free `0x341dd0`/23%；`idf.py size` 显示 DIRAM used `225147`、`.bss=44888`，相比本轮前约 `252355/.bss=72096` 释放约 27 KiB internal 静态占用；`nm` 仅剩相关缓存指针/小符号。

## 十四、待继续讨论

- 是否确认 Safety Monitor quiesced 等待上限为 2500ms。
- BLE 是否正式收敛为页面级 provisioning session：离页或配网完成即销毁，不再作为长期全局开关。
- 目标页面 waiting/error 的最终 UI 表达和是否提供显式“重试”按钮。
- official_chat 与 Hermes 是否都只保留最近有限轮前台消息缓存，避免 session destroy 后仍占用大块内存。

## 下一步

本轮代码阶段闭环记录（2026-07-29）：阶段 3 的 BLE/SoftAP provisioning 异步 owner、阶段 4 的 fail-fast foreground gate、阶段 5 的 owner task 内 5 秒等待与阶段 6 的 generation-owned quiesced ACK 均已完成。复查后补充了 quiesce finish 的 generation 所有权、严格 ACK 条件，以及 BLE provisioning 自动完成/启动失败后的普通 presence 恢复。聚焦 source tests `57 passed`，全量 source tests `438 passed`；layering `warning_count=0`、`known_exception_count=2`；ESP-IDF build 通过，`111.bin=0xabe680`、app free `0x341980`（约 23%）。COM7 `app-flash-monitor` 冷启动回归已通过：`panic_log_seen=0`、`residual_count=0`，日志到达 `network_service_ready`，观测 `internal_free=76971`、largest=55296`、psram_free=6803516`。

2026-07-29 复跑闭环：context standard 校验错误 0、警告 0；聚焦 foreground/Hermes/official_chat/BLE/board source tests `53 passed`；`git diff --check` 无 whitespace error，仅 CRLF/LF 提示；ESP-IDF build 通过，`111.bin=0xabe680`、app free `0x341980`（23%）；COM7 `app-flash-monitor` 45 秒复跑通过，日志 `board_logs/2026-07-29-19-46-16-foreground-session-lifecycle-final-rerun.log`，summary `board_logs/2026-07-29-19-46-16-foreground-session-lifecycle-final-rerun.summary.json`，`panic_log_seen=0`、`residual_count=0`，启动到 `startup_sequence_done`、`ui_first_frame_ready` 和 `SERVICE_READY`，冷启动快照 `internal_free=61423`、`largest=53248`、`psram_free=6777484`。

## 归档结论

本计划的代码、自动化测试、构建和冷启动观测闭环已完成，现归档为 `archived`。真实 Wi-Fi 页面 BLE/SoftAP 交互、Hermes/official_chat 快速切换和 ESP-DL 让路属于后续独立真机验收；本计划不把这些尚未执行的人工场景宣称为已验证。
