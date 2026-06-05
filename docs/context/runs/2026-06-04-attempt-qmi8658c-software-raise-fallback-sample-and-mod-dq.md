---
id: attempt-2026-06-04-qmi8658c-software-raise-fallback-sample-and-mod-dq
tags: qmi8658c, imu, raise-wrist, wom, revision-a, raw-motion, board-test
summary: QMI8658C MoD/sDA 根因定位与 Rev A 原始六轴软件抬腕降级；结果：success。
last_reviewed: 2026-06-04
memory_type: episodic
scope: task
status: completed
result: success
owners: components/qmi8658c: Rev A 寄存器协议, main/app/board_imu.c/h: 原始运动窗口板级配置, main/services/imu_service.c: WoM 后原始六轴软件规则与结果发布
triggers: QMI8658C STATUS0 sDA, CTRL9 0x0C, Motion on Demand, Rev A raw motion
evidence_level: observed
record_reasons: evidence, handoff
force_reason:
---

# Attempt Log: QMI8658C MoD/sDA 根因与 Rev A 原始六轴降级

## 背景

- 本次要验证什么：解释为什么 `CTRL9 REQ_MoD(0x0C)` 请求完成后 `STATUS0.sDA` 一直不上来，并恢复可验证的软件抬腕采样路径
- 对应任务或计划：未绑定计划
- 结果状态：success
- 长期记录理由：evidence, handoff

## 环境

- 分支/工作区状态：codex/qmi8658c-test-program
- 设备/串口/板型：ESP32-S3-Touch-AMOLED-2.06 COM3
- 关键前置条件：板上 `WHO_AM_I=0x05`、`revision_id=0x7c`；QMI INT1(GPIO21) 物理通路不可用，正式 IMU service 已降级到 20 ms `STATUS1.WoM` 轮询

## 操作

- 修改过的文件或 owner：
- `components/qmi8658c`：移除来自旧 Rev0.6 资料、但不属于当前 Rev A 寄存器图的 AE/MoD/dQ 接口；保留 Rev A 原始六轴、WoM 和 CTRL9 握手
- `main/app/board_imu.c/h`：板级配置改为 16 帧、40 ms 周期的原始六轴运动窗口
- `main/services/imu_service.c`：改为 `WoM -> raw 6-axis -> imu_motion -> final pose -> raise_result`
- `tests/test_qmi8658c_source.py`：锁定 Rev A `CTRL9 0x0C` 是 Tap 配置命令，不得再次当作 Motion-on-Demand
- 执行的命令或动作：
- 对照 QST 官方 QMI8658C Rev A 数据手册与当前板 `revision_id=0x7c`
- `uv run python -m unittest tests.test_qmi8658c_source tests.test_imu_service_source tests.test_imu_motion_source tests.test_extract_imu_raise_samples_script`
- 注：当时还包含临时 `tests.test_qmi8658c_diagnostic_source`，该独立 diagnostic 工程已在合并前清理，不作为主线回归入口。
- `idf.py fullclean`、`idf.py build`、`idf.py -p COM3 app-flash`
- COM3 限时监控 90 秒
- 已尝试但不应直接重复的路径：
- 不要基于当前 ODR AE 的 total_mdeg=0 下调 raise_min_rotation_mdeg
- 不要把 WoM 下读到的 raw_gyro 当成触发瞬间真实角速度，WoM 模式下 gyro 本来关闭
- 不要在当前 Rev A 芯片上等待旧 Rev0.6 定义的 `STATUS0.sDA`，也不要把 `CTRL9 0x0C` 当作 MoD 请求

## 观测

- 关键日志/证据：
- board_logs/2026-06-04-21-33-57-imu-raise-sample-v1.log: 12 events, 192 frames, ready dQ vector all zero
- board_logs/2026-06-04-21-45-48-imu-raise-sample-v2.log: 25 events, 400 frames, STATUS0 0x08 ready frames 342, nonzero ready dQ 0
- board_logs/2026-06-04-21-54-38-imu-raise-sample-mod-v1.log: mod=1 startup ok, 120 s no wom_event
- board_logs/2026-06-04-23-37-34-qmi8658c-reva-raw-motion-v1.log: 5 次 WoM、5 个原始六轴动作窗口、5 个 `raise_result`；`source=ae_dq`、`mod=1`、`raw_motion_window_failed`、panic/watchdog 均为 0
- 官方 Rev A 寄存器图中 `CTRL6(0x07)`、`CTRL7.bit3`、`STATUS0.bit3` 均为保留项；`CTRL9 0x0C` 为 `Configure Tap`，不是旧 Rev0.6 的 Motion-on-Demand
- 指定 28 项 unittest 通过；`idf.py fullclean/build` 与 `idf.py -p COM3 app-flash` 通过
- 与预期不一致的点：
- 旧路径中的 CTRL9 握手可以正常完成，但命令语义是 Tap 配置，因此它不能令旧资料中的 `STATUS0.sDA` 置位，也不会产生 dQ

## 结论

- 根因不是 CTRL9 握手时序，也不是用户没有移动手表；根因是把旧 Rev0.6 AE/MoD/dQ 寄存器定义用于当前 Rev A 芯片。
- 当前 Rev A 可用能力是原始加速度/陀螺仪、WoM、STATUS1/STATUSINT 和 Rev A CTRL9 命令；软件抬腕已切换到这些已验证能力。
- 当前 `imu_motion` 规则能够消费真实变化的六轴帧并给出 `motion_reason`，但尚未完成真实佩戴抬腕/非抬腕样本的阈值调优。

## 未验证风险

- 下一轮仍需补证据的边界：
- 用当前 `source=raw_motion` 固件重新抓明确标注的真实佩戴 raise/negative 样本，再调 `imu_motion` 与 final pose 阈值
- 若未来更换 QMI revision，必须先按对应官方数据手册重新建立 capability map，不能从当前 Rev A 结论外推
