---
id: attempt-memory-watch-background-https-retry
tags: context, runs, attempt-log, ai-memory-watch, hermes, background-https-gate, freertos, retry, esp32s3
summary: 修复 AI Memory Watch 后台 health/inbox 在 background HTTPS gate 忙时连续失败、紧循环重派和 Hermes 在线状态抖动的问题。
created: 2026-07-08
last_reviewed: 2026-07-08
owners: main/services/memory_watch_service.c, main/services/background_https_gate.c, docs/context/plans/active/2026-06-29-watch-runtime-resource-gate-plan.md
evidence_level: observed
status: completed
---

# Attempt Log: Memory Watch 后台 HTTPS 延迟重试

## 背景

真机日志显示 Memory Watch 后台 `inbox`、`health` 与 `/sync` 在同一启动窗口争抢 `background_https_gate`：

```text
bg_https_gate: background https busy: reason=memory_watch_inbox
memory_watch_http: inbox poll failed: status=0 err=ESP_ERR_TIMEOUT
bg_https_gate: background https busy: reason=memory_watch_health
memory_watch_http: health check failed: status=0 err=ESP_ERR_TIMEOUT
memory_watch: watch endpoint health result: hermes_online=0 err=ESP_ERR_TIMEOUT
memory_watch: inbox: poll failed, will retry
memory_watch: inbox: poll job dispatched
```

这里的核心问题不是请求应该被“跳过”，而是 owner 在 gate 忙或瞬时失败时应保留 pending 并延迟重试，避免紧循环重派、日志刷屏和 Hermes 在线状态误判。

## 改动

- `memory_watch_service` 增加后台 HTTPS 退避间隔 `kBackgroundHttpsRetryIntervalMs=5000`。
- health check 增加 `s_health_worker_busy`、`s_health_check_pending` 和 `s_health_retry_next_due_ms`：
  - worker 忙或 dispatch 失败时保留 pending。
  - transient failure 不再立刻把 `hermes_online` 改成 false。
  - 认证/协议错误仍允许进入错误态，避免隐藏真实配置问题。
- inbox poll 增加 `s_inbox_poll_next_due_ms`：
  - worker 忙或失败时保留 pending。
  - 失败后按 due 时间重试，不再立即连续重派。
  - auth/protocol error 会清 pending 并暂停，避免认证错误紧循环。
- source test 锁住“后台 HTTPS 失败不丢 pending、health transient failure 保持上次在线状态”的行为。

## 验证

```powershell
uv run python -m unittest tests.test_memory_watch_service_source -q
uv run python -m unittest tests.test_memory_watch_service_source tests.test_background_https_gate_source tests.test_runtime_resource_gate_board_test_source -q
git diff --check -- main/services/memory_watch_service.c tests/test_memory_watch_service_source.py
. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build
```

结果：

- `tests.test_memory_watch_service_source`：20 passed。
- 相关 gate/source tests：27 passed。
- `git diff --check`：无 whitespace error，仅 LF/CRLF 提示。
- `idf.py build`：通过，`111.bin` `0xace350`，最小 app 分区剩余 `0x331cb0`（23%）。

## 后续观察

- 需要真机串口确认失败日志从紧循环变成：
  - `health check deferred: ... retry_in_ms=5000`
  - `watch endpoint health transient failure: keep_online=...`
  - `inbox: poll failed, will retry in 5000 ms`
- 前台 WebSocket `connect failed err=-1` 是另一路前台连接问题，本次只修后台 HTTPS gate 重试与在线状态抖动，不把两个问题混为一个闭环。
