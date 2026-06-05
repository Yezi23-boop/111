---
id: attempt-2026-06-04-qmi8658c-int1-gpio21-schematic-board-evidence
date: 2026-06-04
status: completed
result: resolved_with_software_fallback
summary: 视觉复核原理图并完成 COM3 确定性板测，确认当前样板 QMI INT1 到 GPIO21 物理通路浮空/开路；修复 Rev A CTRL9 握手，并为正式 service 增加 20 ms WoM 轮询降级。
last_reviewed: 2026-06-04
scope: repo
owners: components/qmi8658c, main/app/board_imu.c, main/services/imu_service.c
tags: attempt, qmi8658c, imu, schematic, wom, gpio21, interrupt, board-test, com3
record_because: 该轮包含原理图稳定事实、COM3 板测证据和高复用的软硬件隔离顺序；后续若不记录，容易重复调抬腕阈值或误判 QMI 整体不可用。
---

# QMI8658C INT1(GPIO21) 原理图与板级证据探索

## 背景

- 本次要验证：QMI8658C 是否正常工作，以及 WoM 事件为什么没有通过 `QMI_INT1(GPIO21)` 即时通知。
- 当前抬腕链路为 `WoM -> AE dQ 短窗口 -> final pose -> raise_result`。
- 初始主链日志只能依赖约 10 秒轮询恢复；此时真实抬腕动作已经结束，AE dQ 累计旋转为 `0`。

## 环境

- 仓库：`D:\esp32S3\111`
- 分支：`codex/qmi8658c-test-program`
- 设备/串口：ESP32-S3-Touch-AMOLED-2.06，`COM3`
- 原理图：`C:\Users\ye\Desktop\esp32s3手表项目手册\ESP32-S3-Touch-AMOLED-2.06.pdf`
- QMI8658C 数据手册：`C:\Users\ye\Desktop\esp32s3手表项目手册\QMI8658C.pdf`

## 操作

- 将原理图三页高分辨率渲染并逐块视觉复核：
  - 第 1 页用于电气网络判断。
  - 第 2/3 页只用于 PCB 器件位置和探针可达性参考。
- 追踪 `VCC3V3`、共享 I2C、`QMI_INT1`、`QMI_INT2`、GPIO21 和测试点。
- 对照 QMI8658C Rev0.6 手册中的 INT1 类型、`STATUSINT` 镜像、WoM 初始电平和 `STATUS1` 清除语义。
- 对照 Waveshare 同板官方 `ESP-IDF-v5.4.2/04_Immersive_block`：该例程持续轮询 QMI 原始数据，没有配置或验收 INT1，因此官方例程可证明 I2C/原始数据路径，不能证明 `QMI_INT1 -> GPIO21` 物理通路。
- 对照当前 `board_imu`、`qmi8658c`、`imu_service` 实现和两份 COM3 日志。
- 对照官方 Rev A 数据手册，确认当前板 `revision_id=0x7c` 对应 `CTRL8.bit7 + STATUSINT.bit7` 的 CTRL9 握手路径。
- 修复 driver：执行 CTRL9 前设置 `CTRL8.bit7`，ACK 后等待完成位清除；不占用 INT1 做 CTRL9 命令完成通知。
- 扩展诊断固件：同一时刻比较 `STATUSINT.INT1` 与 GPIO21，执行 GPIO21 内部下拉隔离，并在三次真实 WoM 事件中扫描安全候选 GPIO。
- 正式 `imu_service` 增加启动期 INT1 通路检测；不一致时锁存故障、禁用浮空 ISR，并切换到 20 ms `STATUS1.WoM` 轮询。

## 原理图观测

- `U5 QMI8658C`：
  - `VDD/VDDIO/CS -> VCC3V3`
  - `SCL -> ESP32_SCL -> GPIO14`
  - `SDA -> ESP32_SDA -> GPIO15`
  - `QMI_INT1 -> GPIO21`
  - `QMI_INT2 -> TP15`
