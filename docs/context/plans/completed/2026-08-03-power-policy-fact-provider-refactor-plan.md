---
id: power-policy-fact-provider-refactor-plan
tags: plan, archived, power, power-policy, provider, owner, sleep-blocker, decoupling
summary: 将 power_policy 的省电事实输入改为静态注册式 provider，保持生命周期 owner 和 runtime_coordinator 边界不变。
status: archived
last_reviewed: 2026-08-03
memory_type: plan
scope: repo
owners: main/services/power/power_policy.c, main/services/power/power_policy.h, main/services/runtime/runtime_coordinator.c
triggers: power_policy, provider, 省电注册制, sleep blocker, 低功耗解耦, power facts
evidence_level: design
---

# Power Policy 省电事实 Provider 注册制改造计划

## Purpose / Big Picture

当前低功耗主线已经确定为：

~~~text
owner snapshot（资源 owner 快照）
  -> power_policy 聚合事实
  -> power_budget
  -> 各 owner 在自己的资源域内执行
  -> sleep_coordinator 只消费预算并 dry-run
~~~

本计划只解决 power_policy 对具体服务的输入耦合，不改变 ACTIVE / STANDBY 产品状态，也不把省电策略升级为生命周期管理器。

目标是让真正影响省电或睡眠的服务可以注册自己的事实提供接口：

~~~text
注册事实和能力，不注册 task/resource 的生命周期控制权
~~~

## Current Boundary

- power_policy 是预算聚合与发布者，不直接操作 UI、Wi-Fi、音频、模型、PMIC 或 ESP sleep API。
- power_service 和 ui_refresh_policy 继续作为核心事实源。
- 真实资源生命周期仍由资源 owner 执行。
- runtime_coordinator 继续负责 OTA、Hermes、official chat、BLE provisioning 等强前台资源交接。
- sleep_coordinator 继续保持 DRY_RUN；本计划不启用 CONFIG_PM_ENABLE，不引入真实 Light Sleep / Deep Sleep。
- STANDBY 只允许非关键工作降低频率或延后，不强制停止所有后台 task。

## Problem To Solve

当前实现的主要耦合不是资源 ownership（资源所有权）错误，而是决策输入过于写死：

1. power_policy 直接依赖 safety_monitor_policy 的具体通知函数。
2. power_budget 同时承载事实、策略结论和多个业务域的权限字段。
3. sleep_blockers 中存在预留字段，但缺少统一的 owner 事实上报合同。
4. 新增一个影响睡眠的服务，容易继续修改 power_policy.c，形成中心化判断分支。
5. 各 owner 对 PAUSE_OPTIONAL 的解释可能不一致，必须明确它是建议，不是停止命令。

## Target Architecture

~~~text
power_service / ui_refresh_policy
        │ 核心事实
        ▼
power_policy provider table
        │ 读取各 provider 快照
        ▼
power_budget snapshot
        ├── network_service       执行 Wi-Fi PS / 同步节流
        ├── safety_monitor_policy 合成目标态
        ├── audio owner           维护音频 session
        ├── system_time_service   判断时间同步是否延后
        └── sleep_coordinator     只做 sleep dry-run

runtime_coordinator 独立处理跨 owner 前台资源交接
~~~

power_policy 只保存静态 provider 配置和聚合后的预算事实，不保存业务 task handle，也不调用 stop/start/deinit。

## Decisions: Phase 2 and Audio

### One participant registry, two optional roles

不另做独立的消费者唤醒表。现有 provider config 改名为语义更准确的 participant config，登记项允许三种形态：

```text
facts-only       只提供省电事实
consumer-only    只接收预算变更并唤醒自己的 task
facts+consumer   同时提供事实并消费预算
```

Safety Monitor 采用第三种形态：

- `get_facts()` 从 Safety 自己的 snapshot 提供是否关键运行、是否阻塞睡眠等事实。
- `on_budget_changed()` 只向 Safety policy task 发送 `SAFETY_MONITOR_POLICY_NOTIFY_POWER`。
- 回调不执行 Safety start/stop，不调用 `power_policy_notify()`，不读取硬件。

