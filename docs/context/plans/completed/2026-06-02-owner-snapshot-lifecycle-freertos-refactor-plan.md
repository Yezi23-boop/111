---
id: owner-snapshot-lifecycle-freertos-refactor-plan
tags: plan, active, freertos, owner, snapshot, lifecycle, resource-management
summary: 将当前仓库长期 owner 按 snapshot、生命周期和 FreeRTOS 通信方式逐步整理；V1 不引入 runtime lease，不做中心化资源管理。
created: 2026-06-02
updated: 2026-06-02
last_reviewed: 2026-06-02
status: active
plan_status: completed
memory_type: project_plan
scope: repo
owners: main/services/official_chat_service.c, main/services/network_service.c, main/services/power_service.c, main/services/background_service_manager.c, main/services/sleep_coordinator.c, docs/context/knowledge/project/owner-snapshot-lifecycle-freertos-contract.md
triggers: owner snapshot, lifecycle, FreeRTOS, runtime owner, power_budget, resource release
evidence_level: design
---

# Owner Snapshot / Lifecycle / FreeRTOS 整理计划

## Purpose / Big Picture

目标是把当前仓库长期 owner 的状态发布、生命周期和跨任务通信方式整理成可持续维护的 FreeRTOS 骨架。

当前框架基线：

```text
器件驱动放 components/xxx
板级语义放 board_power / hardware_init
长期系统能力放 service
资源语义和 session 放 domain owner
```

运行时协作基线：

```text
owner 发布只读 snapshot
  -> power_policy / service 聚合事实
  -> budget / command / notify
  -> owner 自己执行生命周期、降级、恢复和资源释放
```

本计划不是一次性大重构。每个 owner 都必须按“小闭环”推进：补 snapshot、整理命令/通知、固定资源释放、补日志、跑测试。

## Scope / Non-Goals

范围：

- 梳理 `official_chat_service`、`network_service`、`power_service`、`background_service_manager`、`sleep_coordinator` 的 owner 形态。
- 统一 snapshot getter 口径：读取副本，不做 I/O、不阻塞、不推进状态。
- 统一生命周期口径：init/start/ready/running/blocked/stopping/stopped 或等价可观测字段。
- 统一 FreeRTOS 通信：轻量唤醒用 task notification，带参数命令用 queue，ready gate 用 event group，状态保护用 mutex/critical section。
- 固定资源结束必须由真实 owner 自己 release，并发布 inactive/released snapshot。

不做：

- 不新增 `runtime lease`。
- 不新增大而全 `ResourceManager`、`resource_policy`、`system_power_manager`。
- 不让 `power_policy` 直接关硬件、释放 session、suspend task。
- 不让 UI 直接 stop 长期后台 runtime。
- 不改 ESP sleep 路线，不默认进入 Light Sleep / Deep Sleep。
- 不为了形式统一强行改 vendor/official_chat C++ 内部既有线程模型；外层 service 只需暴露清晰 snapshot 和生命周期 API。

## Progress

- [x] 新增 `owner-snapshot-lifecycle-freertos-contract.md`，固定 owner 四分类、snapshot 合同、生命周期合同和 FreeRTOS 通信合同。
- [x] 将该专项卡挂到 `project-framework.md`、`runtime-owner-contract.md` 和 `INDEX.agent.md`。
- [x] `validate_context.py --level routing` 通过，query golden 14/14。
- [x] Phase 1：整理 `official_chat_service`。
- [x] Phase 2：整理 `network_service`。
- [x] Phase 3：整理 `power_service`。
- [x] Phase 4：整理 `background_service_manager`。
- [x] Phase 5：整理 `sleep_coordinator`。

## Decision Log

- V1 采用 `FreeRTOS owner snapshot + power_budget`，不做 `runtime lease`。
- V1 `official_chat_service` 是纯前台按需服务：用户退出聊天页面就完整停止 official_chat 服务本身，后续如果要后台聊天再重新拆分前台退出与服务 shutdown。
- “结束资源”不是删除记录，而是向 owner 发命令，由 owner 停止新工作、短收尾、释放 session/硬件并更新 snapshot。
- `power_policy` 是预算发布者，不是硬件执行者。
- 长期 owner 的 getter 不允许顺手推进状态；真实推进必须在 owner task 或明确 owner 执行上下文中完成。
- 整理顺序优先从跨线程意图最明显的 `official_chat_service` 开始，再处理网络探测可打断性。

