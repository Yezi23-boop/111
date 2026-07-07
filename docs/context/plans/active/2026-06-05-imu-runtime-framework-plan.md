---
id: plan-2026-06-05-imu-runtime-framework
tags: imu, qmi8658c, imu_sensor, framework, board-facts, service, physical-6axis, unified-config, fall-detection, esp-dl
summary: 只定 IMU 运行框架：driver/board/adapter/service/algorithm 边界、统一配置入口、物理六轴输出、50Hz 窗口和跌倒模型部署扩展点。
last_reviewed: 2026-07-07
memory_type: task
scope: imu
status: active
owners: components/qmi8658c, components/imu_sensor, main/app/board_imu.c, main/services/imu_service.c, main/services/fall_detection_service.c, components/fall_detection_inference, components/imu_motion
triggers: IMU framework, imu runtime, qmi8658c, imu_sensor, board_imu, imu_service, imu_motion, physical_6axis, unified config, fall detection, ESP-DL
evidence_level: design
route_area: "IMU / motion framework"
---

# IMU 运行框架计划

## Purpose / Big Picture

- 任务目标：先把 IMU 的嵌入式框架定清楚，保证后续换板、换中断线、换算法时不会把板级事实、芯片协议、运行时服务和识别规则混在一起。
- 为什么现在做：QMI8658C 已经验证过 I2C、物理六轴换算、INT1/GPIO21 历史闭环和 WoM 实验路径，但当前固件不再把 WoM 作为运行主线；先把 driver/BSP/service 边界收口到统一配置和物理量输出，避免后续跌倒检测或抬腕识别复用旧实验路径。
- 完成后用户会看到什么变化：后续任务会默认沿用 `imu_service -> imu_sensor -> qmi8658c` 的设备访问链路，并由 `board_imu` 提供当前板地址、GPIO 和安装方向事实；采样策略可以新增在 service/algorithm 层，不需要重写底层芯片协议或板级事实。

## Scope / Non-Goals

- 本计划明确要做：定义 IMU driver、board facts、sensor adapter、service owner、algorithm module 的职责边界；固定 `qmi8658c_config()` 作为 QMI8658C driver 唯一普通配置入口；固定 driver 对外只输出 `m/s^2` / `deg/s` 物理六轴；保留后续调参、连续采样、跌倒检测和抬腕识别的扩展入口。
- 本计划明确不做：不解决真实抬腕识别准确率；不区分左右手；不启用 QMI8658C AE/MoD/dQ 或内部四元数作为产品依赖；不自动合成负样本；不承诺阈值、召回率或误触率；当前第一版不启用 WoM 或芯片 INT 事件源，ESP32 GPIO21 ISR 只作为 service 层运行时事件入口。

## Owner Boundaries

- `components/qmi8658c`：只做芯片协议 owner，包括 I2C register 读写、WHO_AM_I/revision probe、寄存器 raw 解码、物理量换算、量程/ODR/sensor enable 统一配置和最小错误码；对 board/service 层只输出 `m/s^2` / `deg/s` 等物理量，不暴露 raw sample；不得持有板级 GPIO、安装方向、产品阈值或 FreeRTOS 任务生命周期。
- `components/imu_sensor`：只做通用 IMU 窄接口和当前 QMI8658C 适配，包括 `init/probe/config/read` 的类型转换；不得持有当前板 GPIO、安装方向、FreeRTOS task、ISR 或采样窗口。
- `main/app/board_imu.c`：只做板级事实 owner，包括 I2C 地址、INT1/INT2/GPIO 事实和安装方向/轴映射；当前保留 `QMI_INT1 -> GPIO21` 作为真实连线事实，但不代表当前固件启用该 GPIO 中断；不得实现抬腕识别规则，也不得直接推进长期运行状态。
- `main/services/imu_service.c`：做运行 owner。当前第一版随 deferred services 默认启动，用 FreeRTOS task 完成 `board_imu facts -> imu_sensor init/probe/config`，安装 ESP32 GPIO21 ISR 并通过 task notification 处理 INT1 GPIO 事件；同时用 `vTaskDelayUntil` 做稳定 50Hz 周期采样，并维护 200 帧 / 4 秒环形缓冲。不消费 WoM、不启用芯片侧 INT 事件源。后续若启用 fall service queue/window 投递或 data-ready 中断，应继续落在 service 层。
- `components/imu_motion`：做纯算法接口 owner，输入标准化 motion window，输出 `raise_detected / reject_reason / debug metrics`；不得访问 I2C、GPIO、FreeRTOS 或 board 配置。

