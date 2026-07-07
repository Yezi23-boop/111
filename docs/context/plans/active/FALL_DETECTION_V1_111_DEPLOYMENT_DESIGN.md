---
id: plan-fall-detection-v1-111-deployment
tags: plan, active, imu, fall-detection, esp-dl, qmi8658c, event-trigger, post-check, 6ch, 5s-window
summary: Fall Detection V1 在 111 固件中的部署计划，固定 50Hz 六通道 5s 事件窗口、Event Trigger、ESP-DL 模型层、post-check 和本地报警边界。
last_reviewed: 2026-07-08
memory_type: task
scope: imu
status: active
owners: main/services/imu_service.c, main/services/fall_detection_service.c, components/fall_detection_inference, components/imu_sensor, components/qmi8658c, docs/context/plans/active/FALL_DETECTION_V1_111_DEPLOYMENT_DESIGN.md
triggers: FALL_DETECTION_V1_111_DEPLOYMENT_DESIGN, fall detection V1, IMU fall, event trigger, post-check, 5s window, 6ch, input 1500, validate_context light
evidence_level: design
route_area: "IMU fall detection V1"
---

# IMU Fall Detection V1 111 Firmware Deployment Design

## 目标与边界

- 任务目标：在 `D:\esp32S3\111` 主固件仓库中落地 Fall Detection V1 目标链路，包括实时 IMU 事件触发、5 秒事件窗口、ESP-DL 推理、post-check 和本地报警动作。
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

- 当前固件仍在运行旧临时链路：`3ch / 4s / [1,600]` 加速度 CNN，`imu_service` 每约 2 秒发布任意 4 秒滑窗，`fall_detection_service` 直接按 `fall_prob >= 0.80` 确认告警。
- V1 目标链路是 `6ch / 5s / [1,1500]` 事件窗口：先由 Event Trigger 捕获候选，再冻结事件前后窗口，模型只做 Model Check，最后由 post-check 决定是否 ALARM。
- 当前旧链路只吃三轴加速度，缺少陀螺仪 `rad/s` 输入，也缺少 `acc_norm / gyro_norm / jerk_norm` 事件触发和摔后低运动、姿态变化确认。
- 因此静止姿态出现较高 `fall_prob` 时，不应先调阈值当作最终修复；应先补事件触发层和 post-check，使模型回到训练侧设计的使用场景。

## 板端总体流水线

```text
QMI8658C 50Hz acc+gyro
   ↓
单位统一 / 坐标映射
   acc=m/s^2, gyro=rad/s
   ↓
实时 ring buffer
   保存最近 5~8s
   ↓
逐帧派生特征
   acc_norm, gyro_norm, jerk_norm
   ↓
事件触发层
   冲击峰值 / 快速旋转 / 突然加速度变化
   ↓
冻结 5s 事件窗口
   event 前 1.5s + 后 3.5s
   ↓
模型层
   ESP-DL 输出 ADL, FALL
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
  ↓ 5s 窗口可用

MODEL_CHECK
  对 5s 事件窗口执行 ESP-DL 推理
  ↓ fall_prob 达标

POST_CHECK
  继续观察事件后低运动，并比较事件前后姿态变化
  ↓ post-check 通过

ALARM
  执行本地报警动作
```

## 模型输入契约

V1 主线部署 6 通道模型：

```text
accX, accY, accZ,
gyroX, gyroY, gyroZ
```

输入大小：

```text
50Hz * 5s * 6ch = 250 * 6 = 1500
```

单位：

```text
acc  = m/s^2
gyro = rad/s
```

规则层和模型层统一使用同一单位，不在固件里混用 `g` / `dps` 作为主计算单位。

## 当前坐标映射

保持当前已测映射：

```text
model accX  = -chip accX
model accY  =  chip accY
model accZ  = -chip accZ

model gyroX = -chip gyroX
model gyroY =  chip gyroY
model gyroZ = -chip gyroZ
```

gyro 输入模型前转换为 `rad/s`。若未来重新做坐标契约测试，必须先更新测试记录和模型输入契约，再考虑重训或替换模型。

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

窗口固定：

```text
5s = event 前 1.5s + event 后 3.5s
pre_event_frames  = 75
post_event_frames = 175
total_frames      = 250
```

板端 event 点来自实时触发峰值，不来自 WEDA official timestamp。训练侧已经用 `impact_peak` 对齐，目的就是让训练窗口贴近板端事件窗口。

## ESP-DL 模型层