- `QMI_INT1 -> GPIO21` 为直接网络，中间没有外部上拉、下拉、RC、串联电阻、复用器或测试点。
- 板上的 `TP_INT -> GPIO38` 属于触摸中断，不是 QMI 中断。
- 共享 I2C 由 `R23/R49` 两个 `2.2k` 电阻上拉到 `VCC3V3`。
- QMI 原理图只显示一个 `C26 100nF` 去耦；手册推荐 VDD/VDDIO 对应的两个 `100nF`。
- 原理图中 `SA0` 接地，同时标注地址 `0x6B`；该标注与 Rev0.6 手册地址描述不一致，当前硬件以实测地址为准。

## 板级观测

原始数据诊断日志：

- `board_logs/2026-06-04-17-49-16-qmi8658c-raw-after-wom-exit.log`
- `WHO_AM_I=0x05`
- `revision_id=0x7c`
- 连续原始加速度样本有效且变化
- `result: PASS`

主链日志：

- `board_logs/2026-06-04-17-52-16-qmi8658c-main-after-raw-pass.log`
- WoM 配置成功，`STATUS1 bit2` 多次确认 WoM 事件。
- AE 短窗口、dQ 帧读取和最终姿态判断能够运行，多次最终姿态通过。
- GPIO21 在配置后、等待时、读取 STATUS1 前后均观察为高。
- 未观察到 GPIO21 ISR 边沿，WoM 事件由约 10 秒轮询恢复路径发现。
- 由于 AE 启动太晚，当前 `ROTATION_SMALL / total_mdeg=0` 不能证明抬腕动作规则失败。

确定性 INT1/WoM 诊断日志：

- `board_logs/2026-06-04-18-33-57-qmi8658c-int1-wom-candidate-scan.log`
- GPIO21 内部下拉检查：`before=1 pulldown=0 int1_mirror=0 floating_suspected=1`。
- 三次真实 WoM 事件均观察到 `status1=0x04` 与 `STATUSINT 0x02 -> 0x00`。
- 每次事件中 GPIO21 都保持高电平，全部安全候选 GPIO 的 `follow_mask=0`。
- 诊断输出 `result: FAIL err=ESP_ERR_INVALID_RESPONSE` 是正确结果：它表示物理 INT1 路径验收失败，不表示 QMI 原始数据或内部 WoM 失败。

正式主固件降级日志：

- `board_logs/2026-06-04-18-40-15-qmi8658c-main-poll-fallback.log`
- 启动时观察到 `GPIO21=1`、`STATUSINT.INT1=0`，发布：
  `int1_path_unusable ... fallback_poll_ms=20`
- 随后发布：
  `wom_configured ... int1_usable=0`
- 90 秒内无 panic/watchdog，也没有使用旧的 10 秒 `wom_poll_recovery`。
- 板子在该窗口内保持静止，因此没有新的 `wom_event: source=poll`；三次真实 WoM 事件已经由前述诊断固件证明内部事件可轮询读出。

## 结论

- 已确认 QMI8658C 的供电、共享 I2C、原始六轴数据、内部 WoM 状态和 AE 数据路径可用。
- 当前样板 `U5 INT1 -> QMI_INT1 -> ESP32 GPIO21` 物理通路浮空/开路；证据不是单次 GPIO 读值，而是内部下拉隔离加三次真实 WoM 的寄存器/GPIO 同步对照。
- 故障不在 QMI WoM 生成、不在 CTRL9 握手，也不在 ESP32 GPIO ISR 配置；固件不能修复铜线、焊接或板型映射。
- 软件功能已通过 20 ms `STATUS1.WoM` 轮询降级恢复，能够在动作窗口内启动 AE，不再使用 10 秒恢复路径。
- 真实 IRQ 需要断电测通断并修复 INT1 到 GPIO21，或将 `INT2/TP15` 飞线到可用 GPIO。
- 当前样板结论不能自动泛化到所有同型号板批次；每块板都应先做寄存器镜像/GPIO 对照。

## CTRL9/STATUSINT 协议结论

- 本地 `QMI8658C Rev0.6` 手册描述：
  - CTRL9 命令完成后设置 `STATUS1.CmdDone(bit0)` 并拉高 INT1。
  - 主机读取 `STATUS1` 作为确认，随后 CmdDone 清除且 INT1 拉低。
  - `STATUSINT bit1/bit0` 镜像 INT1/INT2，`bit7:2` 为保留位。
