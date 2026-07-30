---
id: plan-fall-detection-v1-111-deployment
tags: plan, active, imu, fall-detection, esp-dl, qmi8658c, event-trigger, post-check, 6ch, 6s-window, candidate
summary: Fall Detection V1 在 111 固件中的部署计划，当前 111 固件采用 50Hz 六通道 6s 事件窗口、2s CNN candidate、post-check 严格确认和本地报警边界。
last_reviewed: 2026-07-29
memory_type: task
scope: imu
status: active
owners: main/services/sensors/imu_service.c, main/services/fall_detection_service.c, components/fall_detection_inference, components/imu_sensor, components/qmi8658c, docs/context/plans/active/FALL_DETECTION_V1_111_DEPLOYMENT_DESIGN.md
triggers: FALL_DETECTION_V1_111_DEPLOYMENT_DESIGN, fall detection V1, IMU fall, event trigger, post-check, 6s window, 2s CNN candidate, 6ch, input 600, validate_context light
evidence_level: design
route_area: "IMU fall detection V1"
---

# IMU Fall Detection V1 111 Firmware Deployment Design

## 目标与边界

- 任务目标：在 `D:\esp32S3\111` 主固件仓库中落地 Fall Detection V1 目标链路，包括实时 IMU 事件触发、6 秒长事件窗口、2 秒 CNN candidate、post-check 和本地报警动作。
- 为什么现在做：当前固件已有 50Hz IMU 采样和旧 3ch/4s CNN 临时部署，但静止姿态会被任意滑窗直接送入模型，和训练侧 V1 事件窗口设计不一致。
- 完成后用户会看到什么变化：普通静止、平放、翻腕和强动态 ADL 不会因为单个滑窗高分直接报警；只有事件触发、模型层和 post-check 都满足时才进入 ALARM。
- 本文档是部署设计，不记录 WEDA 数据清洗、训练细节和模型选择实验。训练侧内容见：

```text
D:\esp32S3\imu\FALL_DETECTION_V1_TRAINING_MODEL_DESIGN.md
```

## 接入边界与非目标

- 不修改 `D:\esp32S3\111\managed_components`。
- 不把完整训练集或训练脚本复制进 `111`。
- `111` 只接收已验证的 `.espdl`、meta、阈值、少量 smoke/test values 和模型输入契约。
- V1 不提供用户取消机会，不实现倒计时。
- V1 报警只做本地事件：串口日志、屏幕提示、可选震动/蜂鸣、event snapshot。
- V1 不接电话、短信、云端通知或外部救援流程。

## 当前实现差距

- 当前固件已部署 recall90 调试 CNN `6ch / 2s / [1,600]` 模型，`imu_service` 只在 Event Trigger 后发布 300 帧长事件窗口；`fall_detection_service` 只提取事件附近 100 帧送入模型。
- 当前已修复背面朝上误告警的直接路径：`flags=0` 定期窗口不再发布，也不会参与推理、确认或清除；本地告警/红屏确认后最多保持 5 秒，之后由 `fall_detection_service` 自动 clear。
- 当前已经让模型退回 candidate 生成器：`fall_prob>=0.60` 只进入候选，只有 post-check 通过后才确认、本地红屏和上传 `danger alert`。
- 当前仍缺少板端行为验收：静止佩戴、平放、小幅旋转、拍桌/撞表和模拟跌倒需要用日志确认 candidate 与 post-check 的过滤效果。

## 板端总体流水线

```text
QMI8658C 50Hz acc+gyro
   ↓
单位统一 / 坐标映射
   acc=m/s^2, gyro=rad/s
   ↓
实时 ring buffer
   保存最近 6s
   ↓
逐帧派生特征
   acc_norm, gyro_norm, jerk_norm
   ↓
事件触发层
   冲击峰值 / 快速旋转 / 突然加速度变化
   ↓
冻结 6s 事件窗口
   event 前 1s + 后 5s
   ↓
模型层
   提取 event 前 0.7s + 后 1.3s 的 2s 子窗口
   ESP-DL 输出 ADL, FALL candidate
   ↓
post-check
   跌倒后低运动 + 姿态变化
   ↓
ALARM
   本地报警动作
```