## Phase 1: official_chat_service

目标：

- 把进入聊天、退出聊天并停止服务等跨线程意图从裸 `volatile` flag 整理为 command queue 或 task notification。
- 补 `official_chat_service_get_snapshot(out)`，至少表达 service state、foreground active、stop pending、last_error。
- 保持前台 AI 生命周期仍归 `official_chat_service`，不把音频 session owner 挪出 `audio_codec`。
- V1 不保留“页面退出但 official_chat 服务继续 idle”的语义；页面退出就是完整停止服务。

建议接口草案：

```c
typedef enum {
    OFFICIAL_CHAT_SERVICE_CMD_ENTER_FOREGROUND,
    OFFICIAL_CHAT_SERVICE_CMD_LEAVE_FOREGROUND_AND_STOP,
    OFFICIAL_CHAT_SERVICE_CMD_NETWORK_READY,
    OFFICIAL_CHAT_SERVICE_CMD_BUDGET_CHANGED,
} official_chat_service_cmd_type_t;

typedef struct {
    official_chat_service_state_t state;
    bool foreground_active;
    bool stop_pending;
    esp_err_t last_error;
} official_chat_service_snapshot_t;

esp_err_t official_chat_service_get_snapshot(official_chat_service_snapshot_t *out);
```

验收：

- [x] UI/API 只投递 command 或调用语义 API，不直接改服务内部 flag。
- [x] 服务 task 消费 `LEAVE_FOREGROUND_AND_STOP` 后，自己停止监听、释放前台音频、进入 quiet period、销毁 official_chat handle，并发布 stopped / released snapshot。
- [x] `background_service_manager` 可继续看到前台音频状态，Safety Monitor 不被误启动。
- [x] source test 锁定 `official_chat_service` 不再新增跨线程裸 flag 协议。
- [x] source test 锁定页面退出会走完整服务停止语义，而不是只清前台标志。
- [x] `idf.py build` 通过。

实现记录：

- `official_chat_service` 新增 command queue：`ENTER_FOREGROUND` / `LEAVE_FOREGROUND_AND_STOP`。
- `official_chat_service` 新增 `official_chat_service_get_snapshot(out)`，包含 `state / foreground_active / stop_pending / last_error`。
- UI 返回 AI 页面时调用 `official_chat_service_leave_foreground()`，V1 语义为“退出前台并完整停止服务”。
- 旧 `official_chat_service_request_shutdown()` 保留为兼容 API，但内部转发到 `leave_foreground`。
- 快照由短 critical section 复制，getter 不做 I/O、不阻塞、不推进状态。

验证记录：

```powershell
uv run python -m pytest tests/test_official_chat_service_source.py tests/test_ai_ui_entry_source.py
# 14 passed

uv run python -m pytest tests/test_audio_codec_port_source.py tests/test_official_chat_service_source.py tests/test_safety_monitor_session_source.py tests/test_ai_ui_entry_source.py
# 25 passed

. 'D:\esp-idf\v5.5.3\esp-idf\export.ps1'; idf.py build
# build passed; app partition warning remains: 5% free
```

## Phase 2: network_service

目标：

- 补统一 `network_service_get_snapshot(out)`。
- 把云端 ready probe 从长阻塞循环整理为可被 budget/notify 打断的小步状态机。
- STANDBY V1 仍不断网，只做 Wi-Fi PS 和同步节流。

建议 snapshot 字段：

```text
state
wifi_connected
service_ready
probe_active
probe_paused_by_budget
power_save_applied
last_error
last_probe_result
```

验收：

- [x] `power_budget.network_budget=SYNC_PAUSED` 后，不再启动新的非关键云端探测。
- [x] 已在进行的探测每轮 attempt 前检查 budget；同步暂停时退出探测并发布 `probe_paused_by_budget`。
- [x] Wi-Fi 连接保持，不主动断 AP。
- [x] source test 锁定 `network_service` 消费 budget 不直接改变产品主状态。
- [x] `idf.py build` 通过。

实现记录：

- `network_service` 新增 `network_service_get_snapshot(out)`，包含 `state / wifi_connected / service_ready / probe_active / probe_paused_by_budget / power_save_applied / last_error / last_probe_result`。
- `network_service_get_state()` 和 `network_service_is_service_ready()` 改为读取 snapshot 副本。
- 云端 ready probe 每轮 attempt 前读取 `power_policy_get_budget()`；STANDBY 或 `network_sync_allowed=false` 时暂停探测，不断开 AP。
- Wi-Fi PS 仍由 `network_service_apply_power_budget()` 调用 `wifi_control_set_power_save(power_save)`，不调用 `network_manager_disconnect()` 或 `esp_wifi_stop()`。

