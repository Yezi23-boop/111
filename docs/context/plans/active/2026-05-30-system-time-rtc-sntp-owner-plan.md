---
id: system-time-rtc-sntp-owner-plan-20260530
tags: plan, active, watch, rtc, pcf85063atl, sntp, system-time, official-chat
summary: 统一系统时间 owner 计划，固定 PCF85063ATL RTC、SNTP、official_chat TLS 前授时和旧 get_time 迁移的分层边界与执行步骤。
status: active
last_reviewed: 2026-05-30
memory_type: project_plan
scope: repo
owners: components/pcf85063atl, components/system_time, main/services/system_time_service.c, main/services/official_chat_service.c, components/official_chat, main/features/weather/time_weather.c
triggers: system_time, system_time_service, pcf85063atl, sntp, rtc, get_time, official_chat, tls, 时间同步, 授时
evidence_level: design
---

# 统一系统时间与 RTC/SNTP 执行计划

## Purpose / Big Picture

- 目标：把系统时间统一收敛到一个 owner，避免 `official_chat`、天气任务、旧 `get_time` 和 RTC 驱动各自启动 SNTP 或维护时间缓存。
- 目标态：
  - `PCF85063ATL` 只做 RTC 寄存器驱动。
  - `components/system_time` 做可复用时间核心，内部封装 ESP-IDF SNTP、RTC 读写和 `settimeofday()`。
  - `main/services/system_time_service` 做应用启动编排、网络 ready 后触发同步和快照日志。
  - `official_chat` 在 HTTPS/TLS 前只通过回调索要有效系统时间，不直接调用 `esp_sntp_init()` 或 `esp_sntp_restart()`。
  - 旧 `components/get_time` 删除，避免形成第二套时间 owner。

## Source Context

- `docs/context/knowledge/project/runtime-owner-contract.md`
- `docs/context/knowledge/project/official-chat-ota-tls-time-bootstrap.md`
- `docs/context/knowledge/project/rtc-pmic-wakeup-evidence-loop.md`
- `docs/context/knowledge/esp32-s3/pcf85063atl-minimal-probe.md`
- `C:/Users/ye/.agents/skills/embedded-framework-mentor/references/current-project-boundaries.md`

## Owner Boundary

- `components/pcf85063atl`
  - 负责：I2C 地址 `0x51`、RTC 时间寄存器读写、OS/AF/TF 状态读取、清 OS、倒计时 timer 证据接口。
  - 不负责：SNTP、TLS、系统时间策略、`settimeofday()`、UI 展示。
- `components/system_time`
  - 负责：判断系统时间是否可信、从 RTC bootstrap 系统时间、启动/重启 SNTP、SNTP 成功后写 RTC、提供本地时间格式化读取。
  - 不负责：启动顺序、网络 ready 轮询、UI 状态展示、official_chat 生命周期。
- `main/services/system_time_service`
  - 负责：应用层启动编排、网络 ready 后触发 SNTP 同步、保存快照、输出关键日志。
  - 不负责：RTC 寄存器细节、official_chat 内部 HTTP 逻辑、天气 UI 刷新。
- `components/official_chat`
  - 负责：在 HTTPS/TLS 请求前调用外部注入的 `ensure_time_valid` 回调。
  - 不负责：直接启动 SNTP、直接访问 RTC、直接依赖 `main/services`。
- `main/features/weather/time_weather.c`
  - 负责：读取当前本地时间快照并刷新 UI。
  - 不负责：阻塞等待 SNTP、维护全局 `now_time`。

## Scope / Non-Goals

本轮要做：

- 新增 `pcf85063atl_set_time()`、`pcf85063atl_clear_oscillator_stopped()`、`pcf85063atl_read_status()`。
- 新增 `components/system_time`。
- 新增 `main/services/system_time_service`。
- 修改 `official_chat` 为回调索要有效时间。
- 删除 `components/get_time` 及其 CMake 依赖。
- 迁移 `time_weather.c` 的旧时间读取。
- 增加 source tests 和构建验证。

本轮不做：

