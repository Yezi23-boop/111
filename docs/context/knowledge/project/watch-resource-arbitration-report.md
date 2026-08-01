---
id: watch-resource-arbitration-report
tags: context, knowledge, project, resource-arbitration, ram, psram, freertos, runtime-gate, espdl, ble, hermes
summary: 当前手表运行时资源仲裁基线：保留强前台互斥和 ESP-DL 主动让路，后台 HTTPS 由各 owner 自行调度，不再经过统一 token gate；资源治理采用定向 PSRAM 迁移、即时失败处理和证据驱动优化。
last_reviewed: 2026-07-14
memory_type: semantic
scope: project
status: active
owners: docs/context/knowledge/project/watch-resource-arbitration-report.md, main/services/runtime/runtime_coordinator.c, main/services/runtime/safety_monitor_policy.c, main/services/power/power_policy.c, components/espdl_inference
triggers: resource arbitration, internal RAM, largest block, psram, foreground runtime gate, espdl, ble, hermes, official chat, websocket, tls
evidence_level: observed
---

# 手表运行时资源仲裁现状、缺点与优化路线

> 本文档是当前资源仲裁的稳定知识入口。2026-06-28 的三阶段栈实测继续作为历史证据；当前运行机制以强前台互斥、ESP-DL 主动让路、各网络 owner 自行调度、定向 PSRAM 迁移和 fail closed 为准。2026-06-29 引入的 `background_https_gate` 已于 2026-07-14 撤除。

## 一、结论先行

当前问题不是“8 MB PSRAM 不够”，而是 ESP32-S3 片内 RAM 的峰值、连续块和不可迁移资源同时紧张：

- LVGL display bounce、SPI/I2S DMA、部分 Wi-Fi/LwIP、TLS crypto 和 BLE controller 必须或倾向使用 internal RAM。
- Hermes/official_chat WebSocket、BLE 初始化和 ESP-DL 推理都可能制造短时 internal RAM 峰值。
- PSRAM 可以承接大 buffer、历史缓存和部分 task stack，但不能替代 DMA-capable internal RAM，也不能在 flash cache 关闭路径中随意访问。

当前资源模型：

1. UI + Wi-Fi 作为基础常驻。
2. Hermes、official_chat、BLE provisioning 等强前台互斥。
3. Safety Monitor / ESP-DL 是可抢占增强任务，遇到强前台主动让路。
4. health、sync、inbox、mark-read、alert、weather 等网络请求由各自 owner 调度、执行和重试，不统一排队或串行。
5. 大 buffer 和确认安全的 task stack 定向迁入 PSRAM。
6. BLE、ESP-DL、TLS 或 task 创建失败时由对应 owner fail closed 或退避，不能带病继续运行。

本项目不需要中央 `ResourceManager`、后台 HTTPS job queue、全局 malloc 替换层或统一 memory pressure 广播。四个强前台 owner 重复实现同一生命周期后，允许新增只拥有协议、generation、deadline 与 ACK 的 `runtime_coordinator`；它不拥有真实资源。

## 二、历史证据与适用范围

### 1. 2026-06-28 三阶段实测

| 场景 | internal free | largest block | 结论 |
| --- | ---: | ---: | --- |
| 扩缩栈前冷启动 | 12,246 B | 9,728 B | BLE、TLS、显示均处高风险区 |
| 扩缩栈后冷启动 | 35,482 B | 17,408 B | 基线明显改善，但连续块仍有限 |
| 扩缩栈后两次 AI 对话 | 25,726 B | 18,432 B | 可运行，但没有足够余量承受任意新峰值 |
| ESP-DL/Fbank 崩溃附近 | 约 6 KB | 约 5 KB | internal 分配失败后触发 NULL 解引用 |

旧实测证明：

- `free size` 不能代替 `largest free block`。
- “麦克风已经释放”不代表 WebSocket/TLS 上传和 crypto 峰值已经结束。
- task stack 调整能释放稳定 internal RAM，但不能独立解决峰值并发。
- BLE controller、display DMA、TLS 和 ESP-DL 的失败属于同一类 internal RAM 压力表现。