这样保留一张静态表，避免 provider registry 和 consumer wake table 两套注册、容量和生命周期语义。注册表的准确定位是“省电参与者登记表”，provider 与 consumer 是登记项的两个独立能力。

### Audio blocker registration host

`audio_codec` 是 `AUDIO_ACTIVE` 事实和 input/output session 的唯一 owner；`components/audio_codec` 不反向依赖 `power_policy`，也不在组件内部注册省电策略。

注册 glue 放在窄的 `main/services/power/power_policy_audio_bridge.c` 中。该 bridge 只做两件事：

1. 读取 audio owner 暴露的非阻塞 session snapshot。
2. 将 input/output active 映射为 `POWER_POLICY_SLEEP_BLOCKER_AUDIO_ACTIVE`。

它不包含播放、录音、告警优先级或 codec 生命周期逻辑。注册调用在 app_main 的 core policy 组装阶段完成，要求 audio codec 已初始化且 power_policy task 尚未启动，保证首次预算能读到初始事实。

当前 `audio_codec_get_session_snapshot()` 会等待资源锁，不能直接作为 policy task 的 provider callback。Phase 0/1 必须先确定并实现一个非阻塞的 cached snapshot（缓存快照）读取合同；在此之前不得接入 AUDIO_ACTIVE provider。

## Provider Contract

第一版只增加窄接口，优先放入现有 power_policy.[ch]，不新增 power_manager 或通用资源注册中心：

~~~c
power_policy_register_provider(config);
~~~

config 至少包含：

~~~text
stable id / name
get_facts(out, context)
on_budget_changed(version, context)  // 可选
context
~~~

事实最小集合：

~~~text
running
must_keep_alive
can_defer_work
sleep_blockers
last_error（可选）
~~~

合同：

- 注册只发生在 service 初始化阶段，静态容量固定为 8 个 provider，不提供运行期卸载。
- 同一 id 重复注册必须幂等；冲突配置返回明确错误。
- get_facts() 只能快速复制 owner snapshot，不能做 I/O、网络、音频、Flash、模型操作或等待其他 task。
- 现有阻塞式 getter 不能直接作为 get_facts()；需要 owner 维护的小快照或明确的 try-read API。
- provider 回调不在 power_policy 锁内执行，避免回调重入和锁反转。
- on_budget_changed() 只能向自己的 owner task 发送 task notification 或 queue 消息；真实动作由 owner task 执行。
- provider 失败不能静默清除 blocker；策略应记录来源和错误，并按 fail-closed 规则处理睡眠许可。
- 普通后台能力不强制注册；只有真实影响功耗、睡眠或关键资源交接的 owner 才接入。

## Semantic Contract

### PAUSE_OPTIONAL

background_budget = PAUSE_OPTIONAL 的含义是：

~~~text
允许非关键工作延后，不要求服务停止
~~~

服务 task 可以继续存在，只暂停周期性网络请求、退避或降低工作频率。

### sleep_blockers

blocker 由真实资源 owner 提供，不由 power_policy 猜测：

- 普通时间同步：can_defer_work = true，不阻塞睡眠。
- 时间同步是 OTA/TLS 的必要前置条件：短时设置 NETWORK_CRITICAL，完成或失败超时后清除。
- 音频、P0 告警、不可中断 OTA 阶段：由对应 owner 上报 blocker。
- Safety Monitor 关键识别期间保持运行；不能暂停时上报 BACKGROUND_CRITICAL 或更窄的 blocker。

sleep_blocker 表示“现在睡眠可能破坏当前功能”，不表示由 power_policy 直接停止或恢复服务。

## Phase 0 盘点结果（字段矩阵与 Provider/Consumer 名单）

### power_budget 字段矩阵：谁写 / 谁读 / 谁执行

写入方只有三类事实来源：`power_service_get_snapshot()`（电源/电池/充电/外电）、
`ui_refresh_policy_get_activity_snapshot()`（活跃度/空闲时长/force_active）、
`s_maintenance_window_active`（唯一外部写入口是 `power_policy_set_maintenance_window`，
由 `ota_service` 调用）。**power_policy 自身不产生业务事实，只聚合后发布。**

