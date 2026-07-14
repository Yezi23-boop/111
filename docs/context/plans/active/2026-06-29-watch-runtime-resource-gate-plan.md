---
id: watch-runtime-resource-gate-plan
tags: context, plans, resource-arbitration, foreground-runtime-gate, espdl, hermes, ble, ram, freertos
summary: ESP32-S3 手表运行时资源 gate 执行计划：在 UI+Wi-Fi 基础常驻上保留强前台独占和 ESP-DL 可抢占让路；后台 HTTPS gate 已撤除，各网络 owner 自行调度和重试。
last_reviewed: 2026-07-14
memory_type: task
scope: task
owners: docs/context/plans/active/2026-06-29-watch-runtime-resource-gate-plan.md, main/services/power/power_policy.c, main/services/safety/background_service_manager.c, components/espdl_inference, components/network_manager, main/services/memory_watch/memory_watch_service.c
triggers: runtime resource gate, foreground_runtime_gate, ESP-DL, Hermes foreground, BLE provisioning, internal RAM, resource arbitration, 前台重任务, 资源冲突
evidence_level: design
status: active
---

# Watch Runtime Resource Gate 执行计划

## 目标与全局

- 任务目标：在不新增大而全 `ResourceManager` 的前提下，为 ESP32-S3 手表保留一层很薄的强前台资源 gate，解决 `UI + Wi-Fi + BLE + Hermes 前台 + ESP-DL` 同时争抢 internal RAM、TLS、音频和启动峰值的问题。
- 为什么现在做：已有真机证据显示 internal RAM 极紧张，BLE controller 启动需要连续 internal block，ESP-DL 曾在 Hermes WebSocket/SSL 高压窗口后因 Fbank internal 分配失败崩溃；单纯迁 PSRAM 或缩栈不能解决“谁先用资源、谁让路”的结构性冲突。
- 完成后用户会看到什么变化：用户主动前台操作更稳定，Hermes 录音/WS、BLE 配网、未来重交互页面不再与 ESP-DL 硬抢；后台 inbox/sync/weather/health 继续由各自 owner 独立运行和重试。

## 核心资源模型

最终口径固定为：

```text
基础常驻：
UI + Wi-Fi

强前台独占：
Hermes / BLE 配网 / OTA / 未来重交互页面

可抢占增强任务：
ESP-DL / 安全检测 / 实时识别

低优先级后台：
inbox / sync / weather / health / SNTP
```

关键规则：

- UI + Wi-Fi 是基础常驻层，不通过本计划关闭。
- Hermes、BLE 配网、OTA、未来重交互页面属于强前台独占，同一时间只允许一个强前台 owner。
- ESP-DL 不和强前台平级硬抢；强前台进入时，ESP-DL 应暂停、跳过窗口或释放重资源。
- 后台 HTTPS 不统一串行，各 owner 保持自己的周期、pending、幂等和退避语义。
- BLE 启动因 `ESP_ERR_NO_MEM` 失败时只短等待并重试一次，仍失败则 fail closed。
- 当前不做集中 job queue；只有可重复真机证据证明特定组合冲突时，才在具体 owner 内局部错峰。

## 范围与非目标

本轮明确要做：

- 文档固定强前台独占和 ESP-DL 可抢占的边界。
- 新增最小 `foreground_runtime_gate` 概念和 source-testable API。
- 让 Hermes 前台进入/退出、BLE 配网/普通 BLE 启动窗口、OTA/未来页面有统一 owner 命名空间。
- 让 ESP-DL 在强前台 active 或 internal RAM 压力过高时让路。
- 后台 HTTPS 由 Memory Watch、weather 等 owner 自行调度，不新增共享 token。
- BLE 低内存失败时做单次延迟重试，不循环。

本轮明确不做：

