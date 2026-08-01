---
id: attempt-runtime-coordinator-board-handoffs
tags: run, runtime-coordinator, foreground-owner, ble, hermes, ota, safety-monitor, com7
summary: Runtime Coordinator 模拟状态机、真实 Hermes/OTA 交接、BLE ACK 竞态修复与默认固件冷启动证据。
memory_type: run
scope: repo
evidence_level: observed
status: completed
last_reviewed: 2026-07-31
---

# Attempt Log: Runtime Coordinator 板端交接

## 结果

- coordinator 模拟 participant 在 COM7 完成请求覆盖、陈旧 ACK、owner 拒绝、当前 owner 超时、后台超时、grant 失败、回滚超时和 degraded late recovery。
- 真实 Hermes 获得强前台后进入 `ACTIVE`，离页后回到 `IDLE`；真实 OTA prepare 获得强前台并进入 `READY/ACTIVE`，取消后退出 maintenance 并回到 `IDLE`。
- BLE presence 在板上保存偏好为关闭，quiesce adapter 立即 ACK “已停止”，系统回到 `IDLE` 后按偏好 reevaluate，没有错误开启 BLE。
- 恢复默认 `sdkconfig` 后执行 `fullclean + build + app-flash-monitor`，冷启动到 `startup_sequence_done`、`SERVICE_READY` 和资源快照，无 `runtime_coord_test`、panic、Guru、WDT 或 stack overflow。

## 可复用错误签名

首轮真实交接曾出现 coordinator 等待 BLE presence ACK，但 BLE worker 已发布完成并被 owner 提前回收，导致 ACK 没有进入 coordinator。根因是 worker completion 可见性早于 coordinator ACK，且 worker handle 发布存在启动竞态。

修复边界：

- worker 必须先向 coordinator 报告 quiesce 结果，再发布 owner completion/recovery 信号。
- 创建 worker 后使用 task notification barrier，保证 handle 发布完成后 worker 才执行。
- BLE 已经关闭时立即 ACK，不创建无意义 transition，同时保留用户 BLE 偏好供 `request_reevaluate` 使用。
- quiesce generation 与 snapshot 同受 critical section 保护，旧 worker 不能覆盖新事务。

后续若再次看到 `waiting_mask` 长时间只剩 BLE participant，应先检查 ACK 与 worker completion 的发布顺序，不要通过放宽超时或伪造 stopped 绕过。

## 环境证据

真实 Hermes 交接中 `watch.934000.xyz:443` 预连接出现 TLS select timeout，owner 随后按失败路径完成 release，coordinator 正常回到 `IDLE`。该错误说明远端连接当时不可用，不是 coordinator 超时或资源泄漏。

本轮 OTA 只验证 prepare/cancel 和 maintenance 生命周期，没有执行下载、VERIFYING、RESTARTING 或写备用槽；OTA 不可中断阶段仍由 OTA adapter 的 source contract 覆盖，不能把本轮结果表述为完整 OTA 掉电回归。

## 验证证据

- 聚焦 source tests：`64 passed`。
- 全量 pytest：`453 passed, 1 failed, 1 warning`；唯一失败属于任务外已有 `danger_sample_recorder` reset-session 漂移。
- 默认配置 ESP-IDF build：`111.bin=0xab56b0`，app 分区余量 11%。
- context standard：错误 0、警告 0；`git diff --check` 无空白错误。
- 模拟与真实 owner：`board_logs/2026-07-31-18-19-46-runtime-coordinator-real-owner-handoffs.log`。
- 默认固件冷启动：`board_logs/2026-07-31-18-39-14-runtime-coordinator-normal-cold-boot-retry.log`。
