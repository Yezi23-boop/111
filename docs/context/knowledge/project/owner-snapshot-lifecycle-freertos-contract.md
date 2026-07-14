---
id: owner-snapshot-lifecycle-freertos-contract
tags: project, architecture, freertos, owner, snapshot, lifecycle, resource-management
summary: 固定当前仓库 owner snapshot、生命周期和 FreeRTOS 通信方式的专项合同，区分器件驱动、板级语义、长期 service 与 domain/session owner。
last_reviewed: 2026-06-02
memory_type: project_knowledge
scope: repo
owners: docs/context/knowledge/project/owner-snapshot-lifecycle-freertos-contract.md, docs/context/knowledge/project/runtime-owner-contract.md, docs/context/knowledge/project/project-framework.md
triggers: owner snapshot, lifecycle, FreeRTOS, task notification, queue, event group, 资源释放, 生命周期, 通信方式
evidence_level: design
status: active
---

# Owner Snapshot / Lifecycle / FreeRTOS 合同

## 一句话结论

当前仓库 V1 不做中心化 `runtime lease` 或大 `ResourceManager`，而是把系统拆成四类 owner：

```text
器件驱动放 components/xxx
板级语义放 board_power / hardware_init
长期系统能力放 service
资源语义和 session 放 domain owner
```

跨 owner 协作统一走：

```text
owner snapshot
  -> power_policy / service 聚合
  -> budget / command / notify
  -> owner 自己执行生命周期和资源释放
```

## 四类 owner 分工

| 类别 | 典型位置 | 负责 | 不负责 |
| --- | --- | --- | --- |
| 器件驱动 / Driver Adapter | `components/axp2101`、`components/pcf85063atl`、`components/co5300_panel`、`components/touch_ft5x06`、`components/wifi_control`、`components/lvgl_port` | SDK/寄存器/总线时序/错误码翻译/器件初始化细节 | 产品状态、页面文案、跨模块策略、后台生命周期 |
| 板级语义 / Board BSP | `main/app/board_power.c`、`main/app/hardware_init.c`、`main/app/board_button.c` | 把板上器件组合成“本板能力”，处理启动基础设施 | 长期后台循环、产品策略、UI 状态机 |
| 长期系统能力 / Service | `main/services/power/power_service.c`、`power_policy.c`、`network_service.c`、`official_chat_service.c`、`background_service_manager.c`、`sleep_coordinator.c` | FreeRTOS task、后台生命周期、ready gate、预算聚合、状态推进 | 直接写寄存器、直接改 LVGL 对象、替 domain owner 释放资源 |
| 资源语义 / Domain owner | `components/audio_codec`、网络语义门面、`components/system_time`、`main/services/safety/safety_monitor_session.c`、`main/features/danger_detection/danger_detection_service.c`、`app_alert_manager.c` | session、领域状态机、资源占用事实、可读快照 | 页面对象、启动阶段总编排、跨领域总调度 |

判断口诀：

```text
碰寄存器/SDK 时序 -> Driver Adapter
描述这块板怎么接线/上电 -> Board BSP
长期跑任务、等事件、做重试 -> Service
管理“谁正在用资源/业务状态” -> Domain owner
```

## Snapshot 合同

每个长期 owner 或 domain owner 对外暴露只读 snapshot，推荐形态：

```c
esp_err_t xxx_get_snapshot(xxx_snapshot_t *out);
```

也允许返回小型值类型：

```c
xxx_snapshot_t xxx_get_snapshot(void);
```

强制规则：

- snapshot 是状态副本，不暴露内部可写对象、任务句柄、队列句柄或动态内存指针。
- getter 不做 I/O、不访问慢硬件、不等待网络、不推进状态机、不顺手重试。
- getter 不创建 task、不申请资源、不释放资源。
- getter 可用 critical section、mutex 或 owner 内部锁复制状态。
- task 尚未启动时，返回默认安全快照或明确错误，不在 getter 里补启动。

当前已存在的 snapshot / 状态入口：

| Owner | 当前入口 | 现状判断 |
| --- | --- | --- |
| `ui_refresh_policy` | `ui_refresh_policy_get_activity_snapshot(out)` | 符合，只读 UI 活跃事实。 |
| `power_policy` | `power_policy_get_budget()` | 符合，返回预算值副本。 |
| `power_service` | `power_service_get_snapshot(out)` / `power_service_get_state()` | 符合，新增 out-copy snapshot；旧指针接口仅兼容旧调用方。 |
| `background_service_manager` | `background_service_manager_get_snapshot()` | 符合，返回值副本。 |
| `safety_monitor_session` | `safety_monitor_session_get_snapshot()` | 符合，返回值副本。 |
| `audio_codec` | `audio_codec_get_session_snapshot(out)` | 符合，表达 input/output session owner。 |
| `system_time_service` | `system_time_service_get_snapshot(out)` | 符合，桥接 system_time 快照。 |
| `sleep_coordinator` | `sleep_coordinator_get_snapshot()` | 符合，返回 dry-run 状态。 |
| `network_service` | `network_service_get_snapshot(out)` / `network_service_get_wifi_status(out)` | 符合，统一发布 state、Wi-Fi、service-ready、probe、Wi-Fi PS 和 last error/result。 |
| `official_chat_service` | `official_chat_service_get_snapshot(out)` / command queue | 符合，外部意图通过 queue 进入 owner task，snapshot 返回副本。 |

