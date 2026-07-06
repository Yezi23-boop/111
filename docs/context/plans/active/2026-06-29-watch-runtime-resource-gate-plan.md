---
id: watch-runtime-resource-gate-plan
tags: context, plans, resource-arbitration, foreground-runtime-gate, background-https-gate, espdl, hermes, ble, ram, freertos
summary: ESP32-S3 手表运行时资源 gate 执行计划：在 UI+Wi-Fi 基础常驻上，引入强前台独占、ESP-DL 可抢占让路、后台 HTTPS 低优先级错峰，解决 Hermes/BLE/ESP-DL/未来前台页面的结构性资源冲突。
last_reviewed: 2026-06-29
memory_type: task
scope: task
owners: docs/context/plans/active/2026-06-29-watch-runtime-resource-gate-plan.md, main/services/power_policy.c, main/services/background_service_manager.c, components/espdl_inference, components/network_manager, main/services/memory_watch_service.c
triggers: runtime resource gate, foreground_runtime_gate, background_https_gate, ESP-DL, Hermes foreground, BLE provisioning, internal RAM, resource arbitration, 前台重任务, 后台 HTTPS, 资源冲突
evidence_level: design
status: active
---

# Watch Runtime Resource Gate 执行计划

## 目标与全局

- 任务目标：在不新增大而全 `ResourceManager` 的前提下，为 ESP32-S3 手表补一层很薄的运行时资源 gate，解决 `UI + Wi-Fi + BLE + Hermes 前台 + ESP-DL + 后台 HTTPS` 同时争抢 internal RAM、TLS、音频和启动峰值的问题。
- 为什么现在做：已有真机证据显示 internal RAM 极紧张，BLE controller 启动需要连续 internal block，ESP-DL 曾在 Hermes WebSocket/SSL 高压窗口后因 Fbank internal 分配失败崩溃；单纯迁 PSRAM 或缩栈不能解决“谁先用资源、谁让路”的结构性冲突。
- 完成后用户会看到什么变化：用户主动前台操作更稳定，Hermes 录音/WS、BLE 配网、未来重交互页面不再被 ESP-DL 或后台 HTTPS 抢资源；ESP-DL 安全检测在强前台进入时可暂停让路；后台 inbox/sync/weather/health 可延后而不影响前台体验。

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
- 后台 HTTPS 是低优先级，必须串行、可延后、可跳过。
- BLE 启动前可打开 quiet window，阻止新的后台 HTTPS，并允许一次安静重试。
- 当前不做集中 job queue；如果 token gate 证明不够，再单独评估集中网络 worker。

## 范围与非目标

本轮明确要做：

- 文档固定强前台独占、ESP-DL 可抢占、后台 HTTPS 错峰的边界。
- 新增最小 `foreground_runtime_gate` 概念和 source-testable API。
- 让 Hermes 前台进入/退出、BLE 配网/普通 BLE 启动窗口、OTA/未来页面有统一 owner 命名空间。
- 让 ESP-DL 在强前台 active 或 internal RAM 压力过高时让路。
- 新增最小 `background_https_gate`，覆盖 Memory Watch 后台 health/sync/inbox/mark-read 与天气 HTTPS。
- BLE 低内存失败时做 quiet window + 单次重试，不循环。

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

### 3. `background_https_gate`

定位：后台 HTTPS 令牌门禁，解决低优先级网络请求同一时间创建多套 `esp_http_client` / TLS / socket 的峰值。

第一版覆盖：

- Memory Watch 后台 health。
- Memory Watch 后台 `/sync`。
- Memory Watch inbox poll。
- Memory Watch inbox mark-read。
- 天气 HTTPS。
- SNTP 先不强行纳入 HTTPS gate；它不是 HTTPS 且影响 TLS 时间有效性，可后续只做启动错峰。

建议接口：