这些数据采集后又发生了 Fall/IMU buffer、告警 task stack、official_chat 和目录结构调整，旧数值不得直接作为当前硬门槛。实现新阈值前必须重新采集。

### 2. 已确认的崩溃时序

```text
Hermes/AI 录音结束
  -> microphone owner 释放
  -> Safety Monitor 判断可以恢复 ESP-DL
  -> WebSocket/TLS 上传仍占用 internal RAM
  -> ESP-DL Fbank internal allocation 失败
  -> vendor Fbank 未检查 NULL
  -> StoreProhibited
```

因此资源仲裁必须观察完整强前台生命周期，不能只观察麦克风占用。

## 三、当前运行时资源模型

### 1. 分层与 owner

```text
UI / App 只表达进入页面、点击开关、开始或结束交互
    |
    v
Service / Domain owner 持有真实生命周期、网络重试和状态
    |
    +-- runtime_coordinator：注册、强前台交接、generation、deadline 与 ACK
    +-- safety_monitor_policy：合成 Safety Monitor 是否应运行
    +-- power_policy：发布整机功耗预算，不直接操纵业务 task
    +-- network owners：各自发起 HTTPS/WSS 并处理错误与退避
    |
    v
Driver adapter / ESP-IDF / vendor SDK 执行 owner 决策
```

`runtime_coordinator` 通过 participant 回调请求 owner 让路并等待 ACK；回调只向 owner task 投递命令。协调器不直接 suspend/delete task，也不接管麦克风、网络、BLE、OTA 或 ESP-DL 的释放。

### 2. 资源档位

| 档位 | 当前能力 | 当前策略 |
| --- | --- | --- |
| 基础常驻 | UI、Wi-Fi、时间、必要 service | 默认常驻，不能为了 BLE/Hermes 粗暴整体关闭 |
| 强前台 | Hermes、official_chat、BLE provisioning；OTA/FUTURE_PAGE 已预留 | 同一时刻只允许一个 owner；进入后 ESP-DL 让路 |
| 可抢占增强 | Safety Monitor / ESP-DL | 强前台 active 时不启动或停止恢复；结束后由自身 owner 决定恢复 |
| 后台网络 | health、sync、inbox、mark-read、weather | 各 owner 按自身周期和重试语义执行，不统一串行 |
| P0 告警 | 危险告警、跌倒本地反馈等 | 不经过后台门禁，但必须使用受控内存和音频 owner |

### 3. 当前冲突处理

| 组合 | 当前处理 | 原因 |
| --- | --- | --- |
| ESP-DL + Hermes/official_chat 强前台 | foreground gate 使 ESP-DL 让路 | 已出现 Fbank internal 分配崩溃 |
| BLE provisioning + 其他强前台 | foreground acquire fail closed | BLE 初始化需要较大连续 internal block |
| microphone 多消费者 | audio/background owner 串行化 | 避免 gate 重复接管麦克风 owner |
| 多个后台 HTTPS | 允许并发，由各 owner 独立重试 | 请求低频，统一 token 的收益不足以抵消状态复杂度 |
| 后台 HTTPS + 强前台 TLS | 当前允许重叠 | 必须通过高压回归确认；若反复失败再做局部错峰 |

不能把“音频录制 + Wi-Fi 上传 + SD 写”永久全局互斥。Memory Watch 的录制、封装、上传是分阶段流水线，应通过清晰阶段边界和有界 buffer 控制，而不是扩大成全局资源锁。

## 四、当前已落地能力