- 不新增大而全 `ResourceManager`、`resource_policy`、`session_router` 或中心 runtime lease 总管。
- 不把所有网络请求改成集中 job queue。
- 不关闭 UI 或基础 Wi-Fi 常驻。
- 不重写 Hermes 页面、Memory Watch 协议或 `official_chat` 主线。
- 不强杀 ESP-DL task；只做可解释的暂停、跳过当前窗口或不启动。
- 不承诺 gate 能凭空解决长期 internal RAM 不足；它解决的是并发峰值、启动窗口和资源错峰。

## 设计边界

### 1. `foreground_runtime_gate`

定位：一个轻量 gate，不拥有硬件，不直接 stop 其他 owner，只记录强前台 owner 和短互斥窗口。

建议 owner 枚举：

```c
typedef enum {
    FOREGROUND_RUNTIME_OWNER_NONE = 0,
    FOREGROUND_RUNTIME_OWNER_HERMES,
    FOREGROUND_RUNTIME_OWNER_BLE_PROVISIONING,
    FOREGROUND_RUNTIME_OWNER_OTA,
    FOREGROUND_RUNTIME_OWNER_FUTURE_PAGE,
} foreground_runtime_owner_t;
```

建议最小接口：

```c
esp_err_t foreground_runtime_gate_acquire(
    foreground_runtime_owner_t owner,
    uint32_t timeout_ms);
void foreground_runtime_gate_release(foreground_runtime_owner_t owner);
bool foreground_runtime_gate_is_active(void);
foreground_runtime_owner_t foreground_runtime_gate_current_owner(void);
void foreground_runtime_gate_quiet_for(uint32_t ms, const char *reason);
bool foreground_runtime_gate_is_quiet(void);
```

约束：

- `acquire` 不直接暂停 ESP-DL，只发布可读状态；ESP-DL owner 自己观察并让路。
- `release` 只允许当前 owner 释放，避免其他模块假释放。
- UI/controller 不直接判断复杂资源状态，只通过 service 发起前台意图。
- 日志必须包含 owner、reason、是否被拒绝，便于串口复查。

### 2. ESP-DL 可抢占让路

ESP-DL 是“可抢占增强任务”，不是强前台独占 owner。

第一版行为：

- ESP-DL 启动推理前检查 `foreground_runtime_gate_is_active()`。
- 强前台 active 时，ESP-DL 不启动新窗口，记录 `ESP_ERR_INVALID_STATE` 或 `ESP_ERR_NO_MEM` 风格的可观测跳过。
- 如果 ESP-DL 已在一个小推理窗口中，不强杀；允许当前窗口短收尾后进入 paused/skip。
- Hermes 前台语音/WS、BLE 启动或 OTA 期间，ESP-DL 应主动让路。

必须避免：

- `power_policy` 或 `foreground_runtime_gate` 直接 suspend/delete ESP-DL task。
- 背景 manager 只看 mic 空闲就立刻启动 ESP-DL，不看 WS/TLS 或前台 gate。

### 3. 后台 HTTPS owner 自治

`background_https_gate` 曾在 2026-06-29 到 2026-07-14 期间串行 Memory Watch 与 weather 后台请求。复查确认它不预留 RAM、不排空在途请求，还增加 busy/quiet 状态和重试复杂度，因此已经撤除。

当前约束：

- health、sync、inbox、mark-read、alert、weather 由各 owner 自行调度和处理错误。
- 不建立共享 token、中央 job queue 或统一优先级。
- 若特定组合出现可重复 `NO_MEM`，优先调整具体 owner 的启动延迟、退避或 buffer 归属。
- 不把一次偶发超时作为恢复全局 gate 的依据。

### 4. BLE 单次延迟重试

当前普通 BLE presence 启动已有 internal heap guard。第一版只加“安静重试一次”：

```text
ble_presence_start() -> ESP_ERR_NO_MEM
  -> vTaskDelay(short)
  -> retry once
  -> still fail: fail closed + toast
```

约束：