## 状态机

```text
NORMAL
  维护 ring buffer，计算 acc_norm / gyro_norm / jerk_norm
  ↓ 事件触发

EVENT_TRIGGERED
  记录 event_index，等待 post frames 收满
  ↓ 6s 长窗口可用

MODEL_CHECK
  对事件附近 2s 子窗口执行 ESP-DL 推理
  ↓ fall_prob 达标

POST_CHECK
  基于同一 6s 长窗口计算事件后低运动，并比较事件前后姿态变化
  ↓ post-check 通过

ALARM
  执行本地报警动作
```

## 模型输入契约

当前 111 固件部署 6 通道 2s CNN recall90 调试模型：

```text
accX, accY, accZ,
gyroX, gyroY, gyroZ
```

输入大小：

```text
50Hz * 2s * 6ch = 100 * 6 = 600
```

单位：

```text
acc  = m/s^2
gyro = rad/s
```

规则层和模型层统一使用同一单位，不在固件里混用 `g` / `dps` 作为主计算单位。

## 当前坐标映射

`imu_service` 输出已经是当前板修正后的右手系物理轴，不再等同于原始 raw chip 轴；后续实现禁止把旧 `-chip X` 映射再次套到模型输入上。

保持当前模型输入映射：

```text
model accX  =  imu accX
model accY  =  imu accY
model accZ  = -imu accZ

model gyroX =  imu gyroX
model gyroY =  imu gyroY
model gyroZ = -imu gyroZ
```

其中 `imu` 指 `imu_service` 发布的修正后板级坐标：`+X` 朝手表顶部，`+Y` 朝手表右侧，`+Z` 朝表背/向下。当前模型输入仅对 Z 轴取反，gyro 输入模型前必须从 `deg/s` 转换为 `rad/s`。若未来重新做坐标契约测试，必须先更新测试记录和模型输入契约，再考虑重训或替换模型。

## 事件触发层

派生特征：

```text
acc_norm  = sqrt(accX^2 + accY^2 + accZ^2)
gyro_norm = sqrt(gyroX^2 + gyroY^2 + gyroZ^2)
jerk_norm = abs(acc_norm[t] - acc_norm[t-1])
```

初始阈值建议：

```text
A_high = 21.57 m/s^2
G_high = 3.84 rad/s
J_high = 5.39 m/s^2/frame

trigger =
    acc_norm > A_high
    OR jerk_norm > J_high
    OR (gyro_norm > G_high AND jerk_norm > 2.45 m/s^2/frame)
```

这些阈值必须由训练仓库的 WEDA 统计脚本给出最终推荐值后再固化到 `111`。事件触发层只负责抓候选，不负责最终报警。

## 事件窗口

当前固件窗口固定：

```text
6s = event 前 1s + event 后 5s
pre_event_frames       = 50
post_event_frames      = 250
total_frames           = 300
trigger_frame_index    = 50
model_subwindow_frames = [trigger-35, trigger+64]
```

板端 event 点来自实时触发峰值，不来自 WEDA official timestamp。当前 6s 长窗口服务 post-check，模型仍只消费事件附近 100 帧 2s 子窗口。

## ESP-DL 模型层

模型接入要求：

```text
- .espdl 嵌入 flash rodata
- input shape = [1, 600]
- output shape = [1, 2]
- label order = [ADL, FALL]
- Model::test() 通过
- get_memory_info() internal RAM < 20KB
```

阈值来自训练仓库 testing threshold scan，不固定为 `0.50`。

模型通过线：

```text
- ESP-DL internal RAM < 20KB
- Model::test() 通过
- FALL precision >= 95%
- all ADL false positives <= 5~10
```

## post-check

post-check 在模型输出 candidate 后执行，判断摔后状态是否成立。Model Check 提供候选证据，不直接报警。

V1 不使用四元数和姿态融合。姿态变化直接用事件前后平均加速度向量估计重力方向变化。