| 字段 | 写入来源 | 读取方 | 实际执行动作 |
|---|---|---|---|
| state / standby_reason | ui 活跃度快照推导 | network / safety / weather | STANDBY 时各 owner 在自己资源域降级 |
| display_budget / ui_budget | ui 活跃度快照推导 | weather(display)；ui_refresh_policy 待接入 | 天气降频；LVGL 2000ms 深省电尚未落地 |
| network_budget | 状态推导 + 维护窗口 | network_service | Wi-Fi PS / 暂停非关键同步 / 保连接 |
| background_budget | 状态推导 + 维护窗口 | weather_service | 天气刷新周期降频（PAUSE_OPTIONAL 唯一真实消费者） |
| cpu_budget / power_poll_budget | 状态推导 | **无消费者** | 仅诊断字段，保留不扩展 |
| sleep_permission | 聚合结论（STANDBY + 无 blocker + interval>0） | sleep_coordinator | 仅 dry-run |
| sleep_blockers | ui 快照(UI_FORCE_ACTIVE)、维护窗口(BACKGROUND_CRITICAL)；其余 6 个为预留 | sleep_coordinator / safety | 阻塞 LIGHT_ALLOWED |
| sleep_interval_hint_ms | ui 空闲满 5 分钟 | sleep_coordinator | 仅 dry-run |
| flags | power 快照(充电/外电/低电量/维护) | network / safety / memory_watch | 分支策略，非产品状态 |
| danger_detection_allowed 等许可字段 | 状态推导 + 维护窗口 | safety / network | 许可语义，真实 start/stop 由 owner 执行 |
| haptic_alert_allowed | 状态推导（默认 true） | haptic_alert_player | 是否允许触觉提醒 |
| low_battery_warn / battery_* | power_service 快照 | memory_watch / 日志 | 上报电量给 Hermes，不弹 UI、不改变预算 |
| budget_version / last_notify_reasons | power_policy 内部 | sleep_coordinator | 判断预算是否更新 |

### Provider vs Consumer 名单

第一批 provider 候选：

| 候选 | 登记形态 | 事实内容 | 接入前提 |
|---|---|---|---|
| Safety Monitor | facts+consumer | must_keep_alive / block_reason | 已有 snapshot，可先做 |
| Audio（power_policy_audio_bridge） | facts-only | AUDIO_ACTIVE | **需先实现 audio_codec 非阻塞快照** |
| Network critical | facts-only（短时） | NETWORK_CRITICAL | 有真实关键前置（OTA/TLS 时间前置）才上报 |
| OTA / provisioning | facts-only | OTA_ACTIVE / PROVISIONING_ACTIVE | 读 runtime_coordinator 单一快照，不重复上报 |
| Alert owner（app_alert_manager） | facts-only | ALERT_ACTIVE | 独立于音频，Phase 3 接入 |

纯预算消费者（不注册事实）：weather_service、memory_watch_service、haptic_alert_player、
sleep_coordinator、system_time_service（Phase 4 起开始消费预算）。

核心事实源保持直接 snapshot 读取，不套 provider：power_service、ui_refresh_policy。

### 语义澄清

- `PAUSE_OPTIONAL`：允许非关键工作延后，不要求服务停止。唯一真实消费者 weather_service
  将其解释为“天气刷新周期降频”，task 继续存在、不断网络 owner。
- `must_keep_alive`：provider 事实输入，表示当前处于关键识别/不可中断阶段；
  **不新增预算字段**，只影响 blocker 聚合（如 Safety 关键识别 → BACKGROUND_CRITICAL）。
  用来解释“普通后台可以延后而关键服务不能暂停”。
- `sleep_blockers`：由真实资源 owner 提供，power_policy 只聚合；每个 blocker 必须能追溯到
  真实 owner 和清除路径。当前 UI_FORCE_ACTIVE / BACKGROUND_CRITICAL 有真实来源，
  其余 6 个为预留位图，由 Phase 3 逐项补齐。

### audio_codec 非阻塞快照决策

已确认 `audio_codec_get_session_snapshot()` 内部对 `s_resource_mutex` 使用
`UINT32_MAX` 无限等待（audio_codec.c:170-186、1003-1018），**不能**直接作为 policy task
的 provider 回调：会阻塞 policy task，并引入锁序反转风险。

