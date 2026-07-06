---
id: qmi8658c-minimal-probe
tags: esp32-s3, imu, qmi8658c, i2c, probe, wearable, gpio21, interrupt, revision-a, unified-config
summary: QMI8658C Rev A 在当前板上的稳定接线、可用能力、历史 INT1/WoM 证据与当前统一配置策略。
last_reviewed: 2026-07-07
memory_type: semantic
scope: board
owners: components/qmi8658c, main/app/board_imu.c, main/services/imu_service.c
triggers: qmi8658c, imu, wom, qmi_int1, gpio21, revision-a, status0 sda, ctrl9 0x0c, unified-config
evidence_level: observed
status: active
---

# QMI8658C 板级接入与 INT1 证据

## 2026-07-07 当前固件策略

- 当前固件不再把 WoM 作为运行主线：public driver API 已删除 `enable_wom/disable_wom/read_wom/read_int`，`STATUSINT` 不再作为 public service 入口。
- `qmi8658c_config()` 是唯一普通配置入口，负责加速度/陀螺仪量程、ODR 和 sensor enable；当前默认最大量程为 `accel_fs=3`（±16g）和 `gyro_fs=7`（±2048 dps）。
- `qmi8658c_config_t` 保留 `int1_source/int2_source` 作为芯片侧 INT 事件源预留字段；第一版唯一支持 `QMI8658C_INT_SOURCE_DISABLED`，不会写 INT1/INT2 事件源寄存器。
- `imu_service` 当前随 deferred services 默认启动：完成 `probe -> qmi8658c_config()` 后安装 ESP32 GPIO21 ISR，并用 FreeRTOS task 做 50Hz 周期采样、维护 200 帧 / 4 秒环形缓冲；不做 WoM poll fallback。
- 当前 QMI 原始读取对齐已验证的 Waveshare 口径：配置 `CTRL1=0x60`、`CTRL5=0x03`，读取 `STATUS0`、24-bit timestamp、temperature，并从 `AX_L` 开始读取 12 字节六轴数据。
- 下方 WoM/INT1 内容是历史板测证据和排查资料，不代表当前固件运行路径。

## 当前 owner 与边界

- 芯片协议与寄存器操作：`components/qmi8658c`
- 板级地址、中断 GPIO 与安装方向：`main/app/board_imu.c`
- 当前统一配置与采样服务：`main/services/imu_service.c`
- 默认调用方向：`imu_service -> imu_sensor -> qmi8658c -> shared I2C`；`board_imu` 只提供 I2C 地址、GPIO21 和安装方向事实。
- QMI8658C 已不是“未接入器件”；当前固件以统一配置、物理六轴读取和 50Hz service 采样为主，不启用芯片 WoM 或芯片侧 INT 事件源。

## 原理图稳定事实

- 视觉复核源：`C:\Users\ye\Desktop\esp32s3手表项目手册\ESP32-S3-Touch-AMOLED-2.06.pdf` 第 1 页。
- `U5` 为 `QMI8658C`，`VDD/VDDIO -> VCC3V3`，`CS -> VCC3V3`，因此使用 I2C 模式。
- `SCL -> ESP32_SCL -> GPIO14`，`SDA -> ESP32_SDA -> GPIO15`。
- 共享 I2C 由 `R23/R49` 两个 `2.2k` 电阻上拉到 `VCC3V3`；QMI、RTC、触摸和 codec 控制面共用该总线。
- `QMI_INT1 -> GPIO21` 是直接网络，中间没有外部上拉、下拉、串联电阻、RC、复用器或测试点。
- `QMI_INT2 -> TP15`，没有接入 ESP32；`TP15` 是隔离验证 QMI 中断输出的优先观测点。
- 板上的 `TP_INT -> GPIO38` 属于触摸中断，不是 QMI 中断测试点。
- `SDO/SA0` 在原理图中接地，但同一原理图标注地址 `0x6B`；这与 QMI8658C Rev0.6 手册的 SA0 地址描述存在矛盾，当前硬件应以实测 `0x6B` 为准。
- 原理图只显示一个 `C26 100nF` 为合并后的 VDD/VDDIO 去耦；手册推荐 `Cp1/Cp2` 各 `100nF`。这是硬件鲁棒性关注点，不是当前原始数据测试失败原因。

## IMU 六面安装方向

- 六面测试成功日志记录的 QMI8658C 芯片坐标系：
  - `FACE_UP -> -Z`
  - `FACE_DOWN -> +Z`
  - `USB_UP -> -X`
  - `USB_DOWN -> +X`
  - `RIGHT_UP -> +Y`
  - `LEFT_UP -> -Y`