- 只重试一次，不循环。
- 不在主界面普通 BLE 按钮里默认关闭整个 Wi-Fi；BLE 配网专门入口可另行定义更强互斥。
- 不把 BLE 失败伪装成成功。

## 分阶段执行

### 阶段 0：计划落地与现有证据复核

要做：

- 固定本计划。
- 复核 `watch-resource-arbitration-report.md` 与 `runtime-owner-contract.md` 是否需要同步补充“强前台独占 / ESP-DL 可抢占”口径。
- 不改代码。

验收：

- `validate_context.py --level standard` 通过。
- `git diff --check` 通过。

### 阶段 1：新增 `foreground_runtime_gate` 最小实现

要做：

- 新增窄 service/helper，提供 acquire/release/current/quiet window。
- 使用 FreeRTOS mutex 或 critical section 保护 owner 状态；不做动态内存分配。
- 增加 source tests 锁住：单 owner、非 owner 不能释放、quiet window 可查询、日志/枚举存在。
- 本阶段只提供 gate，不接入 Hermes/BLE/ESP-DL，避免把行为变更和基础设施混在一个闭环里。

验收：

- source tests 通过。
- `idf.py build` 通过。
- 不改变现有运行行为，只提供 gate。

### 阶段 2：ESP-DL 让路接入

要做：

- `background_service_manager` 或 Safety Monitor 启动 ESP-DL 前检查强前台 gate 和内存压力。
- `components/espdl_inference` 推理窗口前增加轻量检查或沿用已有 internal RAM 预检，强前台 active 时跳过当前窗口。
- 日志区分：内存不足跳过、强前台 active 跳过、正常推理。
- 第一版先在 `background_service_manager` 合成 Safety Monitor 目标态时读取 `foreground_runtime_gate`，强前台 active 时把阻塞原因设为 `FOREGROUND_RUNTIME`，不启动或恢复 ESP-DL runtime；不让 ESP-DL 组件反向依赖 `main/services`。

验收：

- source tests 覆盖 ESP-DL 不在强前台期间启动。
- 真机 Hermes 前台录音/WS 时不会启动新 ESP-DL 重任务。
- 无 Guru/panic/stack overflow。

### 阶段 3：Hermes 与未来前台页面接入

要做：

- Hermes 页面前台 WebSocket/录音上传进入时 acquire `FOREGROUND_RUNTIME_OWNER_HERMES`。
- 离开页面、WS 结束、错误终态或超时 release。
- 保留后台 pending `/sync`，不占强前台 owner。
- 为未来页面保留 `FUTURE_PAGE` 枚举，但不新增 UI 行为。

验收：

- Hermes 前台体验无回归。
- 离页后 WS 关闭，后台 `/sync` 仍可继续。
- source tests 覆盖 acquire/release 生命周期。

### 阶段 4：后台 HTTPS gate 历史接入与撤除

历史结果：

- 2026-06-29 曾完成 Memory Watch 与天气 HTTPS 的共享 token 接入。
- 2026-07-08 曾修复 gate busy 被误判为 Hermes 离线的问题。
- 2026-07-14 根据简单性和 owner 边界复查撤除该 gate、quiet window、CMake/source tests 和所有调用点。

当前验收：

- 后台 HTTP 不依赖共享 token 或中央 worker。
- health/sync/inbox/weather 保留各 owner 原有调度和重试。
- 全仓代码与 active knowledge 不再引用 `background_https_gate`；历史 run 可保留证据。

### 阶段 5：BLE 单次延迟重试

要做：

- BLE presence 启动因 `ESP_ERR_NO_MEM` 失败时，等待短时间后重试一次。
- 失败仍 fail closed，并保持 UI toast 行为。
- BLE 配网专门入口是否需要更强 Wi-Fi 互斥，另开小闭环评估。

验收：

- source tests 覆盖只重试一次。
- 真机点击 Bluetooth：低内存时不 panic，不循环；资源足够时可启动。

### 阶段 6：真机高压回归

要做：