## 生命周期合同

长期 owner 的生命周期默认拆成：

```text
init
  -> start
  -> ready / running
  -> degraded / blocked
  -> stopping
  -> stopped
```

不是每个模块都必须公开完整枚举，但内部日志和 snapshot 应能回答：

- 是否已初始化。
- task 是否已启动。
- 当前是否 ready/running。
- 是否 blocked，原因是什么。
- 最近一次错误码是什么。
- 资源是否 active / inactive / released。

资源结束流程固定为：

```text
外部发出结束意图
  -> owner 收到 command / notify
  -> owner 停止接收新工作
  -> owner 等当前关键动作短收尾
  -> owner 释放 session / 关闭自己域内硬件
  -> owner 更新 snapshot 为 inactive / released
```

禁止：

- `power_policy` 直接关硬件、suspend task 或释放 session。
- UI 页面退出直接 stop 长期后台 runtime。
- 非 owner 模块直接改 owner 内部 flag。
- 中心模块删除“占用记录”并假装真实硬件已经释放。

## FreeRTOS 通信合同

优先使用 FreeRTOS 原语表达并发语义，不用裸 `volatile` 自造协议。

| 场景 | 推荐原语 | 当前项目例子 / 说明 |
| --- | --- | --- |
| 长期执行单元 | task | `power_policy`、`power_service`、`network_service`、`official_chat_service`、`background_service_manager`、`sleep_coordinator`。 |
| 轻量事件唤醒 | task notification | `power_policy_notify(reason)`、`background_service_manager` 的用户开关/前台音频/power budget 变化唤醒。 |
| 带参数命令 | queue | `board_button` 事件队列；后续 `official_chat_service` 前台/退出/shutdown 适合迁移。 |
| 多条件 readiness | event group | `startup_readiness` 的 UI first frame gate。 |
| 独占共享资源 | mutex / semaphore | I2C、音频 session、共享快照复制。 |
| 快照双缓冲 / 小临界区 | critical section | `power_service`、`power_policy` 这类短状态复制。 |
| 超时、退避、低频兜底 | software timer / timeout wait | power poll backoff、网络探测切片、资源释放等待。 |

例外：

- Vendor/SDK 已固定使用自己的线程模型时，不强行改成 FreeRTOS queue。
- C++ 官方组件内部可保留 STL queue / condition_variable，但对项目外层 service 仍要暴露清晰 snapshot 和生命周期 API。
- ISR 到 task 的短事件可以用 queue、notification 或 event group，但 ISR 内不得执行重逻辑。

## 当前优先整理顺序

1. `official_chat_service`
   - 已整理外层 `main/services/official_chat_service.c`。
   - 不强行改 `components/official_chat` C++ 内部线程模型。
   - V1 已补 command queue、snapshot 和页面退出完整停止语义。

2. `network_service`
   - 已补统一 `network_service_get_snapshot(out)`。
   - 云端探测每轮 attempt 前读取 `power_policy` budget；STANDBY/同步暂停时发布 `probe_paused_by_budget` 并退出探测，不主动断开 AP。

3. `power_service`
   - 已补 `power_service_get_snapshot(out)`，getter 只复制双缓冲活动快照，不访问 PMIC/I2C。
   - 有效电源状态变化后 notify `power_policy`，周期采样仍作为兜底；AXP2101 V1 仍保持只读事实源。

4. `background_service_manager`
   - 已保持 Safety Monitor 目标态合成器定位，不升级成通用调度器。
   - 用户开关、前台音频、power budget 变化通过 task notification 唤醒 manager task；1 秒周期等待作为兜底。
   - `power_policy` 只通知预算可能变化，不携带预算、不启动或停止 Safety Monitor。
   - Safety Monitor start/stop 仍由 `safety_monitor_session` 执行。

5. `sleep_coordinator`
   - 已保持只消费 `power_policy_get_budget()` 的 dry-run owner，不逐个询问 UI、网络、音频、PMIC 或 RTC owner。
   - dry-run snapshot 和日志保留 `sleep_permission / sleep_blockers / sleep_interval_hint_ms / budget_version` 可观测字段。
   - 当前不调用 `esp_light_sleep_start()`、`esp_deep_sleep_start()` 或 `esp_sleep_enable_*()`；后续真实 sleep 前再引入 blocker 完整性检查，不提前做 runtime lease。

## 验收标准

文档或代码变更后，至少验证：

```powershell
uv run python scripts/context/validate_context.py --level standard --q "owner snapshot lifecycle FreeRTOS communication runtime owner" --brief
```

改源码时再补对应 source tests，至少检查：

- getter 不做 I/O、不调用 start/stop、不推进状态。
- `power_policy` 不直接操作硬件。
- UI 不直接调用 driver adapter。
- 资源释放由 owner 完成，并能从 snapshot 看到 released/inactive。
- task/queue/notification/event group 的使用有明确 owner 和超时路径。

## 与其他文档关系

- `project-framework.md` 是整体框架总图。
- `runtime-owner-contract.md` 是 owner 边界合同。
- 本文是 FreeRTOS owner snapshot、生命周期和通信方式的专项合同。
- `low-power-framework-architecture.md` 只描述低功耗预算链路和 sleep 边界。
