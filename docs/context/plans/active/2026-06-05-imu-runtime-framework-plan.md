---
id: plan-2026-06-05-imu-runtime-framework
tags: imu, qmi8658c, framework, board-facts, service, raw-motion, raise-wrist
summary: 只定 IMU 运行框架：driver/board/service/algorithm 边界、事件数据流、日志与后续识别扩展点；不在本计划内解决抬腕识别准确率。
last_reviewed: 2026-06-05
memory_type: task
scope: imu
status: active
owners: components/qmi8658c, main/app/board_imu.c, main/services/imu_service.c, components/imu_motion
triggers: IMU framework, imu runtime, qmi8658c, board_imu, imu_service, imu_motion, raw_motion, raise-wrist framework
evidence_level: design
---

# IMU 运行框架计划

## Purpose / Big Picture

- 任务目标：先把 IMU 的嵌入式框架定清楚，保证后续换板、换中断线、换算法时不会把板级事实、芯片协议、运行时服务和识别规则混在一起。
- 为什么现在做：当前 QMI8658C 已能通过 20 ms WoM fallback 采集 16 帧原始 accel/gyro 窗口，但真实抬腕识别还未定型；先固定框架，避免后续调规则时顺手破坏 owner 边界。
- 完成后用户会看到什么变化：后续任务会默认沿用 `qmi8658c -> board_imu -> imu_service -> imu_motion`，识别问题只替换 `imu_motion` 规则或增加离线评估工具，不需要重写底层采样链路。

## Scope / Non-Goals

- 本轮明确要做：定义 IMU driver、board facts、service owner、algorithm module 的职责边界；定义 WoM 触发、原始窗口、结果发布和 CSV 日志这条数据流；定义后续调参/采样/评估的入口。
- 本轮明确不做：不解决真实抬腕识别准确率；不区分左右手；不启用 QMI8658C AE/MoD/dQ 或内部四元数作为产品依赖；不自动合成负样本；不承诺阈值、召回率或误触率。

## Owner Boundaries

- `components/qmi8658c`：只做芯片协议 owner，包括 I2C register 读写、WHO_AM_I/revision probe、raw accel/gyro 读取、WoM/status 配置和最小错误码；不得持有板级 GPIO、安装方向、产品阈值或 FreeRTOS 任务生命周期。
- `main/app/board_imu.c`：只做板级事实 owner，包括 I2C 地址、INT1/INT2/GPIO 事实、安装方向/轴映射、默认采样窗口和可用 fallback；不得实现抬腕识别规则，也不得直接推进长期运行状态。
- `main/services/imu_service.c`：做长期运行 owner，用 FreeRTOS task/notification/queue 类原语承接事件、采样窗口、snapshot、日志和对外结果发布；不得把寄存器细节或板级常量硬编码进 service。
- `components/imu_motion`：做纯算法接口 owner，输入标准化 motion window，输出 `raise_detected / reject_reason / debug metrics`；不得访问 I2C、GPIO、FreeRTOS 或 board 配置。

## Runtime Data Flow

```text
QMI8658C WoM/status/raw data
  -> board_imu board facts and axis mapping
  -> imu_service owner task
  -> 16-frame raw accel/gyro motion window
  -> imu_motion rule interface
  -> imu_service snapshot, raise_result, CSV/debug log
```

- 当前硬件事实：`QMI_INT1 -> GPIO21` 在样板上已按板测归为物理开路/浮空风险，产品路径先使用 20 ms `STATUS1.WoM` fallback 轮询；若后续硬件修复 INT1，只应改 `board_imu` 配置和 `imu_service` 事件入口，不应改算法接口。
- 当前数据窗口：WoM 事件后采 16 帧 accel/gyro；帧间隔、坐标映射、触发源、最终姿态和 reject reason 必须可日志化，便于后续人工样本和离线 replay。
- 当前结果语义：`raise_result` 表示框架产生了一次规则输出，不等于产品级抬腕识别已完成。

## Framework Contract

- 每个 motion event 至少保留：event id、trigger source、sample interval、frame count、accel/gyro 原始或标准化值、final accel norm、stability 指标、`raise_detected`、`reject_reason`。
- CSV 样本日志是后续调规则的接口，不是最终产品 UI；字段要稳定，允许后续增加列，但不要改已有列含义。
- `imu_service` getter 只能读 snapshot，不做 I2C、阻塞等待或状态推进；真实采样和错误恢复必须留在 owner task。
- `imu_motion` 第一版可以是占位/简单规则，但必须保持输入输出接口稳定，让后续 dQ、自研姿态、统计规则或轻量 ML 都能替换进去。

## Progress

- `[x]` 已有 QMI8658C 最小 driver、本地组件和 source tests。
- `[x]` 已有 `board_imu` 承接板级 I2C/GPIO/采样事实。
- `[x]` 已有 `imu_service` 承接 20 ms WoM fallback、16 帧窗口和日志发布。
- `[x]` 已有 `imu_motion` 纯规则模块作为算法替换点。
- `[ ]` 后续补离线 replay/evaluator，把真实样本与规则阈值调参从固件循环中拆出来。
- `[ ]` 后续讨论并实现真实抬腕识别策略。

## Decision Log

- 2026-06-05：用户确认当前项目只先搭 IMU 框架，后续再讨论识别问题。
- 2026-06-05：第一版不区分左右手，不要求佩戴检测；桌面拿起并转向自己可接受为抬腕类动作。
- 2026-06-05：由于 QMI8658C 内部 AE/MoD/dQ 在当前板和公开资料之间仍有未闭环差异，本框架不把内部四元数或 dQ 作为产品依赖；后续可作为独立 diagnostics 或实验分支。

## Validation and Acceptance

- source test：锁定 service 不硬编码板级 GPIO/I2C 地址，driver 不消费 board facts，`imu_motion` 不访问硬件/FreeRTOS。
- board evidence：日志中能看到 `wom_event`、motion window、CSV 样本、final pose/debug metrics、`raise_result` 或 `reject_reason`。
- context validation：本文档变更至少运行 `uv run python scripts/context/validate_context.py --level standard --q "IMU runtime framework QMI8658C board_imu imu_service imu_motion" --brief`。
- build rule：若只改本文档，不要求 `idf.py build`；若后续改 `components` 或 `main` 代码，必须跑相关 source tests 和 `idf.py build`，普通 app 改动用 `idf.py -p COM3 app-flash` 验证。

## Idempotence and Recovery

- 如果中途中断，下次从本文的 Owner Boundaries 和 Runtime Data Flow 继续，不要从当前代码现状反推新框架。
- 如果后续识别策略失败，最小回退路径是保留 `qmi8658c/board_imu/imu_service` 采样链路，只替换或关闭 `imu_motion` 规则输出。

## Next Step

- 下一步最小动作：补一个离线 replay/evaluator 框架，把 COM3 采集的正/负样本 CSV 喂给 `imu_motion` 规则，输出阈值统计和 reject reason 分布。