- COM3 冷启动采集 60 秒。
- Hermes 前台录音/WS + 离页后台 `/sync`。
- Bluetooth 点击启动。
- 天气/inbox/health 由各 owner 在后台运行。
- 如用户允许，再加 ESP-DL 安全检测高压场景。

验收：

- 无 panic/Guru/stack overflow。
- 无 `esp-aes: Failed to allocate memory`。
- BLE 失败路径可解释，成功路径正常。
- Hermes 前台优先，ESP-DL 可让路；后台 HTTPS 自治并且错误可解释。

## 进度

- `[x]` 阶段 0：计划落地。
- `[x]` 阶段 1：新增 `foreground_runtime_gate` 最小实现。
- `[x]` 阶段 2：ESP-DL 让路接入。
- `[x]` 阶段 3：Hermes 与未来前台页面接入。
- `[x]` 阶段 4：后台 HTTPS gate 历史接入后已于 2026-07-14 完整撤除。
- `[x]` 阶段 5：BLE 单次延迟重试。
- `[ ]` 阶段 6：真机高压回归。（foreground/BLE fail-closed 回归已完成；撤除后台 gate 后的公网 HTTPS/WSS 自然并发和 ESP-DL running -> 强前台让路仍待补测。）

## 决策记录

- 2026-06-29：确认资源模型采用“UI+Wi-Fi 基础常驻、强前台独占、ESP-DL 可抢占增强、后台 HTTPS 低优先级”。
- 2026-06-29：确认 ESP-DL 可以给 Hermes/BLE/OTA/未来前台页面让路，不作为同级强前台 owner。
- 2026-06-29：确认先做薄 gate，不做集中 job queue 或大 ResourceManager。
- 2026-06-29：确认后台 HTTPS gate 有价值，但只按“降低并发峰值和碎片窗口”定位，不夸大为根治 internal RAM。
- 2026-06-29：确认 Hermes 前台持有强前台 owner，但录音麦克风占用仍由 recorder 自己向 background manager 声明，避免重复 owner。
- 2026-06-29：确认后台 HTTPS gate 只覆盖低优先级 health/sync/inbox/mark-read/weather，不覆盖 Hermes 前台 WSS、语音上传、文本命令或 cancel。
- 2026-07-14：重审 `watch-resource-arbitration-report.md`，将旧的统一 memory pressure 阈值、全局 allocator、固定内存池和 LVGL heap 整体迁移降为证据驱动候选；同时识别 foreground `timeout_ms` 无实际语义、同 owner 无引用计数，以及 background token 不记录 holder、quiet window 不排空在途请求等缺点。
- 2026-07-14：用户确认撤除 `background_https_gate`。理由是它不提供资源预留或在途排空，却引入共享 token、quiet/busy 状态和跨 owner 测试成本；后台网络恢复为 owner 自治。保留 `foreground_runtime_gate`、ESP-DL 主动让路、BLE 单次延迟重试和各 owner 的瞬时错误退避。

## 意外与发现

- `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` 与 `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y` 已开启，TLS 大块缓冲倾向 PSRAM；但 `esp_http_client`、socket/LwIP、crypto DMA 和多个后台 task 同时活跃仍会制造 internal 峰值与 largest block 碎片窗口。
- Memory Watch HTTP client 当前显式使用 8KB RX + 8KB TX buffer，response 大 buffer 已优先 PSRAM；天气 HTTPS 使用独立 `esp_http_client`，两者不再通过共享 gate 串行。
- BLE guard 需要的连续 internal block 远高于当前部分高压日志中的 largest block；gate 只能减少撞车概率，不能替代长期内存治理。
- Memory Watch 的 health/inbox 仍保留 pending + due 重试和瞬时错误不翻转在线状态的语义；撤除 gate 只移除共享 busy/quiet 来源，不删除 owner 自身的网络恢复能力。

## 验证与验收

计划运行的验证命令：