## Runtime Data Flow

```text
board_imu board facts
  -> imu_service owner task and GPIO21 ISR
  -> imu_sensor generic IMU API
  -> qmi8658c probe/config/read
  -> imu_service 50Hz ring buffer
  -> service snapshot / fall_detection_service window queue
  -> fall_detection_inference ESP-DL runner
  -> fall detection snapshot / logs
```

- 当前硬件事实：2026-06-04 COM3 样板曾按板测归为 `QMI_INT1 -> GPIO21` 浮空/开路风险；2026-07-07 当前 COM7 板已闭环捕获 `wom_event source=irq gpio=21 statusint=0x02 status1=0x04`，证明当前板 GPIO IRQ 链路可用。当前固件不启用 WoM、不配置芯片 INT 事件源；GPIO21 ISR 只验证 ESP32 侧中断路径与 service owner 分层。
- 当前配置语义：`imu_service` 启动后只做探测和统一配置，使用最大动态范围：加速度 `accel_fs=3`（±16g），陀螺仪 `gyro_fs=7`（±2048 dps），`int1_source/int2_source=QMI8658C_INT_SOURCE_DISABLED`。
- 当前结果语义：`imu_service` 的 `RUNNING/configured=true` 表示芯片完成统一配置、GPIO21 ISR 已安装，且 service 会进入 50Hz 采样循环；`fall_detection_service` 的 `RUNNING/model_ready=true` 表示模型已加载并通过内嵌 test vector。跌倒告警第一版已接入轻量状态机：单窗口 `fall_prob>=0.80` 确认，复用 `app_alert_manager` 做红屏/震动/提示音，并通过 watch endpoint 上传一次；连续 3 个窗口 `fall_prob<0.50` 后清除。

## Framework Contract

- 当前 `qmi8658c_read()` 必须在 `qmi8658c_config()` 成功后才返回物理六轴样本；未配置时返回 `ESP_ERR_INVALID_STATE`。
- 后续重新引入 motion event 时，每个 event 至少保留：event id、trigger source、sample interval、frame count、accel/gyro 物理量或算法标准化值、final accel norm、stability 指标、`raise_detected`、`reject_reason`。
- CSV 样本日志是后续调规则的接口，不是最终产品 UI；重新启用采样日志时字段要稳定，允许后续增加列，但不要改已有列含义。
- `imu_service` getter 只能读 snapshot，不做 I2C、阻塞等待或状态推进；真实采样和错误恢复必须留在 owner task。
- `fall_detection_service` 只能消费 `imu_service` 投递的完整 200 帧加速度窗口副本，不直接访问 `qmi8658c_*` 或 `imu_sensor_read()`；模型输入坐标系第一版固定为 `accX=-chip_accel.x`、`accY=+chip_accel.y`、`accZ=-chip_accel.z`，按帧交错展开为 600 个 float。
- `imu_motion` 第一版可以是占位/简单规则，但必须保持输入输出接口稳定，让后续 dQ、自研姿态、统计规则或轻量 ML 都能替换进去。

## Progress