```c
typedef enum {
    BACKGROUND_HTTPS_GATE_REASON_MEMORY_WATCH_HEALTH,
    BACKGROUND_HTTPS_GATE_REASON_MEMORY_WATCH_SYNC,
    BACKGROUND_HTTPS_GATE_REASON_MEMORY_WATCH_INBOX,
    BACKGROUND_HTTPS_GATE_REASON_MEMORY_WATCH_MARK_READ,
    BACKGROUND_HTTPS_GATE_REASON_WEATHER,
} background_https_gate_reason_t;

esp_err_t background_https_gate_acquire(
    background_https_gate_reason_t reason,
    TickType_t wait_ticks);
void background_https_gate_release(background_https_gate_reason_t reason);
void background_https_gate_quiet_for(uint32_t ms, const char *reason);
bool background_https_gate_is_quiet(void);
```

约束：

- 只管后台请求；Hermes 前台 WebSocket 和用户主动语音上传不走这个 gate。
- token 拿不到时，owner 保持 pending 并稍后重试，不阻塞 UI。
- quiet window 期间直接拒绝或短等待，不创建新的 HTTPS client。
- 不宣称节省 20KB+ internal RAM；实际收益主要是减少并发峰值和 largest block 碎片窗口。

### 4. BLE quiet-window retry

当前普通 BLE presence 启动已有 internal heap guard。第一版只加“安静重试一次”：