```powershell
uv run python scripts/context/validate_context.py --level standard --q "watch runtime resource gate foreground background HTTPS ESP-DL BLE Hermes" --brief
git diff --check
uv run python -m pytest tests/test_memory_watch_service_source.py tests/test_ble_presence_source.py tests/test_main_screen_ble_toggle_source.py -q
idf.py build
```

期望看到的结果：

- context 校验通过。
- diff 无空白错误。
- source tests 覆盖 foreground gate、ESP-DL 让路和 BLE fail-closed 边界。
- `idf.py build` 通过。
- 真机高压场景无 panic/Guru/stack overflow。

当前实际结果：

- 阶段 0 文档已落地。
- 阶段 1 已新增 `main/services/runtime_gate/foreground_runtime_gate.[ch]`、接入 `main/CMakeLists.txt`，并新增 `tests/test_foreground_runtime_gate_source.py` 锁住最小 API 与“非 ResourceManager”边界。
- 阶段 1 验证：`uv run python -m unittest tests.test_foreground_runtime_gate_source` 通过；`idf.py build` 通过，`111.bin` `0xabc140`，最小 app 分区剩余 `0x343ec0`（23%）。
- 阶段 2 已在 `background_service_manager` 增加 `FOREGROUND_RUNTIME` 阻塞原因：强前台 active 时 Safety Monitor 目标态为 false，ESP-DL 不启动或恢复；gate 不直接 suspend/delete ESP-DL task。验证：`uv run python -m unittest tests.test_safety_monitor_session_source tests.test_danger_detection_controller_source tests.test_foreground_runtime_gate_source` 通过。
- 阶段 3 已完成：`memory_watch_service_set_foreground_active(true/false)` 在 Hermes 前台进入/退出时 acquire/release `FOREGROUND_RUNTIME_OWNER_HERMES`，并通知 `background_service_manager_notify_foreground_runtime_changed()`；录音路径继续由 `memory_watch_recorder.c` 声明 foreground audio。
- 阶段 4 历史上曾接入 `background_https_gate`；2026-07-14 已删除实现文件、CMake、Memory Watch/weather/official_chat/BLE 调用和对应 source test。后台请求恢复为 owner 自治，不保留空 wrapper。
- 阶段 5 当前为：主界面 Bluetooth 显式点击路径持有 `FOREGROUND_RUNTIME_OWNER_BLE_PROVISIONING`；若 `network_manager_set_ble_enabled(true)` 返回 `ESP_ERR_NO_MEM`，短等待后只重试一次，仍失败则 fail closed + toast。
- 2026-07-14 gate 撤除聚焦验证：Memory Watch voice/service、weather、official_chat、BLE UI、runtime board test、foreground gate 和 Safety Monitor 共 `63 tests` 通过。
- 2026-07-14 gate 撤除完整验证：全量 source tests `420 passed / 7 failed`，7 项与任务前目录整理交接记录一致；`idf.py fullclean; idf.py build` 通过，`111.bin=0xac5a60`，app 余量 `0x33a5a0`（23%）。真机组合高压仍归阶段 6。
- 阶段 3-5 构建验证：`idf.py build` 通过，`111.bin` `0xabcc20`，最小 app 分区剩余 `0x3433e0`（23%）。
- 阶段 6 已新增默认关闭的板端自动高压测试入口：`runtime_resource_gate_board_test_start()` 只在 `CONFIG_RUNTIME_RESOURCE_GATE_BOARD_TEST=y` 时创建一次性测试 task，默认关闭时空返回。撤除后台 gate 后，该测试保留 Hermes foreground、BLE foreground owner 和可选真实 BLE toggle。
- 阶段 6 历史自动 gate 压测 COM3 通过：`board_logs/2026-06-29-10-18-11-runtime-resource-gate-board-test-auto.log` 完整跑完，无 Guru、panic、stack overflow、NO_MEM；其中 background busy/quiet 结果仅作为已撤除方案的历史证据。
- 阶段 6 真实 BLE toggle 首轮发现并修复 cache-disabled 断言：真实 BLE toggle 会写 BLE NVS 偏好，NVS/flash 写入时 cache 可能关闭；若测试任务栈在 PSRAM，会触发 `esp_task_stack_is_sane_cache_disabled()` 断言。已改为 `CONFIG_RUNTIME_RESOURCE_GATE_BOARD_TEST_ENABLE_REAL_BLE_TOGGLE=y` 时使用 `MALLOC_CAP_INTERNAL` 栈。
- 阶段 6 真实 BLE toggle 修复后 COM3 结果：`board_logs/2026-06-29-10-31-09-runtime-resource-gate-board-test-real-ble-internal-stack.log` 未见 assert/Guru/panic/stack overflow；BLE guard 返回 `real_ble_enable: result=ESP_ERR_NO_MEM`，随后 `real_ble_disable: result=ESP_OK`，属于可解释 fail closed。结束内存约 `internal_free=43690 largest=20480 psram_free=6953672`。
- 阶段 6 当前限制：撤除后台 gate 后的公网 HTTPS/WSS 自然并发尚未板测；Safety Monitor 当时包含 `user_disabled` 阻塞，未覆盖 ESP-DL 正在运行后被强前台暂停的完整路径。历史构建和串口结果继续保留，但不能代替撤除后的新回归。
- 2026-07-08 补修后台 HTTPS 重试语义：真机日志显示 health/inbox 在 gate busy 时会连续失败并让 `hermes_online` 抖到 0。`memory_watch_service` 已增加 health worker busy/pending/retry due，transient failure 保持上次在线状态；inbox poll 失败保留 pending 并按 5 秒 due 重试，auth/protocol 错误才暂停。验证：`tests.test_memory_watch_service_source` 20 passed；相关 gate/source tests 27 passed；`idf.py build` 通过（`111.bin` `0xace350`，app free `0x331cb0`/23%）。attempt log：`docs/context/runs/2026-07-08-attempt-memory-watch-background-https-retry.md`。