验证记录：

```powershell
uv run python -m pytest tests/test_network_service_wifi_management_source.py tests/test_network_service_ble_source.py tests/test_nonblocking_boot_source.py tests/test_power_integration_source.py
# 33 passed

. 'D:\esp-idf\v5.5.3\esp-idf\export.ps1'; idf.py build
# build passed; app partition warning remains: 5% free
```

## Phase 3: power_service

目标：

- 评估是否把 `power_service_get_state()` 从只读指针改为 `power_service_get_snapshot(out)`。
- 状态变化时 notify `power_policy`，周期采样仍作为兜底。
- 保持 AXP2101 V1 只读，不写电源轨或 sleep/wakeup 寄存器。

验收：

- [x] snapshot 复制期间不会暴露半更新状态。
- [x] `power_policy` 不直接读 PMIC 寄存器。
- [x] 上电首帧电源日志和状态变化日志保留。
- [x] 状态变化时 notify `power_policy`，周期采样仍作为兜底。
- [x] `idf.py build` 通过。

实现记录：

- `power_service` 新增 `power_service_get_snapshot(out)`，只复制双缓冲活动快照，不访问 PMIC/I2C、不推进采样状态机。
- 旧 `power_service_get_state()` 保留为兼容接口，新增代码优先使用 out-copy API。
- `power_policy_recalculate()` 改为读取 `power_service_get_snapshot(&power_snapshot)`，不再把 `power_service_get_state()` 的只读指针直接传入预算计算。
- `power_service` 在有效电源状态变化后调用 `power_policy_notify(POWER_POLICY_NOTIFY_POWER_STATE)`；1s/2s/5s 采样节奏仍作为兜底。
- AXP2101 / `board_power` 只读边界不变，本阶段不写电源轨、不写 sleep/wakeup 寄存器。

验证记录：

```powershell
uv run python -m pytest tests/test_power_service_source.py tests/test_power_integration_source.py tests/test_board_power_source.py tests/test_axp2101_power_source.py
# 28 passed

. 'D:\esp-idf\v5.5.3\esp-idf\export.ps1'; idf.py build
# build passed; app partition warning remains: 5% free
```

## Phase 4: background_service_manager

目标：

- 保持它只合成 Safety Monitor 目标态，不变成通用调度器。
- 把调用方同步触发的策略应用逐步整理成 notify + 低频兜底。
- snapshot 继续表达 user intent、policy allowed、should_run、runtime_running、block reason。

验收：

- [x] 用户开关、前台音频、power budget 变化都能触发目标态重算。
- [x] Safety Monitor start/stop 仍由 `safety_monitor_session` 执行。
- [x] 页面退出不停止后台能力。

实现记录：

- `background_service_manager` 新增内部 task notification reason：`USER_SWITCH / FOREGROUND_AUDIO / POWER_BUDGET / STARTUP / PERIODIC`。
- `background_service_manager_set_danger_detection_enabled()` 和 `background_service_manager_set_foreground_audio_active()` 只更新 owner fact 并 notify manager task，不再由调用方同步推进 `apply_policy()`。
- `background_service_manager` task 在 UI first frame gate 后先执行一次 startup 重算，随后用 `xTaskNotifyWait(..., k_policy_poll_ticks)` 同时支持关键事件唤醒和 1 秒周期兜底。
- `power_policy_store_budget()` 只在预算有效变化后调用 `background_service_manager_notify_policy_changed()`；该通知不携带预算、不启动/停止 Safety Monitor，只唤醒 manager task 重新读取 `power_policy_get_budget()`。
- Safety Monitor runtime 生命周期仍只由 `safety_monitor_session_apply(should_run, reason)` 执行，`background_service_manager` 继续只合成目标态和 block reason。

验证记录：

```powershell
$env:UV_CACHE_DIR='D:\esp32S3\111\.uv-cache'
uv run python -m pytest tests/test_safety_monitor_session_source.py tests/test_danger_detection_controller_source.py tests/test_audio_codec_port_source.py tests/test_official_chat_service_source.py tests/test_power_integration_source.py
# 40 passed
```

## Phase 5: sleep_coordinator

目标：