初始建议：

```text
post_window = 3s

low_motion =
    gyro_norm_mean < 0.44 rad/s
    AND gyro_norm_max < 1.40 rad/s
    AND acc_norm_std < 1.96 m/s^2

posture_change =
    angle(pre_gravity, post_gravity) > 45 deg
```

其中：

```text
pre_gravity  = 事件前 1s 平均 acc 向量
post_gravity = 事件后 2~3s 平均 acc 向量
angle = acos(dot(pre, post) / (|pre| * |post|))
```

确认策略：

```text
普通确认：
fall_prob >= FALL_MODEL_THRESHOLD_DEFAULT
AND low_motion
AND posture_change

强置信兜底：
fall_prob >= 0.90
AND low_motion
```

`fall_prob >= 0.60` 只生成 candidate；`low_motion` 是 V1 进入确认报警的强条件。

时间窗口口径：低运动统计覆盖事件后约 2~5s；姿态变化使用事件前 1s 与事件后 2~3s 的平均加速度向量估计重力方向变化。

## 报警层

V1 不提供取消机会，不设置倒计时。

```text
ALARM:
  post-check 通过后执行本地报警动作
```

V1 本地报警动作：

```text
- 串口日志打印 FALL alarm
- 屏幕提示 FALL
- 本地红屏/告警保持 5 秒后自动退出
- 可选震动 / 蜂鸣
- 记录最近一次 fall event snapshot
```

## 接入步骤

1. 从 `D:\esp32S3\imu` 选择已通过样板验证的 `.espdl`。
2. 复制 `.espdl` 到 `111` 的 fall detection inference 模型目录。
3. 更新模型名、SHA256、输入元素数、推荐阈值和 label 顺序。
4. 确认模型 test values 已可用于 `Model::test()`。
5. 编译 `D:\esp32S3\111`，不修改 `managed_components`。
6. 板端运行，确认日志包含：

```text
Model::test(): passed
input shape [1,600]
output shape [1,2]
ESP-DL internal RAM < 20KB
adl_prob / fall_prob / threshold / infer_ms
event trigger / post-check / ALARM 状态
```

## 板端验收

行为测试至少覆盖：

```text
- 静止佩戴
- 平放
- 抬腕
- 翻腕
- 快速甩手
- 拍桌 / 撞表
- 快速坐下
- 模拟跌倒
```

验收重点：

```text
- 非摔倒强动态只允许进入 candidate，不应轻易 ALARM
- 模拟跌倒应能触发 event，模型给出较高 fall_prob
- post-check 应能过滤恢复运动明显的误触发
- ALARM 只在模型层和 post-check 都通过后出现
```

## 与训练仓库的交付接口

训练仓库交付给 `111` 的最小资产：

```text
model.espdl
model.info
model_export_manifest.json
local_resource_estimate.json
metrics.json
dataset_manifest.json 摘要
模型 SHA256
推荐 threshold
输入契约说明
smoke FALL / ADL 样本
```

`111` 不保存完整训练集。

## 进度