## 幂等与恢复

- 如果中途中断，下次从“进度”中第一个未完成阶段继续。
- 如果 `foreground_runtime_gate` 引入行为回归，可先只保留 source-level API，不接入 Hermes/BLE/ESP-DL。
- 如果撤除后台 gate 后出现可重复 HTTPS/TLS 资源冲突，先在具体 owner 内错开启动或退避，不直接恢复全局 token。
- 如果 BLE 单次延迟重试无收益，保留 fail closed，撤回延迟重试。

## 文档与收尾规则

本计划后续涉及 FreeRTOS、RAM/PSRAM、资源仲裁和底层并发。每完成一个代码 gate，在宣布完成前必须：

1. 新建 `docs/context/runs/YYYY-MM-DD-attempt-<feature>.md` 记录错误签名、证伪路径和验证证据。
2. 更新 `docs/context/CHANGELOG.md` 顶部摘要。
3. 按需更新本 active plan 的 `Progress / Validation and Acceptance / Next Step`，尤其是暂停、接手或状态反转时。
4. 执行：

```powershell
uv run python scripts/context/validate_context.py --level standard --q "watch runtime resource gate foreground ESP-DL BLE Hermes HTTPS owner" --brief
```

## 下一步

- 下一步最小动作：阶段 6 补测撤除后台 gate 后的公网 HTTPS/WSS 自然并发，以及 ESP-DL running -> 强前台让路。COM3 冷启动后依次观察 Hermes 前台/离页、official_chat 预连接、Bluetooth 点击、天气/inbox/health 后台并发和 Safety Monitor/ESP-DL 让路日志。
- 前台资源的 owner 内部 create/destroy、自动切换、5 秒旧 owner 超时和 quiesced ACK 细化到 `2026-07-14-watch-foreground-session-lifecycle-plan.md`；后续代码按新计划分阶段执行，本计划继续保留资源 gate 历史和阶段 6 高压验收。