- 官方 Rev A 与当前板 `revision_id=0x7c` 对齐：
  - `CTRL8.bit7=1` 选择 `STATUSINT.bit7` CTRL9 握手，不通过 INT1 通知命令完成。
  - 默认 `CTRL8.bit7=0` 才使用 INT1 完成握手。
- 修复后的 driver 在命令前设置 `CTRL8.bit7`，等待 `STATUSINT.bit7` 置位，写 `CTRL9=0x00` ACK，再等待该位清除。
- `CTRL1.bit3` 在 QMI8658C 中为保留位，不能直接复制 QMI8658A SensorLib 的 `enableINT()`。

## 已尝试但不应直接重复的路径

- 不要继续先调 `35°~220°` 累计旋转阈值或最终姿态阈值；当前动作窗口采集时机不成立。
- 不要假设 GPIO21 高电平来自原理图外部上拉；原理图没有该上拉，QMI INT1 也是主动驱动输出。
- 不要在 `TP_INT(GPIO38)` 测量 QMI 中断。
- 不要把 QMI 整体判为不可用；原始数据、WoM 状态和 AE 已有明确通过证据。
- 不要继续尝试 GPIO 中断触发模式或普通 ISR 配置变化；寄存器镜像与 GPIO21 不一致已经把故障定位到物理通路。
- 不要恢复 10 秒轮询作为正式路径；20 ms 降级只轮询 WoM 状态，动作触发后仍进入同一 AE/dQ/final-pose 链路。

## 后续硬件修复验证

1. 断电测量 U5 INT1 到 ESP32-S3 GPIO21/package pin27 的通断，并检查该网络是否对 3.3V 短路。
2. 若原网络难以修复，把 WoM 输出切到 `INT2_INITIAL_LOW`，确认 `TP15` 边沿后飞线到一个已验证可用 GPIO。
3. 修复后重新跑确定性诊断，验收条件是 GPIO 与 `STATUSINT.INT1` 同步、内部下拉不再能任意改变电平、真实 WoM `follow_mask` 命中新 GPIO。
4. 真实 IRQ 闭环后再关闭软件 fallback，并重新采集佩戴状态下的真实抬腕 dQ/最终姿态样本。

## Owner、风险与回退

- 板级 GPIO、地址和安装方向继续由 `main/app/board_imu.c` 持有。
- QMI 寄存器与 `STATUSINT/STATUS1` 语义继续由 `components/qmi8658c` 持有。
- `main/services/imu_service.c` 只编排 WoM、AE 短窗口与结果发布，不应硬编码新的板级引脚。
- 当前诊断与正式抬腕路径保持 `log_only`，未接入亮屏或 sleep 行为。
- 正式降级路径只替代 WoM 通知来源，不改变 AE/dQ/final-pose 算法 owner。
- 20 ms 周期由 `imu_service` task 使用 FreeRTOS timeout 等待实现；这是 owner task 的定时等待，不新增跨上下文裸 flag。

## 验证

- QMI/诊断/IMU service source tests：
  `uv run python -m unittest tests.test_qmi8658c_source tests.test_imu_service_source`
  - 结果：主线 source tests 通过；当时还包含的临时 `tests.test_qmi8658c_diagnostic_source` 已在合并前清理，不作为主线回归入口。
- 主固件 `idf.py build` 通过，随后使用 `idf.py -p COM3 app-flash` 恢复到 COM3。
- 正式主固件日志：
  `board_logs/2026-06-04-18-40-15-qmi8658c-main-poll-fallback.log`
  - 启动识别 `int1_path_unusable`
  - 启用 `fallback_poll_ms=20`
  - 90 秒无 panic/watchdog
- 上下文标准校验：
  `uv run python scripts/context/validate_context.py --level standard --q "QMI8658C INT1 GPIO21 polling fallback" --brief`
  - 结果：检查 130 个文件，0 错误、0 警告。

## 未验证风险

- 尚未在 `TP15` 验证 INT2 WoM 输出。
- 尚未对 INT1/GPIO21 做断电导通和短路测量。
- 主固件 90 秒验证窗口内板子静止，尚未直接观察正式 service 的 `wom_event: source=poll`；内部 WoM 可轮询读出已有三次诊断固件真实事件证据。
- 尚未在佩戴状态下采集软件 fallback 触发后的真实抬腕 dQ 与 `raise_detected`，抬腕阈值仍需后续样本调优。