- `[x]` 已有 QMI8658C/imu_sensor/imu_service 50Hz 物理六轴采样基础，当前采样可提供 `m/s^2` 与 `deg/s`，V1 输入构造时必须将 gyro 转换为 `rad/s`。
- `[x]` 已有旧 `3ch / 4s / [1,600]` ESP-DL 临时部署和日志闭环，用于验证 loader、PSRAM 缓冲和告警通路。
- `[x]` 已确认当前临时链路会把任意静止滑窗送入模型，和 V1 事件窗口设计不一致。
- `[x]` 2026-07-08：`imu_service` 改为事件触发后发布 50Hz / 5s / 250 帧事件窗口，窗口 payload 保留 acc+gyro 物理量；本阶段仍不替换模型资产。
- `[x]` 2026-07-08：实现 Event Trigger：逐帧计算 `acc_norm / gyro_norm / jerk_norm`；当前固件阈值已下调为 `A_high=15.0m/s^2`、`G_high=2.5rad/s`、`J_high=3.5m/s^2/frame`，用于提高事件窗口召回。
- `[x]` 2026-07-08：冻结 5 秒事件窗口：event 前 75 帧 + event 后 175 帧；`fall_detection_service` 只消费事件窗口，旧模型暂按 legacy 200 帧 / 3ch 加速度输入兼容运行。
- `[x]` 2026-07-08：接入 V1 6ch 输入构造：`fall_detection_fill_model_input()` 改为 250帧×6ch 布局，gyro 从 `deg/s` 转换为 `rad/s`，坐标映射 `[+X,+Y,-Z,+gx,+gy,-gz]`。
- `[x]` 2026-07-08：历史 RF4s 部署：`tcn_v1_rf4s_6ch_5s_with_test.espdl`（SHA256=4566d13b...），`FALL_MODEL_INPUT_ELEMENTS` 改为 1500，阈值为 0.50；COM7 板端 `Model::test()` 通过，internal RAM=32KB。
- `[x]` 2026-07-08：部署 RF5s retry 6ch/5s 模型 `tcn_v1_rf5s_6ch_5s_with_test.espdl`（SHA256=105b389d696c649114fd4fa520ab57cc626d772489ed31df9281b9d40d8df0ca），输入仍为 `[1,1500]`，来源为 `weda_v3_event5s_6ch_tcn_c16_k3_rf5s_pool25_e500_strongaug_full_retry`。
- `[x]` 2026-07-08：按调试需求将 RF5s 默认 FALL 告警阈值从 `0.85` 降为 `0.65`，让中等置信事件更容易进入本地告警/上传路径；后续量产阈值需结合 ADL 误报日志回调。
- `[x]` 2026-07-08：按用户要求清理旧模型资产，`components/fall_detection_inference/models/esp32s3/` 只保留当前 RF5s `.espdl` 和 meta；旧 3ch CNN 与 RF4s `.espdl` 已删除。验证：fall source tests 16 passed，meta JSON 解析通过，`idf.py build` 通过。
- `[x]` 2026-07-08：修正 V1 输入契约文档和代码注释：当前输入来自修正后右手系 `imu_service` 板级坐标，禁止继续按旧 raw chip 轴理解或额外套 `-chip X`。
- `[x]` 2026-07-08：修正 `imu_service_accel_window_t` 注释契约：5s 事件窗口发布当前板级右手系物理轴语义，Fall V1 消费方只负责 Z 轴取反和 gyro `rad/s` 转换。
- `[x]` 2026-07-08：修复背面朝上误告警路径：删除 `imu_service` 定期窗口发布，`fall_detection_service` 拒绝 `flags=0` 非事件窗口；只有 `flags!=0` 的 Event Trigger 窗口可进入模型推理和确认。
- `[x]` 2026-07-08：本地告警/红屏确认后最多保持 5 秒；`fall_detection_service` 通过 1 秒 queue timeout 和每次推理后的超时检查自动 clear，后续低风险事件窗口仍可提前 clear。
- `[x]` 2026-07-08：新增 `fall_detection_service_destroy()` 对外一键销毁入口；销毁只断开 fall 模型窗口队列，不停止 `imu_service` 后台采样；运行中的 `fall_detect` task 进入 `STOPPING` 并在安全点释放 ESP-DL runner、static queue、queue storage、current window 和 model input PSRAM 缓冲。
- `[x]` 2026-07-08：部署 recall90 调试 CNN `cnn_v1_recall90_6ch_2s_with_test.espdl`（SHA256=cbe18c7e089bac506ff5229f2ed8c4b728148df5902dce9527aad3a315504684），输入改为 `[1,600]`；`imu_service` 事件窗口同步为 event 前 35 帧 + event 后 65 帧，默认 FALL 阈值为 `0.30`。
- `[x]` 2026-07-19：调试期将当前 2s CNN 默认 FALL 阈值锁定为 `0.60`，并收紧 Event Trigger：`jerk` 不再单独触发，必须同时满足 `acc_norm>16.0m/s^2` 或 `gyro_norm>3.0rad/s`；source tests 已同步当前阈值契约。
- `[x]` 2026-07-19：让模型从报警决策者退回 candidate 生成器。`imu_service` 事件窗口扩为 50Hz / 6s / 300 帧，`fall_detection_fill_model_input()` 只提取 `[trigger-35, trigger+64]` 的 100 帧 2s 子窗口；`fall_detection_service` 新增 post-check，普通确认要求 `fall_prob>=0.60 && low_motion && posture_change`，强置信兜底要求 `fall_prob>=0.90 && low_motion`，post-check 失败不红屏、不上传。
- `[x]` 2026-07-19：进一步优化 candidate + post-check 可维护性：推理前显式校验基础窗口、模型子窗口和 post-check 区间契约；`post检查表` 增加触发帧 `acc/gyro/jerk` 强度，便于板端按误触发原因回看。
- `[x]` 2026-07-08：默认策略确认：跌倒告警可以上传 `danger alert`，但 `APP_ALERT_SOURCE_FALL_DETECTION` 不播放危险提示音，也不抢占普通音频输出。
- `[x]` 2026-07-29：按用户要求随 IMU 将 Fall runtime 改为开机默认关闭，避免 IMU 不采样时仍加载 ESP-DL runner 和 PSRAM 窗口缓冲；默认启动时不应出现模型加载、candidate、post-check、红屏或 danger alert 上传。
- `[x]` 2026-07-29：Fall runtime 的动态关闭沿用 `fall_detection_service_destroy()`；关闭 IMU/Fall 时先断开并销毁 Fall 模型运行时，再调用 `imu_service_destroy()` 释放采样任务、GPIO ISR 和 PSRAM 缓冲，避免消费者仍访问 IMU 窗口。
- `[ ]` 板端验收静止佩戴、平放、抬腕、翻腕、快速甩手、拍桌/撞表、快速坐下和模拟跌倒。