决策：audio_codec 维护一份由现有 `s_resource_mutex` 保护的 cached session snapshot，
新增非阻塞读取 API（无锁或 portMUX 保护的快照副本）；`power_policy_audio_bridge` 只读缓存。
**在未实现前不得接入 AUDIO_ACTIVE provider。**

### 边界确认

- system_time_service 独立于 network_service：只负责 SNTP 同步、RTC 回写和时间有效性，
  当前不消费预算，Phase 4 再作为可延后消费者接入。
- ACTIVE / STANDBY、5 分钟 LIGHT_ALLOWED、sleep_coordinator dry-run 语义保持现状不变，
  已有 source tests 锁定。

## Implementation Phases

### Phase 0: 合同和字段盘点

- [x] 列出当前 power_budget 每个字段的写入方、读取方和实际执行动作。
- [x] 明确 PAUSE_OPTIONAL、must_keep_alive、sleep_blockers 的 Doxygen 与 source-test 语义。
- [x] 确认哪些服务是 provider，哪些只是预算消费者。
- [x] 明确 system_time_service / 时间同步只负责时间同步业务，不并入 network_service。
- [x] 确认 audio_codec session snapshot 的非阻塞读取方式；现有会等待资源锁的 getter 不能直接进入 policy task。
- [x] 保持现有 ACTIVE / STANDBY、5 分钟 LIGHT_ALLOWED 和 dry-run 语义不变。

验收：字段矩阵和 owner 边界能回答“谁写、谁读、谁执行”；不需要修改代码即可解释普通后台为何可以延后而关键服务不能暂停。

### Phase 1: 静态 Provider Registry

- [x] 在 power_policy.[ch] 增加固定容量 provider 表和注册 API。
- [x] 实现重复注册幂等、容量上限、初始化阶段检查和清晰错误日志。
- [x] 在独立 policy task 中遍历 provider，读取快照并聚合 blocker。
- [x] 将配置结构命名为 participant config，并允许 facts-only、consumer-only、facts+consumer 三种登记项。
- [x] 确保 provider 回调不在 policy mutex（互斥锁）内执行。
- [x] 保留现有核心 power_service / ui_refresh_policy 读取路径，不为一次性事实强行套 provider。

验收：模拟 provider 能改变 sleep_blockers 和预算版本；没有 provider 时现有 ACTIVE/STANDBY 行为不变。

### Phase 2: 通知链解耦

- [x] 将 power_policy -> safety_monitor_policy 的具体通知改为注册式预算变更唤醒。
- [x] Safety policy 仍只读取预算快照和自己的 owner snapshot，真实 start/stop 仍由 Safety session 执行。
- [x] 保留 1 秒周期等待作为一致性兜底，notify 只作为加速触发器。
- [x] 检查并删除因本次迁移产生的直接 include、旧通知函数和无效变量。

验收：power_policy.c 不再依赖 Safety policy 的具体函数；Safety 预算变化、用户开关和 coordinator block 仍能唤醒自己的 task。

### Phase 3: 关键 Provider 迁移

按风险从低到高迁移：

- [x] Safety Monitor：注册关键运行事实和预算变化通知（facts+consumer）。
- [x] Audio：先由 audio_codec 提供非阻塞 session snapshot，再由 power_policy_audio_bridge 注册 AUDIO_ACTIVE；alert owner 单独提供 ALERT_ACTIVE，不把音频业务写入 power 层核心策略。
- [x] Network critical：评估后决定不注册——当前没有真实"OTA/TLS 时间前置等待中"的运行时状态源，网络关键场景由 coordinator/OTA 事实覆盖；普通 time sync 走延后消费，符合"普通能力不强制注册"。后续出现真实前置状态源时按同一合同补注册，不把时间同步业务放入 network_service。
- [x] OTA / provisioning：优先读取 runtime_coordinator 的单一当前 owner/transition 快照，避免同一事实被 coordinator participant 和 power provider 重复上报。
- [x] 普通 time sync：作为可延后预算消费者；只有在真实关键前置条件成立时才短时上报 blocker。

验收：新增或迁移一个 provider 不需要在 power_policy.c 增加业务分支；每个 blocker 都能追溯到真实 owner 和清除路径。