- `[x]` 已有 QMI8658C 最小 driver、本地组件和 source tests。
- `[x]` 已有 `board_imu` 承接板级 I2C/GPIO/安装方向事实。
- `[x]` 历史版本已验证 `imu_service` 可承接 20 ms WoM fallback、16 帧窗口和日志发布；当前版本已按用户要求移除该运行路径。
- `[x]` 已有 `imu_motion` 纯规则模块作为算法替换点。
- `[x]` 2026-07-07：按 framework review 将 driver 对外采样契约收口为物理量；`board_imu/imu_service` 不再依赖 raw sample 或 `accel_lsb_per_g`。
- `[x]` 2026-07-07：QMI8658C public header 完成简约命名收口；对外只保留 `qmi8658c_read()` 完整物理六轴采样，单位由类型注释表达，字段保持 `x/y/z`。
- `[x]` 2026-07-07：按 driver/BSP/service 解耦原则收窄 `board_imu`，只保留 I2C 地址、INT1 GPIO 和 IMU 安装方向；WoM/窗口/姿态阈值迁入 `imu_service` 本地 profile。
- `[x]` 2026-07-07：完成当前 COM7 板 QMI8658C INT1/GPIO21 闭环测试，捕获 3 次 `source=irq` WoM 事件、16 帧物理六轴窗口和 `raise_result`，当前板 GPIO IRQ 链路可用。
- `[x]` 2026-07-07：按用户要求将 `imu_service` profile 切到最大量程：WoM/运动窗口加速度 `±16g`，运动窗口陀螺仪 `±2048 dps`。
- `[x]` 2026-07-07：按用户要求移除 WoM 运行路径和 public WoM API；`qmi8658c_config()` 成为唯一普通配置入口，预留 `int1_source/int2_source` 且第一版只支持 disabled；`imu_service` 收口为探测 + 最大量程配置，不连续采样，芯片侧 INT 事件源保持 disabled。
- `[x]` 2026-07-07：参考小智 Board/Device 分层思路，新增 `components/imu_sensor` 极窄适配层；`imu_service` 改为 `imu_service -> imu_sensor -> qmi8658c`，并在 service 层安装 GPIO21 ISR，通过 FreeRTOS task notification 处理事件计数。
- `[x]` 2026-07-07：按用户要求移除 `app_main.c` 中包住 `imu_service_start()` 的 `#if 0`，IMU service 现在随 deferred services 默认启动，开机即可验证 probe/config/GPIO21 ISR 安装日志。
- `[x]` 2026-07-07：按用户要求先做稳定 50Hz 采样；`imu_service` 使用 `vTaskDelayUntil` 每 20ms 调用 `imu_sensor_read()`，写入 200 帧环形缓冲，并在 snapshot 中发布 `sampling_active/sample_count/sample_error_count/last_sample_interval_us/sample_window_ready`。
- `[x]` 2026-07-07：COM7 闭环验证 50Hz 采样主线；最新 QMI 读法按 `STATUS0 -> TIMESTAMP -> TEMP -> AX_L 12 bytes` 对齐，`sample_50hz` 从 `count=1` 持续到 `count=1350`，`count=200` 后 `window_ready=1`，未见 panic。
- `[x]` 2026-07-07：记录当前板 IMU 六面安装方向：表盘朝上为 QMI8658C `-Z`，表背朝上为 `+Z`，USB/手表右侧朝上为 `-X`，手表左侧朝上为 `+X`，手表顶部朝上为 `+Y`，手表底部朝上为 `-Y`。
- `[x]` 2026-07-07：部署自训练 ESP-DL 跌倒模型 `cnn_c24_pool225_do015_e80_with_test.espdl`，SHA256=`10526143f02d047b0e5b2c29f29802396171998cfb4071cb54d7858375a98d54`；新增 `components/fall_detection_inference`，校验 `FLOAT [1,600]` 输入、2 类 `[ADL,FALL]` 输出并运行 `Model::test()`。
- `[x]` 2026-07-07：`imu_service` 新增完整窗口出口，50Hz/200 帧 ring 满后用 queue length 1 + `xQueueOverwrite()` 投递完整加速度窗口副本；当前按每 100 帧约 2s 发布一次，`fall_detection_service` 消费窗口、做 `[-x,+y,-z]` 轴向重映射、按帧交错 flatten、推理并发布只读 snapshot。
- `[x]` 2026-07-07：COM7 闭环验证 fall 部署链路；日志出现模型加载、`dl::Model: Test Pass!`、`sampling_started: rate_hz=50 window_frames=200`、`window_published` 和连续 `fall_window_result`，当前静止样本输出 ADL，`fall_prob=0.1480`，推理耗时约 14-30ms，未见 panic。
- `[x]` 2026-07-07：跌倒确认告警状态机接入完成；`IDLE -> FALL_CONFIRMED` 不再要求连续 2 个 FALL，单窗口 `fall_prob>=0.80` 即触发一次本地完整告警和 App 上传；`FALL_CONFIRMED` 期间不重复告警/上传，连续约 4 秒低风险窗口 `fall_prob<0.50` 后 clear。
- `[x]` 2026-07-07：将 IMU/Fall 大窗口缓冲迁到 PSRAM：`imu_service` 的 200 帧 ring 与 publish window、`fall_detection_service` 的 queue storage/current window/model input 均改为 `heap_caps_*` + `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`；`fall_detect` task 栈改为 `xTaskCreateWithCaps(..., MALLOC_CAP_SPIRAM)`。
- `[x]` 2026-07-07：按用户要求将当前 QMI8658C/IMU/Fall 关键运行日志中文化，并统一为表格化单行输出；QMI 原始采样调试、`imu_service` 50Hz 采样日志、窗口发布和 fall 推理结果按约 2s 输出（50Hz 下每 100 个样本）。低风险清除窗口数同步改为 2，约 4s 恢复证据。
- `[ ]` 后续补离线 replay/evaluator，把真实样本与规则阈值调参从固件循环中拆出来。
- `[ ]` 后续讨论并实现真实抬腕识别策略。