## 决策记录

- 2026-07-08：`FALL_DETECTION_V1_111_DEPLOYMENT_DESIGN.md` 从训练仓库同步到 `111` active plan；该文档作为 V1 目标态，不代表当前旧 3ch/4s 临时固件已经满足设计。
- 2026-07-08：修复 context 检索路由时保留本文为 active plan，不晋升为 knowledge；后续实现应先读本文，再读当前 IMU runtime plan 和代码。
- 2026-07-08：第一阶段只落地 Event Trigger + 5s 事件窗口，不替换 `.espdl`；旧 3ch / `[1,600]` 模型仅作为临时兼容消费者，避免静止任意滑窗继续直接触发推理。
- 2026-07-08：跌倒告警默认保留 `watch_endpoint_service_post_danger_alert()` 上传，但本机不播放 `audio_alert_player` 的危险提示音；本地仍可保留屏幕/震动告警证据。
- 2026-07-08：当前 IMU 输出已按板级右手系修正，V1 输入构造不再使用旧 raw chip 轴描述；保持数值映射 `[+X,+Y,-Z,+gx,+gy,-gz]` 和 gyro `rad/s` 单位。
- 2026-07-08：`imu_service` 事件触发阈值以当前源码为准：`A_high=15.0m/s^2`、`G_high=2.5rad/s`、`J_high=3.5m/s^2/frame`；文档中的 `21.57/3.84/5.39` 保留为初始建议值，不代表当前固件。
- 2026-07-08：RF5s retry 模型阈值采用训练 run 的 `weda_v3_threshold_selection.json` 推荐值 `0.85`，对应验证集 `fall_precision=0.9508`、`fall_recall=0.7733`、ADL false positives=3；清除阈值 `k_fall_clear_threshold=0.50` 仍只用于已确认后的恢复证据。
- 2026-07-08：调试期将当前固件默认 FALL 阈值改为 `0.65`，偏离训练 run 推荐值 `0.85`；这会提高召回和告警触发率，也会提高 ADL 误报风险。
- 2026-07-08：删除定期窗口推理路径；`imu_service` 不再发布 `flags=0` 周期窗口，`fall_detection_service` 也会拒绝任何 `flags=0` 非事件窗口。背面朝上等姿态误判应先被 Event Trigger / flags 门控挡住，后续 post-check 再做第二层过滤。
- 2026-07-08：本地告警/红屏不再依赖后续窗口退出，确认后最多保持 5 秒；App danger alert 可上传一次，本机危险语音继续跳过。
- 2026-07-08：Fall 模型运行时生命周期由 `fall_detection_service` 自己持有；外部只能调用 `fall_detection_service_destroy()` 表达销毁意图，禁止 UI 或其他 owner 直接删除 `fall_detect` task、释放 runner 或停止 `imu_service` 后台采样。
- 2026-07-08：按调试召回优先路线部署 2s/6ch CNN recall90 模型，临时偏离原 5s RF5s 目标态。该模型阈值 `0.30` 来自训练 run 的 `validation_selected_threshold`：验证集 `fall_recall=1.0`、ADL false positives=1/245；测试集 `fall_recall=0.9333`、ADL false positives=26/712，误报风险高于 RF5s，需要板端 ADL/FALL 日志回调。
- 2026-07-19：当前固件实际确认阈值以源码为准，`FALL_MODEL_THRESHOLD_DEFAULT=0.60`，不再采用 2s CNN 训练侧 recall90 阈值 `0.30` 作为固件默认。Event Trigger 实际阈值以 `main/services/sensors/imu_service.c` 为准：`A_high=25.0m/s^2`、`G_high=5.0rad/s`、`J_high=10.0m/s^2/frame`、`GJ_min=5.0m/s^2/frame`；`JERK_HIGH` 需要额外满足 `acc_norm>16.0m/s^2` 或 `gyro_norm>3.0rad/s`。
- 2026-07-19：当前确认策略改为严格确认：模型输出只作为 `fall candidate`，不再直接触发本地红屏或 App 上传；post-check 使用事件前 1s 平均重力、事件后 2~3s 平均重力和事件后 2~5s 低运动统计。严格确认会引入约 5 秒事件后确认延迟，这是当前误报优先收敛的取舍。
- 2026-07-29：当前 Fall V1 逻辑仍保留在源码中，但默认不随 deferred services 启动；这是启动/资源策略变化，不改变模型输入、Event Trigger、candidate + post-check 或 danger alert 静音上传策略。
- 2026-07-29：默认关闭不是编译期屏蔽；`app_main` 通过生命周期函数选择启动/销毁。运行时关闭必须先 `fall_detection_service_destroy()` 后 `imu_service_destroy()`，重新启动需等待 IMU owner task 完成销毁并回到 `STOPPED`。