- 继续只消费 `power_policy_get_budget()`，不逐个询问 owner。
- dry-run snapshot 保持可观测。
- 后续真实 sleep 前，再根据 blocker 完整性单独评审，不提前引入 `runtime lease`。

验收：

- [x] 默认不调用 `esp_light_sleep_start()` 或 `esp_deep_sleep_start()`。
- [x] sleep readiness 日志能显示 permission、blockers、interval hint、budget version。

实现记录：

- `sleep_coordinator` 继续只在 `sleep_coordinator_record_dry_run()` 中调用 `power_policy_get_budget()`。
- `sleep_coordinator` 不读取 `ui_refresh_policy`、`network_service`、`network_manager`、`wifi_control`、`audio_codec`、`axp2101` 或 `pcf85063atl`。
- `sleep_coordinator` 只保留 `DISABLED / DRY_RUN` 模式，不保留 `LIGHT_TEST / DEEP_TEST`、sleep test result 或手动测试编译开关。
- dry-run snapshot 继续发布 `initialized / started / mode / sleep_permission / sleep_blockers / sleep_interval_hint_ms / budget_version / dry_run_count`。
- dry-run 日志格式包含 `budget_version=%u permission=%s blockers=%s interval_ms=%u`。
- `main/app/app_main.c` 默认只调用 `sleep_coordinator_start()`，不调用 `sleep_coordinator_set_mode()`，普通开机不会启用真实 sleep。

验证记录：

```powershell
uv run python -m pytest tests/test_power_integration_source.py
# passed; includes test_sleep_coordinator_is_dry_run_only and manual sleep API absence check

rg --line-number "esp_(light|deep)_sleep_start|esp_sleep_enable|sleep_coordinator|power_policy_get_budget|dry_run|sleep_permission|sleep_blockers|budget_version" main components tests docs/context/knowledge/project docs/context/plans/active
# firmware source hits show sleep_coordinator dry-run only; no main/components ESP sleep API call sites
```

## Validation and Acceptance

文档阶段：

```powershell
uv run python scripts/context/validate_context.py --level standard --q "owner snapshot lifecycle FreeRTOS communication runtime owner" --brief
```

涉及 `INDEX.agent.md`、query golden 或路由时：

```powershell
uv run python scripts/context/validate_context.py --level routing --q "owner snapshot lifecycle FreeRTOS communication runtime owner" --brief
```

源码阶段：

- 修改前先跑相关 source tests，确认旧基线。
- 修改后跑对应 source tests。
- 确认 `export.ps1` 可用后执行 `idf.py build`。
- 如涉及启动或板端 runtime，再用 `app-flash-monitor` 限时采集日志。

板端日志至少关注：

- `boot_stage:*`
- owner state change
- snapshot/budget version
- resource active/released
- blocker reason
- no Guru / panic / NO_MEM / watchdog

## Idempotence and Recovery

- 每个 Phase 都应能单独回滚，不依赖后续 Phase。
- 每次只改一个 owner 的通信方式，避免多 owner 同时变更导致难定位。
- 如果某个 owner 的现有模型来自 vendor/官方 C++ 组件，先只在外层 service 加 snapshot/command 边界，不强行重写内部线程模型。
- 如果 `idf.py build` 失败，优先回退当前 Phase 的接口变更，不改动已验证的框架文档。

## Outcomes & Retrospective

完成结果：

- `official_chat_service`、`network_service`、`power_service`、`background_service_manager`、`sleep_coordinator` 均已按 V1 owner snapshot / lifecycle / FreeRTOS 通信合同完成整理。
- V1 没有引入 `runtime lease`、`ResourceManager`、中心化硬件管家或 `power_policy` 硬件执行路径。
- 前台聊天、网络探测、电源采样、后台 Safety Monitor 目标态、sleep dry-run 都有对应 snapshot 或可观测日志。
- 真实资源释放仍由各 owner 自己执行；`power_policy` 只聚合事实和发布 budget。
- `sleep_coordinator` 仍只 dry-run，不调用真实 ESP sleep API。

残留风险：

- 当前完成的是 source-level / build-level 框架整理，未执行本轮板端 app-flash 观察。
- `idf.py build` 仍提示 app 分区仅剩约 5% free，这是既有容量风险，不属于本计划新增问题。
- 后续若要引入 ESP-IDF Automatic Light-sleep、Deep Sleep 或外部唤醒增强，必须新建独立计划并重新评审 blocker 完整性，不能在本计划基础上直接恢复手动 sleep harness。
