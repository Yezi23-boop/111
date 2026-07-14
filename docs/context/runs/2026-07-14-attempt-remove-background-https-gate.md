---
id: attempt-remove-background-https-gate-2026-07-14
tags: context, runs, attempt-log, resource-arbitration, background-https, freertos, hermes, weather, ble
summary: 撤除 background_https_gate 共享 token 与 quiet window，后台 HTTPS 恢复为各 owner 自治，同时保留 foreground gate、ESP-DL 让路和 BLE fail-closed。
last_reviewed: 2026-07-14
memory_type: episodic
scope: task
status: completed
owners: docs/context/knowledge/project/watch-resource-arbitration-report.md, docs/context/plans/active/2026-06-29-watch-runtime-resource-gate-plan.md, main/services/runtime_gate/foreground_runtime_gate.c
triggers: remove background https gate, https concurrency, quiet window, owner retry
evidence_level: observed
record_because: route-choice, evidence
---

# Attempt Log: 撤除 Background HTTPS Gate

## 目标

删除低优先级后台 HTTPS 的共享 binary semaphore 和 quiet window，不留下空 wrapper 或中央调度层。Memory Watch、weather 等 owner 继续负责自身请求、pending、重试和业务状态；强前台互斥和 ESP-DL 让路保持不变。

## 路线选择

撤除原因：

- gate 不预留 internal RAM，不能保证 BLE/TLS/task 创建成功。
- quiet window 不排空已经在途的 HTTPS。
- shared busy/quiet 状态曾导致 health/inbox 瞬时失败被误解释，需要额外修复。
- 后台请求频率、幂等和优先级不同，共享 token 没有替代各 owner 的调度职责。
- 当前没有证据要求 health/sync/inbox/weather 永久全局串行。

保留边界：

- `foreground_runtime_gate` 继续管理 Hermes、official_chat、BLE 等强前台互斥。
- `background_service_manager` 继续让 Safety Monitor / ESP-DL 在强前台期间让路。
- BLE `ESP_ERR_NO_MEM` 后继续只延迟重试一次，仍失败则 fail closed。
- Memory Watch health/inbox 的 pending、due 和瞬时错误恢复继续保留。

## 修改

- 删除 `main/services/runtime_gate/background_https_gate.[ch]` 及 CMake 接入。
- 删除 `tests/test_background_https_gate_source.py` 和 `tests/main_paths.py` 对应路径常量。
- Memory Watch health/alert/inbox/mark-read/sync 直接调用已有 HTTP helper。
- weather 直接创建和清理自己的 `esp_http_client`。
- official_chat 和 BLE UI 删除 background quiet window。
- runtime board test 删除 background busy/quiet 场景，保留 foreground/BLE 场景。
- 同步资源报告、目录地图、框架审查、active plan 和 CHANGELOG。

## 验证

- 聚焦 source tests：63 tests passed。
- 全量 source tests：420 passed、7 failed；失败项与目录整理交接记录一致，分别为 CO5300 queue depth、danger UI 坐标、Fall 模型契约、IMU service 依赖断言、official_chat 分区、resources 分区和 UI font seam，均未由本次改动触及。
- `idf.py fullclean; idf.py build`：通过；`111.bin=0xac5a60`，最小 app 分区余量 `0x33a5a0`（23%）。
- `git diff --check`：通过，仅工作树 CRLF 提示。
- context standard：已通过，`0 error / 0 warning`。
- 真机高压：待阶段 6 覆盖无 gate 的公网 HTTPS/WSS 自然并发。

## 风险与下一步

- health、sync、inbox、weather 现在可能并发创建 TLS/socket，启动或集中重试时可能恢复 internal RAM 峰值。
- official_chat/Hermes WSS 不再通过 quiet window 阻止新后台 HTTPS。
- 如果真机出现可重复 `esp-aes`、TLS、socket 或 task `NO_MEM`，先修具体 owner 的启动延迟、退避或 buffer 归属，不直接恢复全局 token。
