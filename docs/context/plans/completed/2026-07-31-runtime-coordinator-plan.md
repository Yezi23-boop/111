---
id: runtime-coordinator-plan
tags: context, plans, runtime-coordinator, resource-arbitration, foreground-owner, freertos
summary: 将四个重复的强前台 gate/quiesce 流程收敛为注册式 Runtime Coordinator；协调器只拥有协议、代次、超时和 ACK，不拥有业务资源。
last_reviewed: 2026-07-31
memory_type: task
scope: task
owners: docs/context/plans/completed/2026-07-31-runtime-coordinator-plan.md, main/services/runtime/runtime_coordinator.c, main/services/runtime/safety_monitor_policy.c
triggers: runtime coordinator, participant, foreground handover, quiesce ack, resource arbitration
evidence_level: observed
status: completed
---

# Runtime Coordinator 执行计划

## 目标与全局

- 将 Hermes、official chat、network provisioning、OTA 重复实现的 `request -> wait -> acquire -> release` 收敛为统一异步协议。
- coordinator 只创建自己的 FreeRTOS task、静态 queue、participant 表和快照；不创建、停止或销毁业务资源。
- 强前台 owner 和真正争抢关键资源的可抢占后台能力通过注册接入；普通 weather、health、inbox、sync 保持 owner 自治。

## 固定设计

- coordinator task 使用 4096B internal RAM stack；所有请求、ACK、取消和超时由同一 command queue 串行处理。
- participant 表固定 8 项，不支持运行期卸载；回调只能向各自 owner task 投递命令。
- 强前台：Hermes、official chat、BLE/SoftAP provisioning、OTA。
- 可抢占后台：Safety Monitor、普通 BLE presence。
- 使用独立 `request_generation` 与 `transition_generation`；新请求覆盖旧目标时保留已发出的排空事务。
- owner 拒绝或超时均 fail closed；coordinator 不强杀 task，不解释 OTA 等业务状态枚举。
- OTA 是否可中断由 OTA adapter 自己判断；coordinator 只消费接受、拒绝和完成 ACK。

## 进度

- [x] 完成现状核实和详细设计评审，确认新增抽象满足两个以上真实调用方重复生命周期的门槛。
- [x] 建立 active plan，并改写 runtime owner/资源仲裁合同边界。
- [x] 实现 coordinator task、静态 queue、participant 注册表、状态机和只读快照。
- [x] 将 `background_service_manager` 收敛并重命名为 Safety Monitor policy owner。
- [x] 接入 Hermes、official chat、network provisioning、BLE presence、OTA participant。
- [x] 删除旧 `foreground_runtime_gate` 和旧 quiesce API，更新 board test/source tests。
- [x] 完成聚焦/全量测试、ESP-IDF build、context 校验和 COM7 回归。

## 本轮闭环（2026-07-31）

- `runtime_coordinator` 已成为唯一跨 owner 的强前台协议事实源；旧 `foreground_runtime_gate`、旧 quiesce API 和 `background_service_manager` 运行时协调职责已删除或迁移。
- 固定 8 项 participant 表已接入 Hermes、official chat、network provisioning、OTA、Safety Monitor 和 BLE presence；普通 weather、health、inbox、sync 保持 owner 自治。
- 模拟 participant 序列已覆盖最新请求覆盖、有效旧 ACK、陈旧 ACK、owner 拒绝、当前 owner 5 秒超时、后台 2.5 秒超时、grant 失败、回滚超时、Safety/BLE 偏好恢复和 OTA adapter 边界。
- COM7 真实交接已覆盖 Hermes 与 OTA 的 `ACTIVE -> release -> IDLE`；默认配置冷启动已通过，未启动板测 task，无 panic/WDT。

## 验证结果

- 聚焦 source tests：`64 passed`。
- ESP-IDF：恢复默认 `sdkconfig` 后执行 `idf.py fullclean; idf.py build` 通过，`111.bin=0xab56b0`，app 分区余量 11%。
- COM7 测试镜像：`board_logs/2026-07-31-18-19-46-runtime-coordinator-real-owner-handoffs.log`，模拟与真实 Hermes/OTA 交接完成；Hermes WSS TLS timeout 为端点网络环境证据，未阻断 coordinator release。
- COM7 默认镜像：`board_logs/2026-07-31-18-39-14-runtime-coordinator-normal-cold-boot-retry.log`，`startup_sequence_done`、`SERVICE_READY` 和冷启动资源快照均出现，`panic_log_seen=false`，无 `runtime_coord_test`。
- 全量 pytest 仍保留一个与本计划无关的既有 `danger_sample_recorder` reset-session 失败；其余结果为 `453 passed, 1 failed, 1 warning`。
- context standard：检查 235 个文件，错误 0、警告 0；`git diff --check` 无空白错误，仅已有 CRLF/LF 提示。

## 状态与恢复

```text
IDLE -> QUIESCING -> GRANTING -> ACTIVE -> IDLE
                     |             |
                     +-> ROLLING_BACK -> IDLE / DEGRADED
```

- 当前 owner 拒绝：新请求失败，旧 owner 保持 ACTIVE。
- 当前 owner 停止超时：进入 DEGRADED_CURRENT，保留旧 owner 席位，等待 late ACK 或 active report。
- 后台 quiesce 超时：不授予新 owner；无旧强前台时恢复可抢占后台。
- grant/start 超时：请求 provisional owner 回滚停止；回滚超时进入 DEGRADED_TARGET。
- 被覆盖的旧强前台不自动恢复；可抢占后台在系统回到 IDLE 后按自身用户意图和策略重新评估。

## 验证与验收

- source tests 锁定 coordinator 不包含业务 service 头文件、各 owner 不再直接调用旧 gate/quiesce API。
- 板测覆盖请求覆盖、陈旧 ACK、拒绝、停止超时、grant 失败、回滚和背景恢复。
- 执行 `uv run python -m pytest tests -q`、`idf.py build`、context standard 和 `git diff --check`。
- COM7 覆盖正常冷启动、Safety Monitor -> 强前台让路、Hermes/official chat/network/OTA 交接和 OTA 不可中断拒绝。

## 幂等与恢复

- 中断后从第一个未勾选进度继续；不得同时保留旧 gate 与 coordinator 两套 owner 真相。
- owner 迁移未全部完成前不声明代码闭环；构建失败时保留文档和未接线 coordinator 核心，不回退用户已有 OTA/目录整理改动。

## 下一步

- 本计划已完成并归档。后续新增关键资源 participant 时，先按 `runtime-owner-contract.md` 注册合同和固定容量评估，再补对应 source/board evidence。