### Phase 4: Budget Consumer 收口

- [x] network_service 只消费 network_budget / network_sync_allowed，自己执行 Wi-Fi PS 和非关键同步节流。
- [x] system_time_service 只消费预算并维护同步周期、重试和时间有效性。
- [x] safety_monitor_policy 只合成目标态，不把 PAUSE_OPTIONAL 解释成无条件停止。
- [x] sleep_coordinator 只消费最终 sleep_permission、sleep_blockers 和 interval hint。
- [x] 检查预算字段是否仍有“发布但没有消费者”的情况；没有实际消费者的字段先保留为诊断或移出，不继续扩展。

验收：服务 task 生命周期未被 power_policy 接管；STANDBY 下普通同步可延后，关键服务保持运行或明确阻塞睡眠。

### Phase 5: 聚焦测试和静态边界检查

必须覆盖：

- [x] provider 注册、重复注册、容量上限和未初始化调用。
- [x] 普通 time sync 在 STANDBY 下延后，不停止 task、不关闭网络 owner。
- [x] 音频/告警 blocker 使 sleep_permission = NONE。
- [x] blocker 清除后预算版本递增，sleep dry-run 恢复。
- [x] provider 回调返回错误时不错误放行睡眠。
- [x] 预算变更通知只唤醒 owner task，不在 policy task 执行业务动作。
- [x] runtime_coordinator 的强前台交接路径不被 power provider 复制或绕过。

验证顺序：

~~~powershell
python -m unittest <新增或修改的 power_policy/provider source tests>
uv run python scripts/context/validate_context.py --level standard --q "power_policy provider sleep blocker owner lifecycle"
git diff --check
cmd /c "call D:\esp-idf\v5.5.3\esp-idf\export.bat >nul && idf.py build"
~~~

### Phase 6: COM7 冷启动与 dry-run 回归

- [x] 默认配置下执行 COM7 app-flash-monitor，确认冷启动到 SERVICE_READY。
- [x] 观察普通 STANDBY：UI 降频、Wi-Fi PS、生存中的关键后台能力无 panic/WDT。
- [x] 观察 LIGHT_ALLOWED：只出现 dry-run 预算，不进入真实 ESP sleep。
- [x] 在模拟 provider 序列中验证 time sync 延后、音频 blocker、OTA/coordinator blocker 和恢复路径。
- [x] 不以冷启动日志替代真实 OTA 不可中断阶段验证；OTA 阶段仍以 owner contract 和已有 coordinator 证据为准。

验收：无 panic、Guru、WDT、NO_MEM；预算日志能显示来源、版本、blocker 和恢复；默认固件没有新增实际停服务行为。

### Phase 7: 文档闭环

- [x] 更新 low-power-framework-architecture.md：补充 provider 事实输入合同和 PAUSE_OPTIONAL 语义。
- [x] 更新 runtime-owner-contract.md：明确 power provider 不拥有生命周期控制权，真实资源仍由 owner 执行。
- [x] 更新 project-framework.md：补充 provider 输入、预算输出和 runtime coordinator 的分工。
- [x] 更新 docs/context/CHANGELOG.md：记录注册制边界、未启用真实 sleep 和验证证据。
- [x] 如出现高成本板端异常或路线取舍，再写入 docs/context/runs/；普通 source test/build 结果不单独建 run。

## Migration Table

| 当前内容 | 改造结果 |
|---|---|
| power_policy 直接通知 Safety policy | 改为通用预算变更通知，回调只唤醒 owner task |
| power_budget | 先保留字段，补清楚事实、建议、结论三类语义 |
| 预留 sleep_blockers | 由实际 owner provider 逐项补齐 |
| Safety 预算唤醒 | 使用同一 participant registry 的 consumer-only / facts+consumer 登记，不新增第二张唤醒表 |
| PAUSE_OPTIONAL | 定义为可延后非关键工作，不是停止命令 |
| AUDIO_ACTIVE 注册 | audio_codec 保持事实 owner，power_policy_audio_bridge 只做跨层映射；先补非阻塞快照 |
| network_service | 继续拥有网络连接和 Wi-Fi PS，不拥有时间同步业务 |
| 时间同步 | 由 system_time_service / 独立 time-sync owner 维护周期和重试，普通状态只消费预算 |
| OTA/Hermes/Chat/BLE 交接 | 继续由 runtime_coordinator 负责，不复制成省电生命周期协议 |
| sleep_coordinator | 保持 dry-run，不增加反向控制 |
| 普通 weather/inbox/sync | 不强制注册，继续维护自己的周期和错误语义 |