| 能力 | 状态 | 当前事实 |
| --- | --- | --- |
| task stack 审计和扩缩栈 | 已完成 | 修复 `mw_health/mw_conv` 低水位，缩减部分 internal task stack |
| Memory Watch 等 task stack 迁 PSRAM | 已完成 | 仅迁移确认不经过 cache-disabled/ISR 关键路径的任务 |
| Fall/IMU 大窗口与告警 task stack 迁 PSRAM | 已完成 | 后续实测显著恢复 internal free/largest block |
| ESP-DL Fbank 分配前预检 | 已完成 | 不足时返回 `ESP_ERR_NO_MEM` 并跳过窗口，避免 NULL 解引用 |
| `runtime_coordinator` | 已完成 | 替代四个 owner 重复 gate/quiesce 流程，保留真实资源 owner |
| Safety Monitor / ESP-DL 主动让路 | 已完成 | `safety_monitor_policy` 作为可抢占 participant，停止后向 coordinator ACK |
| BLE 单次重试与 fail closed | 已完成 | `ESP_ERR_NO_MEM` 后只短等待重试一次，仍失败则关闭并提示 |
| Memory Watch 瞬时错误重试 | 已完成 | health/inbox 保留 pending/due，瞬时错误不误判永久离线 |
| `background_https_gate` | 已撤除 | 不再统一串行 HTTPS，也不再使用 quiet window |
| 自动 foreground/BLE fail-closed 板测 | 已完成 | 默认关闭的 Kconfig 测试入口保留强前台 owner 路径 |
| 无后台 gate 的组合高压闭环 | 未完成 | 需覆盖公网 HTTPS、WSS、BLE 和 ESP-DL 让路 |

## 五、为什么撤除后台 HTTPS gate

`background_https_gate` 原本使用静态 binary semaphore 串行 health、sync、inbox、mark-read、alert 和 weather，并允许前台打开 quiet window。实际复查后决定撤除，原因是：

1. gate 不预留 internal RAM，也不能保证 TLS、BLE 或 task 创建成功。
2. quiet window 只阻止新请求，不会排空已经在途的 HTTPS。
3. 请求 reason 只用于日志，不验证真实 holder，也没有优先级和公平性。
4. gate busy 引入额外瞬时状态，曾导致 health/inbox 被误判失败并需要额外重试修复。
5. health、sync、inbox 和 weather 的周期、幂等、错误处理不同，统一 token 没有替代各 owner 的调度职责。
6. 后续 PSRAM 迁移已经改善旧基线，但尚无证据证明后台 HTTPS 之间必须全局串行。

撤除收益主要是降低代码、状态和测试复杂度，不是释放大量 RAM。原静态 semaphore 本身占用很小。

## 六、当前方案的优点

1. **owner 清楚**：每个网络 owner 决定何时请求、如何退避、何时改变业务状态。
2. **强前台保护保留**：Hermes、official_chat、BLE 与 ESP-DL 的核心互斥不受影响。
3. **状态更少**：不再区分 background busy、quiet denied 和真实 transport failure。
4. **P0 路径不受误阻塞**：danger alert 不需要等待低优先级后台 token。
5. **容易验证**：网络错误直接来自 HTTP/TLS/业务协议，不夹带第二层门禁状态。
6. **符合简单性原则**：没有为低频请求建立中央队列或调度器。

## 七、缺点与风险

### 1. 后台 HTTPS 可以重叠

health、sync、inbox 和 weather 可能同时创建 socket/TLS client，启动阶段和重试集中时仍可能制造 internal RAM 峰值。这是撤除 gate 后最直接的风险。

### 2. 强前台不会自动阻止后台网络

official_chat/Hermes WebSocket 握手期间，后台 health/weather 仍可能开始。当前接受这个简单模型，必须由真机高压回归证明可承受。

### 3. foreground gate 不提供内存预留

foreground acquire 成功不代表 BLE/TLS 有足够连续 internal block。BLE、ESP-DL、HTTP client 和 task 创建仍必须检查自身返回值并 fail closed。

### 4. coordinator deadline 不等于业务资源强制回收

旧 `foreground_runtime_gate_acquire(owner, timeout_ms)` 已删除。当前 5 秒前台和 2.5 秒后台 deadline 只约束协调事务；超时后进入明确失败或 degraded 状态，不得由 coordinator 强杀 task、释放业务资源或伪造 stopped。

### 5. generation 只保护协议事务

`request_generation` 选择最新目标，`transition_generation` 识别当前排空事务；它们不替代各 owner 自己的 worker/session generation。迟到 worker 仍必须在 owner adapter 内收口，不能修改新 session 快照。

### 6. 历史内存压力阈值已经过时

旧方案提出 `NORMAL >35KB / LOW 26-35KB / CRITICAL <26KB`，但没有进入当前代码，后续内存布局也已经变化。不得直接用旧阈值控制所有 owner。

### 7. 统一观测仍不足