- 结合当前板 “USB 在手表右侧” 的物理方向：
  - 表盘朝上 / 正面朝上 -> QMI8658C `-Z`
  - 表背朝上 / 背面朝上 -> QMI8658C `+Z`
  - 手表右侧 / USB 侧朝上 -> QMI8658C `-X`
  - 手表左侧朝上 -> QMI8658C `+X`
  - 手表顶部朝上 -> QMI8658C `+Y`
  - 手表底部朝上 -> QMI8658C `-Y`
- 一句话：当前板子静止平放、表盘朝上时，加速度主轴是 `-Z`；USB 侧对应 `-X`。这属于 `board_imu` 板级安装事实，不属于 QMI8658C 芯片 driver 或抬腕/跌倒算法阈值。

## 2026-06-04 板级证据

- COM3 原始数据诊断日志：
  - `board_logs/2026-06-04-17-49-16-qmi8658c-raw-after-wom-exit.log`
  - `WHO_AM_I=0x05`，`revision_id=0x7c`
  - 连续原始加速度样本有效且变化
  - `result: PASS`
- COM3 确定性 INT1 诊断日志：
  - `board_logs/2026-06-04-18-33-57-qmi8658c-int1-wom-candidate-scan.log`
  - GPIO21 启用内部下拉后从 `1` 变为 `0`，同时 `STATUSINT.INT1=0`：`floating_suspected=1`
  - 三次真实 WoM 均出现 `STATUS1=0x04`、`STATUSINT 0x02 -> 0x00`
  - 三次事件中 GPIO21 始终为高，安全候选 GPIO 的 `follow_mask` 均为 `0`
  - 结论：QMI 内部 WoM/INT1 状态正常，但当前样板没有任何可用 MCU GPIO 跟随该输出
- COM3 主固件降级日志：
  - `board_logs/2026-06-04-18-40-15-qmi8658c-main-poll-fallback.log`
  - 启动时同时读取到 `GPIO21=1`、`STATUSINT.INT1=0`，发布 `int1_path_unusable`
  - service 关闭浮空 GPIO21 的中断输入，启用 `fallback_poll_ms=20`
  - 90 秒运行无 panic/watchdog，且没有再依赖旧的 10 秒 `wom_poll_recovery`
- COM3 Rev A 原始运动窗口日志：
  - `board_logs/2026-06-04-23-37-34-qmi8658c-reva-raw-motion-v1.log`
  - 90 秒内观察到 5 次 WoM、5 个 16 帧原始六轴动作窗口和 5 个 `raise_result`
  - `source=ae_dq`、`mod=1`、`raw_motion_window_failed`、panic/watchdog 均为 0

## 当前结论与运行策略

- QMI8658C 芯片、供电、共享 I2C、原始六轴数据、内部 WoM 状态和 Rev A CTRL9 握手已经可用。
- 2026-06-04 COM3 样板 `U5 INT1 -> QMI_INT1 -> ESP32 GPIO21` 的实测行为是浮空/开路；原理图标称连接不能替代具体样板的导通证据。
- 芯片内部 `STATUSINT.INT1` 和 `STATUS1.WoM` 能正常变化，故障不在 WoM 生成逻辑，也不在 ESP32 ISR 配置。
- 2026-07-07 当前 COM7 板曾闭环捕获 `wom_event source=irq gpio=21 statusint=0x02 status1=0x04`，证明当前板 IRQ 链路可用；该结论只适用于当次板测，不要求当前固件继续启用 WoM。
- Waveshare 同板官方 `04_Immersive_block` 只轮询 QMI 原始数据，没有配置或验收 INT1；官方例程运行正常不能作为 GPIO21 中断通路正常的证据。
- 固件无法修复铜线、焊接或板型映射问题。需要真实 IRQ 时，应断电测通断并修复 INT1 到 GPIO21，或将 `INT2/TP15` 飞线到可用 GPIO。
- 旧版 service 的兼容策略曾是：启动时对照 `STATUSINT.INT1` 与 GPIO21，不一致则禁用浮空 ISR 并轮询 `STATUS1.WoM`；该路径已在 2026-07-07 统一配置收口中删除。
- 当前软件路径是 `imu_service -> imu_sensor -> qmi8658c_probe/config/read` 的物理六轴契约；50Hz 采样已经在 service 层运行。抬腕、跌倒识别仍需要在后续 service/algorithm 任务中定义。

## CTRL9 与 STATUSINT 协议结论