## Non-Goals

- 不新增 ResourceManager、system_power_manager、通用资源账本或 runtime lease。
- 不让 power_policy 保存或操作服务 task handle。
- 不增加 stop_service、start_service、deinit_service provider 回调。
- 不把所有后台服务强制纳入注册表。
- 不把网络时间同步、天气、inbox 等业务继续堆进 network_service。
- 不在本计划内启用 Automatic Light-sleep、Deep Sleep、PMIC rail 控制或真实唤醒。

## Rollback Strategy

- Provider 表可以默认为空，现有核心事实路径继续发布原预算。
- 任一 provider 迁移失败时，撤回该 provider 注册和通知回调，保留 owner 原有周期消费。
- 如果 blocker 聚合异常，优先 fail-closed 为 sleep_permission = NONE，不影响 STANDBY 的 UI/Wi-Fi 基础节能。
- 不回滚已经验证的 power_budget、STANDBY、Wi-Fi PS 或 sleep_coordinator dry-run。

## Progress

- [x] 已完成现状解释和方案边界确认：注册事实/能力，不注册生命周期控制权。
- [x] 已创建本改造计划，与现有低功耗主计划保持父子关系。
- [x] Phase 0 字段盘点和合同补充（字段矩阵、Provider/Consumer 名单、audio 非阻塞快照决策已写入本文档）。
- [x] Phase 1 静态 Provider Registry（participant 表 + 注册 API，容量 8）。
- [x] Phase 2-4 owner 迁移和预算消费收口（Safety facts+consumer、audio bridge、coordinator 内置 provider、time sync 延后消费）。
- [x] Phase 5-6 聚焦测试、构建和 COM7 回归（31 passed、全量 503 passed、build 通过、COM7 冷启动到 network_service_ready 且无 panic/WDT）。
- [x] Phase 7 文档闭环（low-power-framework-architecture、runtime-owner-contract、project-framework、CHANGELOG 均已更新）。

复查与去重（2026-08-03）：

- 独立复查确认无 BLOCKER；修复 MAJOR：`sleep_permission` 推导移到 provider blocker 聚合之后统一收紧（任何 blocker 存在即不允许 LIGHT_ALLOWED），并补 `s_started` 加锁写入。
- 消除“缓存套缓存”：`power_policy_audio_bridge` 去掉本地缓存副本，`get_facts` 直读 `audio_codec_get_cached_session_snapshot()`；合同固化“provider 不维护中间缓存副本”。
- 消除 Safety 运行态镜像：删除 `safety_monitor_policy` 的 `runtime_running` 镜像字段，`power_facts` 与 `danger_detection_controller`“正在启动”文案均直读 `safety_monitor_session_get_snapshot()`。
- 专项 36 passed、全量 506 passed（3 个失败为预存 pySerial 环境问题）、`idf.py build` 通过。

## Next Step

本计划全部 Phase 已完成。后续可选：alert owner（app_alert_manager）单独注册 `ALERT_ACTIVE`、以及 network critical 在有真实"OTA/TLS 时间前置等待中"状态源时补注册短时 blocker；这些都不需要修改 power_policy.c 的核心聚合逻辑，按同一 participant 合同接入即可。真实 Automatic Light-sleep / Deep Sleep 仍属后续独立计划，本计划未启用。

## Related Context

- docs/context/knowledge/project/low-power-framework-architecture.md
- docs/context/plans/active/2026-06-01-low-power-framework-execution-plan.md
- docs/context/plans/active/2026-07-07-light-allowed-runtime-save-plan.md
- docs/context/knowledge/project/runtime-owner-contract.md
- docs/context/knowledge/project/owner-snapshot-lifecycle-freertos-contract.md
- docs/context/knowledge/project/project-framework.md