- 不做 UI 时间设置页。
- 不做时区配置页，默认沿用当前项目 CST-8 口径。
- 不做 RTC alarm API。
- 不做 Light Sleep / Deep Sleep。
- 不引入可插拔 SNTP backend 抽象；ESP-IDF SNTP 只封装在 `components/system_time` 内部。

## Target Behavior

### 冷启动 RTC 有效

```text
app_main
  -> system_time_service_start()
  -> system_time_bootstrap_from_rtc()
  -> PCF85063ATL os=0 且字段合法
  -> settimeofday()
  -> source=RTC
```

### 冷启动 RTC 停振

```text
app_main
  -> system_time_service_start()
  -> system_time_bootstrap_from_rtc()
  -> PCF85063ATL os=1
  -> 不设置系统时间
  -> 快照 rtc_oscillator_stopped=1
```

### 网络 ready 后校时

```text
network_service 进入 SERVICE_READY
  -> system_time_service_note_network_ready()
  -> system_time_sync_sntp_and_write_rtc()
  -> SNTP 成功
  -> settimeofday()
  -> pcf85063atl_set_time()
  -> RTC OS 清除
  -> source=SNTP
```

### official_chat HTTPS 前

```text
official_chat Ota::PerformJsonRequest()
  -> HTTPS URL
  -> 调 ensure_time_valid(timeout_ms, ctx)
  -> official_chat_service adapter
  -> system_time_service_ensure_valid_for_tls()
  -> 时间有效后继续 HTTPS
```

### 天气/时钟 UI

```text
time_weather loop
  -> system_time_service_get_local_time()
  -> 有效则刷新数字时钟
  -> 无效则跳过本次刷新
```

## Implementation Gates

### Gate 1: RTC 驱动 API

- `[x]` 在 `pcf85063atl.h` 增加 `pcf85063atl_status_t`。
- `[x]` 实现 `pcf85063atl_set_time()`：
  - 校验秒/分/时/日/月/星期/年范围。
  - BCD 编码。
  - 从 `SECONDS(0x04)` 连续写 7 字节。
  - 秒寄存器 bit7 OS 写 0。
- `[x]` 实现 `pcf85063atl_clear_oscillator_stopped()`：
  - 只读写 seconds 寄存器。
  - 保留低 7 位，清 bit7 OS。
- `[x]` 实现 `pcf85063atl_read_status()`：
  - 读取 seconds raw 和 Control_2。
  - 暴露 `oscillator_stopped / alarm_flag / timer_flag / raw`。

### Gate 2: system_time 核心组件

- `[x]` 新增 `components/system_time`。
- `[x]` 提供 `system_time_init()`、`system_time_bootstrap_from_rtc()`、`system_time_sync_sntp_and_write_rtc()`、`system_time_ensure_valid_for_tls()`、`system_time_get_snapshot()`、`system_time_get_local_time()`。
- `[x]` ESP-IDF SNTP 调用只允许出现在该组件内。
- `[x]` SNTP 成功后才写 RTC；SNTP 失败不清 OS。

### Gate 3: 应用层 system_time_service

- `[x]` 新增 `main/services/system_time_service.h/.c`。
- `[x]` `app_main()` 在 core policy 附近启动 service。
- `[x]` `network_service` 进入 `SERVICE_READY` 时通知 `system_time_service`。
- `[x]` getter 只返回快照，不做 I2C/SNTP。

### Gate 4: official_chat 回调迁移

- `[x]` `official_chat_config_t` 增加 `ensure_time_valid` 回调和 `user_ctx`。
- `[x]` `Ota` 保存回调，HTTPS 前调用回调。
- `[x]` 删除 `official_chat` 内部 `esp_sntp_init()` / `esp_sntp_restart()` 逻辑。
- `[x]` `official_chat_service` 创建 config 时注入 `system_time_service_ensure_valid_for_tls()` adapter。

### Gate 5: 删除 get_time 与迁移 weather

- `[x]` 删除 `components/get_time`。
- `[x]` `main/CMakeLists.txt` 删除 `get_time` 依赖，增加 `system_time_service.c`。
- `[x]` `time_weather.c` 改用 `system_time_service_get_local_time()`。
- `[x]` 仓库不再引用 `get_time.h`、`now_time`、`update_now_time()`。