```text
ble_presence_start() -> ESP_ERR_NO_MEM
  -> background_https_gate_quiet_for(...)
  -> foreground_runtime_gate_quiet_for(...)
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
- 复核 `watch-resource-arbitration-report.md` 与 `runtime-owner-contract.md` 是否需要同步补充“强前台独占 / ESP-DL 可抢占 / 后台 HTTPS gate”口径。
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

### 阶段 4：后台 HTTPS gate 接入

要做：

- Memory Watch 后台 health/sync/inbox/mark-read acquire/release `background_https_gate`。
- 天气 HTTPS acquire/release `background_https_gate`。
- token 拿不到时不阻塞 UI，按 owner 现有 retry 机制延后。

验收：

- source tests 覆盖后台 HTTP 调用必须 gated。
- Hermes 前台 WebSocket/语音上传不被 `background_https_gate` 阻塞。
- 天气和 inbox 可在 gate 忙时跳过/延后。

### 阶段 5：BLE quiet-window retry

要做：

- BLE presence 启动因 `ESP_ERR_NO_MEM` 失败时，打开 quiet window，等待短时间后重试一次。
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
- 天气/inbox/health 在后台运行。
- 如用户允许，再加 ESP-DL 安全检测高压场景。

验收：

- 无 panic/Guru/stack overflow。
- 无 `esp-aes: Failed to allocate memory`。
- BLE 失败路径可解释，成功路径正常。
- Hermes 前台优先，ESP-DL 可让路，后台 HTTPS 可延后。

## 进度

- `[x]` 阶段 0：计划落地。
- `[x]` 阶段 1：新增 `foreground_runtime_gate` 最小实现。
- `[x]` 阶段 2：ESP-DL 让路接入。
- `[x]` 阶段 3：Hermes 与未来前台页面接入。
- `[x]` 阶段 4：后台 HTTPS gate 接入。
- `[x]` 阶段 5：BLE quiet-window retry。
- `[ ]` 阶段 6：真机高压回归。（板端自动 gate/BLE fail-closed 回归已完成；公网 HTTPS 成功路径和 ESP-DL running -> 强前台让路仍待 Wi-Fi 可用后补测。）

## 决策记录

- 2026-06-29：确认资源模型采用“UI+Wi-Fi 基础常驻、强前台独占、ESP-DL 可抢占增强、后台 HTTPS 低优先级”。
- 2026-06-29：确认 ESP-DL 可以给 Hermes/BLE/OTA/未来前台页面让路，不作为同级强前台 owner。
- 2026-06-29：确认先做薄 gate，不做集中 job queue 或大 ResourceManager。
- 2026-06-29：确认后台 HTTPS gate 有价值，但只按“降低并发峰值和碎片窗口”定位，不夸大为根治 internal RAM。
- 2026-06-29：确认 Hermes 前台持有强前台 owner，但录音麦克风占用仍由 recorder 自己向 background manager 声明，避免重复 owner。
- 2026-06-29：确认后台 HTTPS gate 只覆盖低优先级 health/sync/inbox/mark-read/weather，不覆盖 Hermes 前台 WSS、语音上传、文本命令或 cancel。

## 意外与发现

- `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` 与 `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y` 已开启，TLS 大块缓冲倾向 PSRAM；但 `esp_http_client`、socket/LwIP、crypto DMA 和多个后台 task 同时活跃仍会制造 internal 峰值与 largest block 碎片窗口。
- Memory Watch HTTP client 当前显式使用 8KB RX + 8KB TX buffer，response 大 buffer 已优先 PSRAM；天气 HTTPS 仍是独立 `esp_http_client`，未进入任何后台错峰机制。
- BLE guard 需要的连续 internal block 远高于当前部分高压日志中的 largest block；gate 只能减少撞车概率，不能替代长期内存治理。
- 后台 HTTPS gate 接入后，天气、inbox、health 和 `/sync` 会在 gate 忙或 quiet window 中延后；这是预期行为，不能在 UI 层把单次跳过误判成服务永久失败。

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
- source tests 覆盖新增 gate 调用边界。
- `idf.py build` 通过。
- 真机高压场景无 panic/Guru/stack overflow。

当前实际结果：

- 阶段 0 文档已落地。
- 阶段 1 已新增 `main/services/foreground_runtime_gate.[ch]`、接入 `main/CMakeLists.txt`，并新增 `tests/test_foreground_runtime_gate_source.py` 锁住最小 API 与“非 ResourceManager”边界。
- 阶段 1 验证：`uv run python -m unittest tests.test_foreground_runtime_gate_source` 通过；`idf.py build` 通过，`111.bin` `0xabc140`，最小 app 分区剩余 `0x343ec0`（23%）。
- 阶段 2 已在 `background_service_manager` 增加 `FOREGROUND_RUNTIME` 阻塞原因：强前台 active 时 Safety Monitor 目标态为 false，ESP-DL 不启动或恢复；gate 不直接 suspend/delete ESP-DL task。验证：`uv run python -m unittest tests.test_safety_monitor_session_source tests.test_danger_detection_controller_source tests.test_foreground_runtime_gate_source` 通过。
- 阶段 3 已完成：`memory_watch_service_set_foreground_active(true/false)` 在 Hermes 前台进入/退出时 acquire/release `FOREGROUND_RUNTIME_OWNER_HERMES`，并通知 `background_service_manager_notify_foreground_runtime_changed()`；录音路径继续由 `memory_watch_recorder.c` 声明 foreground audio。
- 阶段 4 已完成：新增 `main/services/background_https_gate.[ch]`，接入 Memory Watch 后台 health、`/sync`、inbox poll、inbox mark-read 和天气 HTTPS；前台 voice/text/cancel 请求不走该 gate。
- 阶段 5 已完成：主界面 Bluetooth 显式点击路径在启动 BLE 前持有 `FOREGROUND_RUNTIME_OWNER_BLE_PROVISIONING`，打开后台 HTTPS quiet window；若 `network_manager_set_ble_enabled(true)` 返回 `ESP_ERR_NO_MEM`，短等待后只重试一次，仍失败则 fail closed + toast。
- 阶段 3-5 代码验证：`uv run python -m unittest tests.test_background_https_gate_source tests.test_foreground_runtime_gate_source tests.test_safety_monitor_session_source tests.test_danger_detection_controller_source tests.test_main_screen_ble_toggle_source tests.test_memory_watch_service_source tests.test_memory_watch_recorder_source tests.test_memory_watch_voice_client_source tests.test_memory_watch_ws_client_source tests.test_time_weather_source tests.test_network_manager_source tests.test_ble_presence_source` 通过，`Ran 75 tests ... OK`。
- 阶段 3-5 构建验证：`idf.py build` 通过，`111.bin` `0xabcc20`，最小 app 分区剩余 `0x3433e0`（23%）。
- 阶段 6 已新增默认关闭的板端自动高压测试入口：`runtime_resource_gate_board_test_start()` 只在 `CONFIG_RUNTIME_RESOURCE_GATE_BOARD_TEST=y` 时创建一次性测试 task，默认关闭时空返回。测试覆盖 Hermes foreground、background HTTPS busy/quiet、Memory Watch health/inbox、BLE foreground owner 和可选真实 BLE toggle。
- 阶段 6 自动 gate 压测 COM3 通过：`board_logs/2026-06-29-10-18-11-runtime-resource-gate-board-test-auto.log` 完整跑完，无 Guru、panic、stack overflow、NO_MEM；验证 `foreground acquired: owner=HERMES`、后台 HTTPS busy/quiet 拒绝、inbox poll quiet-window 拒绝、BLE owner acquire/release。
- 阶段 6 真实 BLE toggle 首轮发现并修复 cache-disabled 断言：真实 BLE toggle 会写 BLE NVS 偏好，NVS/flash 写入时 cache 可能关闭；若测试任务栈在 PSRAM，会触发 `esp_task_stack_is_sane_cache_disabled()` 断言。已改为 `CONFIG_RUNTIME_RESOURCE_GATE_BOARD_TEST_ENABLE_REAL_BLE_TOGGLE=y` 时使用 `MALLOC_CAP_INTERNAL` 栈。
- 阶段 6 真实 BLE toggle 修复后 COM3 结果：`board_logs/2026-06-29-10-31-09-runtime-resource-gate-board-test-real-ble-internal-stack.log` 未见 assert/Guru/panic/stack overflow；BLE guard 返回 `real_ble_enable: result=ESP_ERR_NO_MEM`，随后 `real_ble_disable: result=ESP_OK`，属于可解释 fail closed。结束内存约 `internal_free=43690 largest=20480 psram_free=6953672`。
- 阶段 6 当前限制：Wi-Fi 未连上，公网 HTTPS 成功路径未覆盖；Safety Monitor 当时包含 `user_disabled` 阻塞，未覆盖 ESP-DL 正在运行后被强前台暂停的完整路径。收尾时已恢复 `sdkconfig` 为 `# CONFIG_RUNTIME_RESOURCE_GATE_BOARD_TEST is not set`，`uv run python -m unittest tests.test_runtime_resource_gate_board_test_source tests.test_background_https_gate_source tests.test_foreground_runtime_gate_source tests.test_safety_monitor_session_source tests.test_main_screen_ble_toggle_source tests.test_memory_watch_service_source tests.test_time_weather_source` 通过（`Ran 39 tests ... OK`），`idf.py fullclean; idf.py build` 通过（`111.bin` `0xabcc80`，app free `0x343380`/23%），`app-flash-monitor` 45 秒通过且无 `runtime_gate_test` 自动压测日志。