## Decision Log

- 2026-06-05：用户确认当前项目只先搭 IMU 框架，后续再讨论识别问题。
- 2026-06-05：第一版不区分左右手，不要求佩戴检测；桌面拿起并转向自己可接受为抬腕类动作。
- 2026-06-05：由于 QMI8658C 内部 AE/MoD/dQ 在当前板和公开资料之间仍有未闭环差异，本框架不把内部四元数或 dQ 作为产品依赖；后续可作为独立 diagnostics 或实验分支。
- 2026-07-07：采纳 framework reviewer 意见：QMI8658C driver 内部可保留 raw 寄存器解码，但 board/service/snapshot/log 只接收物理量；WoM 触发帧只记录物理加速度，临时运动窗口使用完整物理六轴。
- 2026-07-07：用户要求加速度和角速度全部使用最大量程；当前 profile 固定为 `accel_fs=3`（±16g）和 `gyro_fs=7`（±2048 dps），优先避免摔倒/撞击/快速旋转样本饱和。
- 2026-07-07：用户明确“不需要 WoM，统一配置就行”。当前固件删除 WoM public API、STATUSINT public read、service GPIO ISR/task notification/poll fallback，芯片侧 INT 字段只作为后续 data-ready/FIFO 中断扩展预留。
- 2026-07-07：用户确认采用 `imu_service -> imu_sensor -> qmi8658c`，并要求安装 GPIO21 ISR。当前决策为：ESP32 GPIO ISR 属于 `imu_service` 运行时资源；`imu_sensor` 只做传感器适配；`qmi8658c` driver 不知道 GPIO21，也不安装 ISR。
- 2026-07-07：用户要求“不需要 `#if 0`”，确认 IMU service 默认启动；该启动只验证统一配置和 GPIO21 ISR 事件计数，不表示已经开启连续采样或跌倒/抬腕算法。
- 2026-07-07：用户确认第一版先走稳定 50Hz 采样，而不是 FIFO Watermark；当前只在 `imu_service` 内缓存 200 帧窗口，不投递 fall service，不做模型推理。
- 2026-07-07：当前 QMI8658C 读取方法按 Waveshare 对齐口径收口：`CTRL1=0x60`、`CTRL5=0x03`，读取时分段取 `STATUS0`、24-bit timestamp、temperature 和从 `AX_L` 开始的 12 字节六轴原始数据。
- 2026-07-07：当前板六面映射只作为 board fact 记录在 `board_imu`，暂不新增通用坐标转换 API；后续 fall/raise 的模型输入坐标系需要基于该事实单独定义。
- 2026-07-07：用户确认当前仓库负责部署、`D:\esp32S3\imu` 负责训练；本轮部署已验证的自训练 ESP-DL 模型，不修改 `managed_components`、不改 ESP-DL 源码、不新增自定义算子。
- 2026-07-07：用户将 fall detection 第一版从“只做日志闭环”推进到“确认即告警”。当前默认阈值 `FALL>=0.80`，模型内已有 Softmax，板端只读取 `[ADL,FALL]` 概率，不做二次 Softmax；单个 4 秒窗口超过阈值即可确认，不采用连续 2 个 FALL 策略；同一次 `FALL_CONFIRMED` 期间不重复触发本地告警或 App 上传；按 2s 窗口发布后，连续 2 个 `fall_prob<0.50` 低风险窗口约等于 4s 清除证据。

## Validation and Acceptance