### Gate 6: 测试、构建、上下文闭环

- `[x]` 增加 source tests 锁定 owner 边界和关键 API。
- `[ ]` `uv run python -m unittest discover tests -v`。
  - 当前结果：本轮新增/相关 tests 通过；全量 discover 剩余 3 个失败点均在既有 AP Portal source tests 的旧预期，与本轮 system_time/RTC/SNTP owner 迁移无直接关系。
- `[x]` `uv run python scripts/context/validate_context.py --level standard --q "system_time_service PCF85063ATL SNTP RTC official_chat get_time" --brief`。
- `[x]` 确认 `export.ps1` 可用后执行 `idf.py build`。

## Expected Logs

```text
system_time: rtc bootstrap skipped: oscillator_stopped=1
system_time: SNTP started server0=ntp1.aliyun.com
system_time: sntp sync ok source=SNTP
system_time: rtc writeback ok os_cleared=1
official_ota: time ensure callback ok before HTTPS
```

## Risks

- `official_chat` 是 component，不能反向依赖 `main/services`；必须用回调注入。
- `time_weather` 当前正式入口未启用，但仍编译；迁移时不能留下未解析符号。
- USB 串口环境下不能把 Light Sleep 作为本轮验收依据。
- 板端 `os=1` 只有在 SNTP 成功写回 RTC 后才应清除，不能为了日志好看手动清。

## Execution Result

- 已完成 RTC 驱动 API、`system_time` 核心组件、应用层 `system_time_service`、`official_chat` 回调迁移、天气时间读取迁移和旧 `get_time` 删除。
- `official_chat` component 保持不反向依赖 `main/services`，由 `official_chat_service` 注入时间回调。
- `settimeofday()` 与 ESP-IDF SNTP 调用集中在 `components/system_time`，上层只通过 service 或回调使用。
- 已通过定向 source tests、context standard 和 `idf.py build`。
- 全量 unittest discover 当前剩余失败：
  - `test_adapter_reuses_same_httpd_handle_for_softap_provisioning`
  - `test_routes_expose_status_scan_and_configure_endpoints`
  - `test_ap_portal_app_uses_provisioning_client_entrypoint`
- 已完成板端 SNTP 写 RTC 与物理断电重启闭环，证据见 `docs/context/runs/2026-06-01-attempt-system-time-rtc-power-cycle-validation.md`。

## Progress

- `[x]` RTC driver API、`components/system_time`、`system_time_service` 和 `official_chat` 时间回调迁移完成。
- `[x]` 旧 `components/get_time` 已删除，天气时间读取已迁移。
- `[x]` 定向 source tests、context standard、`idf.py build` 和板端 RTC/SNTP/断电保持证据已完成。
- `[ ]` 全量 unittest discover 仍保留 AP Portal 旧预期失败，和本计划主线无直接关系。

## Decision Log

- 决策：ESP-IDF SNTP 与 `settimeofday()` 调用集中到 `components/system_time`。
- 原因：避免 `official_chat`、天气和旧 `get_time` 各自成为时间 owner。
- 决策：`official_chat` 通过回调索要有效时间，不反向依赖 `main/services`。
- 原因：保持 component 边界，避免 TLS 前授时逻辑穿透层级。

## Validation and Acceptance

- 定向 source tests 通过。
- `uv run python scripts/context/validate_context.py --level standard --q "system_time_service PCF85063ATL SNTP RTC official_chat get_time" --brief` 通过。
- `idf.py build` 通过。
- 板端证据见 `docs/context/runs/2026-06-01-attempt-system-time-rtc-power-cycle-validation.md`。

## Idempotence and Recovery

- 若后续中断，从 `system_time_service` 快照、`official_chat` 时间回调和天气读取入口三处检查 owner 边界。
- 若 SNTP 或 RTC 写回回归，优先保留 `components/system_time` owner，不恢复旧 `get_time`。

## Next Step

- 后续若要提升 RTC 走时质量，单独做长时间 drift 观测，不在本计划内继续扩展 owner。
