---
id: watch-foreground-session-lifecycle-plan
tags: context, plans, foreground-session, lifecycle, resource-arbitration, freertos, hermes, official-chat, ble, espdl
summary: 为 Hermes、official_chat 和 BLE provisioning 建立 owner 内部完整的资源创建/销毁机制，再通过异步意图 API、强前台 gate 和后台 quiesced ACK 完成可验证的前台切换。
last_reviewed: 2026-07-14
memory_type: task
scope: task
owners: docs/context/plans/active/2026-07-14-watch-foreground-session-lifecycle-plan.md, main/services/memory_watch/memory_watch_service.c, main/services/official_chat_service.c, main/services/network/network_service.c, main/services/runtime_gate/foreground_runtime_gate.c, main/services/safety/background_service_manager.c
triggers: foreground session, resource create destroy, 前台资源, 创建销毁, Hermes切换, official chat切换, BLE provisioning生命周期
evidence_level: design
status: active
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

### 阶段 0：资源清单与基线锁定

- 为 Hermes、official_chat、BLE 列出实际 handle、task、queue、buffer、audio/model/transport owner。
- 标记长期控制面、前台 session、单次操作和缓存。
- 记录当前 create/stop/destroy 路径和遗漏的错误出口。
- 增加或调整 source tests，只锁 owner、状态与清理合同，不复述实现细节。

验收：三份资源清单能回答“谁创建、谁销毁、何时确认 STOPPED”。

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

- `[x]` 初步计划落地。
- `[ ]` 阶段 0：资源清单与基线锁定。
- `[ ]` 阶段 1：Memory Watch owner 内部生命周期。
- `[ ]` 阶段 2：official_chat owner 内部生命周期。
- `[ ]` 阶段 3：BLE provisioning 异步生命周期。
- `[ ]` 阶段 4：精简 foreground gate。
- `[ ]` 阶段 5：自动切换与 5 秒超时。
- `[ ]` 阶段 6：Safety Monitor quiesced ACK。
- `[ ]` 阶段 7：组合真机回归。

## 十四、待继续讨论

- 是否确认 Safety Monitor quiesced 等待上限为 2500ms。
- BLE 是否正式收敛为页面级 provisioning session：离页或配网完成即销毁，不再作为长期全局开关。
- 目标页面 waiting/error 的最终 UI 表达和是否提供显式“重试”按钮。
- official_chat 与 Hermes 是否都只保留最近有限轮前台消息缓存，避免 session destroy 后仍占用大块内存。

## 下一步

先执行阶段 0，只做资源清单与当前路径审计，不改变运行行为。完成清单后，再从 Memory Watch 开始做第一个可验证的 owner 内部 create/destroy 小闭环。