模型接入要求：

```text
- .espdl 嵌入 flash rodata
- input shape = [1, 1500]
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
fall_prob >= recommended_threshold
AND low_motion
AND posture_change

强置信兜底：
fall_prob >= 0.90
AND low_motion
```

`low_motion` 是 V1 进入报警候选的强条件。

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
- 可选震动 / 蜂鸣
- 记录最近一次 fall event snapshot
```

## 接入步骤

1. 从 `D:\esp32S3\imu` 选择已通过样板验证的 `.espdl`。
2. 复制 `.espdl` 到 `111` 的 fall detection inference 模型目录。
3. 更新模型名、SHA256、输入元素数 `1500`、推荐阈值和 label 顺序。
4. 确认模型 test values 已可用于 `Model::test()`。
5. 编译 `D:\esp32S3\111`，不修改 `managed_components`。
6. 板端运行，确认日志包含：

```text
Model::test(): passed
input shape [1,1500]
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
- `[x]` 2026-07-08：实现 Event Trigger：逐帧计算 `acc_norm / gyro_norm / jerk_norm`，用 `A_high=21.57m/s^2`、`G_high=3.84rad/s`、`J_high=5.39m/s^2/frame` 捕获候选事件。
- `[x]` 2026-07-08：冻结 5 秒事件窗口：event 前 75 帧 + event 后 175 帧；`fall_detection_service` 只消费事件窗口，旧模型暂按 legacy 200 帧 / 3ch 加速度输入兼容运行。
- `[ ]` 接入 V1 6ch 输入构造：按坐标映射生成 `[1,1500]`，并将 gyro 从 `deg/s` 转换为 `rad/s`。
- `[ ]` 替换 V1 6ch `.espdl` 资产，更新模型名、SHA256、输入元素数、推荐阈值和 smoke/test values。
- `[ ]` 实现 post-check：低运动 + 姿态变化；普通确认和强置信兜底都必须满足 `low_motion`。
- `[x]` 2026-07-08：默认策略确认：跌倒告警可以上传 `danger alert`，但 `APP_ALERT_SOURCE_FALL_DETECTION` 不播放危险提示音，也不抢占普通音频输出。
- `[ ]` 板端验收静止佩戴、平放、抬腕、翻腕、快速甩手、拍桌/撞表、快速坐下和模拟跌倒。

## 决策记录

- 2026-07-08：`FALL_DETECTION_V1_111_DEPLOYMENT_DESIGN.md` 从训练仓库同步到 `111` active plan；该文档作为 V1 目标态，不代表当前旧 3ch/4s 临时固件已经满足设计。
- 2026-07-08：修复 context 检索路由时保留本文为 active plan，不晋升为 knowledge；后续实现应先读本文，再读当前 IMU runtime plan 和代码。
- 2026-07-08：第一阶段只落地 Event Trigger + 5s 事件窗口，不替换 `.espdl`；旧 3ch / `[1,600]` 模型仅作为临时兼容消费者，避免静止任意滑窗继续直接触发推理。
- 2026-07-08：跌倒告警默认保留 `watch_endpoint_service_post_danger_alert()` 上传，但本机不播放 `audio_alert_player` 的危险提示音；本地仍可保留屏幕/震动告警证据。

## 验证与验收

- context routing：修改本文、`validate_context.py` 或 golden query 后运行 `uv run python scripts/context/validate_context.py --level routing --q "FALL_DETECTION_V1_111_DEPLOYMENT_DESIGN event trigger post-check 5s 6ch" --brief`。
- light 检索：运行 `uv run python scripts/context/validate_context.py --level light --q "FALL_DETECTION_V1_111_DEPLOYMENT_DESIGN event trigger post-check 5s 6ch" --brief`，期望 plans 检索 top1 或 top3 命中本文。
- 固件实现阶段才运行 source tests、`idf.py build` 和 COM7 板端验证；仅规整本文不要求构建固件。

## 幂等与恢复

- 如果中途中断，下次先从 `## 当前实现差距` 和 `## 进度` 未完成项继续，不要把旧 `3ch / 4s` 临时链路当作 V1 完成态。
- 如果 V1 6ch 模型接入失败，保留 IMU 50Hz 采样和旧临时日志链路作为诊断入口，但不得把旧模型阈值调参当作 V1 闭环。

## 下一步

- 下一步最小动作：先实现事件触发与 5 秒事件窗口，再替换 6ch 模型，最后接 post-check 和本地 ALARM。