目前日志分散在 gate、BLE、ESP-DL、HTTP 和 heap 路径。缺少同一时间点的 foreground owner、internal free、largest block、PSRAM、HTTP in-flight 和 deny/failure 统计。

## 八、优化优先级

### P0：验证拆除后的真实行为

1. 当前固件重采 internal free、largest block、PSRAM 和关键 task high-water。
2. 开机覆盖 health、inbox、weather 的自然并发，不允许假离线、panic 或持续 `NO_MEM`。
3. ESP-DL running 时进入 Hermes/official_chat，确认让路和恢复顺序。
4. 前台 WSS/语音与后台 HTTPS 同时发生，观察 `esp-aes`、TLS、socket 和 task 创建错误。
5. BLE 显式启动应成功或可解释 fail closed，不允许 Guru/WDT。

### P1：统一协议观测，不增加资源 owner

1. 在现有板测日志统一打印 foreground owner、internal free、largest block、PSRAM free。
2. 通过 owner 自己的 snapshot 统计 HTTP in-flight、retry 和失败原因，不把业务状态搬进 coordinator。
3. 记录 coordinator current/target owner、request/transition generation、deadline 和 ACK 结果。

### P2：只有证据出现时才局部错峰

如果回归重复证明特定组合失败，按以下顺序处理：

1. 调整具体 owner 的启动延迟或退避，例如 weather 避开开机 health/inbox。
2. 对具体大 buffer 定向迁 PSRAM。
3. 在 BLE/TLS/ESP-DL acquire 前增加对应资源的即时预检。
4. 只有两个以上 owner 反复需要同一判断，才考虑共享只读 memory pressure snapshot。

不因一次偶发超时恢复全局 HTTPS token。

### 暂不做

- 不恢复 `background_https_gate` 或通用后台 job queue。
- 不新增中央 `ResourceManager`、`session_router`；`runtime_coordinator` 只允许持有协议事实。
- 不全局替换 `malloc/free`。
- 不把所有 task stack 或 LVGL heap 一次性迁 PSRAM。
- 不使用旧的 26KB/35KB 阈值控制所有 owner。
- 不让 UI 直接执行 BLE/ESP-DL/网络停机或在 getter 中推进仲裁状态。

## 九、验证合同

涉及 FreeRTOS、RAM/PSRAM 或 runtime gate 的后续代码改动必须：

1. 新建对应 `docs/context/runs/YYYY-MM-DD-attempt-<feature>.md`，记录错误签名、证伪路径和板端证据。
2. 更新 `docs/context/CHANGELOG.md`。
3. 更新 `2026-06-29-watch-runtime-resource-gate-plan.md` 的 Progress/Validation/Next Step。
4. 运行目标 source tests、`git diff --check`、`idf.py build`。
5. 修改 `sdkconfig` 时先 `idf.py fullclean` 再 build。
6. 真机日志必须观察 foreground owner、internal free、largest block、PSRAM、HTTP/TLS 错误、panic/Guru/stack overflow/NO_MEM。

## 十、关联资料

- `docs/context/plans/completed/2026-06-29-watch-runtime-resource-gate-plan.md`
- `docs/context/plans/completed/2026-07-31-runtime-coordinator-plan.md`
- `docs/context/knowledge/project/runtime-owner-contract.md`
- `docs/context/knowledge/project/task-stack-measurement-full-summary.md`
- `docs/context/runs/2026-06-29-attempt-foreground-runtime-gate.md`
- `docs/context/runs/2026-06-29-attempt-espdl-foreground-runtime-yield.md`
- `docs/context/runs/2026-06-29-attempt-runtime-resource-gate-hermes-https-ble.md`
- `docs/context/runs/2026-06-29-attempt-runtime-resource-gate-board-stress.md`
- `docs/context/runs/2026-07-08-attempt-memory-watch-background-https-retry.md`
- `docs/context/runs/2026-07-14-attempt-remove-background-https-gate.md`

## 一句话结论

当前资源仲裁只保留有明确 owner 和崩溃证据的强前台互斥与 ESP-DL 让路。后台 HTTPS 恢复为各 owner 自治，不再统一串行；如果拆除后出现可重复资源冲突，优先修具体组合，而不是重新引入全局门禁。