## 幂等与恢复

- 如果中途中断，下次从“进度”中第一个未完成阶段继续。
- 如果 `foreground_runtime_gate` 引入行为回归，可先只保留 source-level API，不接入 Hermes/BLE/ESP-DL。
- 如果 `background_https_gate` 导致后台同步延迟过大，可缩小覆盖范围到 inbox/weather，保留 Hermes pending `/sync` 优先。
- 如果 BLE quiet-window retry 无收益，保留 fail closed 现状，撤回 retry 调用。

## 文档与收尾规则

本计划后续涉及 FreeRTOS、RAM/PSRAM、资源仲裁和底层并发。每完成一个代码 gate，在宣布完成前必须：

1. 新建 `docs/context/runs/YYYY-MM-DD-attempt-<feature>.md` 记录错误签名、证伪路径和验证证据。
2. 更新 `docs/context/CHANGELOG.md` 顶部摘要。
3. 按需更新本 active plan 的 `Progress / Validation and Acceptance / Next Step`，尤其是暂停、接手或状态反转时。
4. 执行：

```powershell
uv run python scripts/context/validate_context.py --level standard --q "watch runtime resource gate foreground background HTTPS ESP-DL BLE Hermes" --brief
```

## 下一步

- 下一步最小动作：阶段 6 补测公网 HTTPS 成功路径与 ESP-DL running -> 强前台让路。建议在 Wi-Fi 可用时 COM3 冷启动后依次观察 Hermes 前台/离页、Bluetooth 点击、天气/inbox/health 后台并发和 Safety Monitor/ESP-DL 让路日志。