- source test：锁定 service 不硬编码 I2C 地址，driver 不消费 board facts，`imu_motion` 不访问硬件/FreeRTOS；锁定 public header 不再出现 WoM API/type；锁定 `imu_service` 不直接 include/call `qmi8658c_*`，GPIO ISR/task notification 只出现在 service 层。
- board evidence：当前第一版要求正常固件可 build/app-flash；开机日志应出现 `started: sampling_50hz`、`probe:`、`configured:`、`int1_gpio_ready`、`sampling_started: rate_hz=50 window_frames=200`、周期性 `sample_50hz:`、`boot_stage: imu_service_ready`、`Model::test()` 通过、`window_published` 和 `fall_window_result`。
- 2026-07-07 COM7 evidence：`board_logs/2026-07-07-06-11-28-serial.log` 显示 `configured registers: ctrl1=0x60 ctrl2=0x33 ctrl3=0x73 ctrl5=0x03 ctrl7=0x03`，`sample_50hz count=200 window_ready=1`，采集至 `count=1350`，`panic_log_seen=0`。
- 2026-07-07 COM7 fall evidence：`board_logs/2026-07-07-06-39-36-fall-detection-espdl.log` 显示 `model loaded: input=float exp=0 shape=[1, 600], output=float exp=0 shape=[1, 2]`、`dl::Model: Test Pass!`、`window_published: sequence=1 source_sample_count=200`、`fall_window_result: sequence=1 ... label=ADL(0) ... adl_prob=0.8520 fall_prob=0.1480 threshold=0.80 infer_ms=14.31`，summary `panic_log_seen=false`。
- 2026-07-07 COM7 fall alert evidence：`board_logs/2026-07-07-07-26-15-fall-alert-state-machine-psram-alert-tasks.log` 显示 `fall_window_result: sequence=12 ... fall_prob=0.8520` 后立刻 `fall_alert_confirmed`；随后 `haptic_alert_player: initial danger haptic started/finished`、`audio_alert_player: warning playback started/finished`、`display_alert: danger overlay shown`、`fall_app_upload_queued` 和 `watch_endpoint: danger alert dispatched: type=fall prob=0.8520 seq=1`；连续 3 个低风险窗口后 `fall_alert_cleared: ... clear_windows=3`，summary `panic_log_seen=false`。
- 2026-07-07 COM7 PSRAM buffer evidence：`board_logs/2026-07-07-07-46-41-fall-psram-window-buffers.log` 显示 `heap_init` 主 RAM 池恢复到 `142 KiB`，资源快照 `RAM: 304 KB / 332 KB (91.8%)`，`STACK: internal_free=26482 largest=24576 psram_free=6651280`；同轮仍有 `window_published`、连续 `fall_window_result`、`fall_alert_confirmed`、本地震动/提示音、`danger alert dispatched: type=fall` 和 `fall_alert_cleared`，summary `panic_log_seen=false`。
- 2026-07-07 中文日志验证：`qmi8658c` 周期日志为 `原始表`，`imu_service` 周期日志为 `采样表`、窗口日志为 `窗口表`，`fall_detection` 推理/告警日志为 `跌倒表`、`跌倒告警已确认/已清除`；普通采样、窗口发布和 fall 推理结果均按约 2s 输出；source tests 和 `idf.py build` 已覆盖 UTF-8 字符串编译。
- context validation：本文档变更至少运行 `uv run python scripts/context/validate_context.py --level standard --q "IMU runtime framework QMI8658C board_imu imu_service imu_motion" --brief`。
- build rule：若只改本文档，不要求 `idf.py build`；若后续改 `components` 或 `main` 代码，必须跑相关 source tests 和 `idf.py build`，普通 app 改动用 `idf.py -p COM3 app-flash` 验证。

## Idempotence and Recovery

- 如果中途中断，下次从本文的 Owner Boundaries 和 Runtime Data Flow 继续，不要从当前代码现状反推新框架。
- 如果后续识别策略失败，最小回退路径是保留 `qmi8658c/board_imu/imu_service` 采样链路，只替换或关闭 `imu_motion` 规则输出。

## Next Step

- 下一步最小动作：把真实 ADL/FALL 动作样本做离线 replay/evaluator，对比板端 `fall_prob` 与训练仓库评估结果；重点观察 `0.50~0.80` 灰区动作、误报动作和连续窗口推理耗时峰值，再决定是否需要调阈值或加入更细的产品策略。