## 验证与验收

- context routing：修改本文、`validate_context.py` 或 golden query 后运行 `uv run python scripts/context/validate_context.py --level routing --q "FALL_DETECTION_V1_111_DEPLOYMENT_DESIGN event trigger post-check 5s 6ch" --brief`。
- light 检索：运行 `uv run python scripts/context/validate_context.py --level light --q "FALL_DETECTION_V1_111_DEPLOYMENT_DESIGN event trigger post-check 5s 6ch" --brief`，期望 plans 检索 top1 或 top3 命中本文。
- 固件实现阶段才运行 source tests、`idf.py build` 和 COM7 板端验证；仅规整本文不要求构建固件。
- 2026-07-08 RF5s retry 板端验证：`board_logs/2026-07-08-16-39-22-fall-rf5s-deploy.log` 显示 `tcn_v1_rf5s_6ch_5s` 加载为 `shape=[1, 1500]`、`threshold=0.85`，`dl::Model: Test Pass!`；静止窗口判定 ADL，`fall_prob=0.0000`，推理约 8.9~10.9ms，无 panic。
- 2026-07-08 删除定期窗口与 5 秒本地告警退出验证：source tests 16 passed；`idf.py build` 通过；context standard 错误 0、警告 0；COM7 `board_logs/2026-07-08-17-16-14-fall-no-periodic-5s-clear.log` 显示 `定期窗口表=0`、fall/imu `flags=0x00=0`、确认后约 5 秒 clear，且 `panic_log_seen=0`。完整静止佩戴、背面朝上、快速翻腕和模拟跌倒分场景验收仍需补采。
- 2026-07-08 调试阈值 `0.65` 验证：fall source tests 16 passed；`idf.py build` 通过；COM7 `board_logs/2026-07-08-17-39-30-fall-threshold-065.log` 显示 `threshold=0.65`、`Model: Test Pass!`、`panic_log_seen=0`；context standard 错误 0、警告 0。
- 2026-07-08 一键销毁入口验证：`uv run python -m unittest tests.test_fall_detection_service_source tests.test_fall_detection_inference_source tests.test_imu_service_source` 通过 17 tests；`git diff --check` 无 whitespace error；`idf.py build` 通过，`111.bin` `0xace0b0`，app free `0x331f50`/23%。板端实际点击销毁/重启模型的 RAM 日志仍需补采。
- 2026-07-08 2s CNN recall90 部署验证：`uv run python -m unittest tests.test_fall_detection_inference_source tests.test_fall_detection_service_source tests.test_imu_service_source` 通过 17 tests；`git diff --check` 无 whitespace error；`idf.py build` 通过，`111.bin` `0xac65e0`，app free `0x339a20`/23%。当前机器仅发现 `COM1`，未执行板端 `app-flash-monitor`；接板后需补 `Model::test()`、RAM、静止 ADL 和模拟 FALL 日志。
- 2026-07-19 jerk 组合触发验证：`uv run python -m unittest tests.test_fall_detection_inference_source tests.test_imu_service_source tests.test_fall_detection_service_source` 通过 17 tests；`git diff --check` 无 whitespace error；`idf.py build` 通过，`111.bin=0xac6640`，app free `0x3399c0`/23%。板端仍需按 `flags=0x01/0x02/0x04` 分类采静止、小幅旋转、敲击、模拟跌倒日志。
- 2026-07-19 candidate + post-check 实现验证：`uv run python -m unittest tests.test_fall_detection_inference_source tests.test_imu_service_source tests.test_fall_detection_service_source` 通过 17 tests；`git diff --check` 无 whitespace error；`idf.py build` 通过，`111.bin=0xac6e80`，app free `0x339180`/23%；context standard 错误 0、警告 0。板端仍需验证 candidate 出现但 post-check 失败时不红屏、不上传，以及模拟跌倒能通过 `low_motion` 与姿态变化确认。
- 2026-07-19 candidate + post-check 进一步优化验证：source tests 17 passed；`git diff --check -- main/services/fall_detection_service.c tests/test_fall_detection_service_source.py` 无 whitespace error；`idf.py build` 通过，`111.bin=0xac6f40`，app free `0x3390c0`/23%；context standard 错误 0、警告 0。
- 2026-07-29 默认关闭验证：IMU/Fall 启动契约与日志降噪 source tests 44 passed；`git diff --check` 无 whitespace error；`idf.py build` 通过，`111.bin=0xabd730`，app free `0x3428d0`/23%。板端仍需复验默认不加载 Fall 模型，且没有 candidate/post-check/红屏/上传。

## 幂等与恢复

- 如果中途中断，下次先从 `## 当前实现差距` 和 `## 进度` 未完成项继续，不要恢复旧周期滑窗推理路径。
- 如果 V1 6ch 模型接入失败，保留 IMU 50Hz 采样和旧临时日志链路作为诊断入口，但不得把旧模型阈值调参当作 V1 闭环。

## 下一步

- 下一步最小动作：先板端确认默认启动不加载 Fall runtime；需要继续调试跌倒识别时，临时打开 `app_main.c` 中 IMU/Fall 受控启动块，再采集 2s CNN candidate + 6s post-check 的静止佩戴、平放、小幅旋转、快速翻腕、拍桌/撞表、快速坐下和模拟跌倒日志。