- 当前板 `revision_id=0x7c` 与官方 QMI8658C Rev A 数据手册一致。
- 官方 Rev A 数据手册：`https://www.qstcorp.com/upload/pdf/202210/13-52-27%20QMI8658C%20Datasheet%20Rev%20A%20%281%29.pdf`。
- Rev A 中 `CTRL8.bit7=1` 选择通过 `STATUSINT.bit7` 完成 CTRL9 握手，不使用 INT1 通知命令完成；默认 `0` 才会用 INT1 完成握手。
- driver 现在在执行 CTRL9 前以 read-modify-write 设置 `CTRL8.bit7`，等待 `STATUSINT.bit7` 置位，写 `CTRL9=0x00` ACK，再等待该位清除后返回。
- Rev A 中 `CTRL9 0x0C` 是 `Configure Tap`，不是 Motion-on-Demand；`CTRL6(0x07)`、`CTRL7.bit3` 和 `STATUS0.bit3` 均为保留项，因此当前芯片不存在旧资料定义的 AE enable、dQ 数据区或 `STATUS0.sDA` 等待条件。
- `CTRL1.bit3` 在 QMI8658C 中为保留位；不要直接复制面向 QMI8658A 的 SensorLib `enableINT()` 行为。
- 本地 Rev0.6 手册描述的 AE/MoD/dQ 路径不适用于当前 `0x7c` Rev A 样板；不得通过延长等待或修改阈值尝试令 `STATUS0.sDA` 置位。

## 可复用排查顺序

1. 先验证 `WHO_AM_I=0x05`、`REVISION_ID` 和 `qmi8658c_config()` 后的物理六轴读取，不要把“无 GPIO 中断”等同为“QMI 不可用”。
2. 同一时刻读取 `STATUSINT.INT1` 与 `gpio_get_level(GPIO21)`：
   - `STATUSINT.INT1=0`、`GPIO21=1`：优先检查 PCB 网络、焊接、板型版本或 GPIO 映射。
   - 两者都为 `1`：QMI 正在主动输出高，继续检查 WoM 初始电平和读取 `STATUS1` 后的复位行为。
   - 两者同步变化但 ISR 不触发：再检查 ESP32 GPIO ISR 配置。
3. 临时启用 GPIO21 内部下拉做输入诊断：
   - GPIO21 被拉低，说明线路可能浮空或开路。
   - GPIO21 仍为高，说明存在主动驱动或对高电平短接。
4. 需要进一步硬件确认时，把 WoM 输出切到 `INT2_INITIAL_LOW`，在 `TP15` 测量：
   - TP15 正常翻转，说明 QMI 中断生成正常，问题集中在 INT1/GPIO21 通路。
   - TP15 不翻转但 `STATUS1.WoM=1`，说明中断输出配置或芯片 revision 语义仍需确认。
5. 需要恢复真实 IRQ 时，断电测量 U5 INT1 到 ESP32-S3 GPIO21/package pin27 的通断，并检查该网络是否对 3.3V 短路。

## 不要重复的判断

- 不要给 INT1 补外部上拉来解释当前高电平；原理图没有该上拉，QMI INT1 也不是依赖外部上拉的开漏输出。
- 不要把 `TP_INT(GPIO38)` 当作 QMI 中断测试点；QMI 可直接探测的测试点是 `INT2 -> TP15`。
- 不要再次用普通 GPIO ISR 配置变化尝试修复 COM3 样板；芯片镜像与 GPIO 电平不一致已经证明当时问题位于物理通路。
- 不要把旧 20 ms WoM 轮询 fallback 当作当前固件主线；当前版本已经移除 WoM 路径。
- 不要在当前 Rev A 芯片上重新引入旧 Rev0.6 的 AE/MoD/dQ 寄存器定义；`CTRL9 0x0C` 只可按 Tap 配置命令理解。

## 适用边界

- 本卡记录当前样板稳定板级事实、当前 owner 和已验证能力；不能将当前样板的物理开路结论泛化到所有 ESP32-S3-Touch-AMOLED-2.06 批次。
- 历史软件轮询与原始六轴动作窗口已闭环，但当前固件已移除该路径；抬腕参数仍未完成佩戴体验调优。
- 本次 MoD/sDA 根因与旧路径证据见 `docs/context/runs/2026-06-04-attempt-qmi8658c-software-raise-fallback-sample-and-mod-dq.md`。
- 本次详细原理图与板测探索记录见 `docs/context/runs/2026-06-04-attempt-qmi8658c-int1-gpio21-schematic-board-evidence.md`。
- 若后续更换硬件版本、QMI revision 或 `SA0` 绑法，需要重新确认地址和中断行为。
