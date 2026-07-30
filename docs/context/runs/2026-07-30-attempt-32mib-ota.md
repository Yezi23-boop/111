---
id: attempt-2026-07-30-32mib-ota
tags: ota, partition-table, rollback, 32mb
summary: 32MiB 双槽 OTA 分区构建验证；结果：success。
last_reviewed: 2026-07-30
memory_type: episodic
scope: task
status: active
result: success
owners: partitions.csv, sdkconfig, sdkconfig.defaults, CMakeLists.txt, main/features/danger_detection/danger_detection_service.c
triggers: OTA dual slot partition rollback 32MB
evidence_level: observed
record_reasons: route-choice, evidence
force_reason:
---

# Attempt Log: 32MiB 双槽 OTA 分区构建验证

## 背景

- 本次要验证什么：32MiB 双槽 OTA 分区构建验证
- 对应任务或计划：未绑定计划
- 结果状态：success
- 长期记录理由：route-choice, evidence

## 环境

- 分支/工作区状态：未记录
- 设备/串口/板型：未涉及或未记录
- 关键前置条件：未记录

## 操作

- 修改过的文件或 owner：
- partitions.csv
- sdkconfig
- sdkconfig.defaults
- 执行的命令或动作：
- 32MiB Flash 上使用 ota_0/ota_1 各 12MiB、otadata 8KiB 的自定义分区表，并执行 idf.py fullclean + idf.py build
- 固定危险检测为 ESP-DL，使用 EXCLUDE_COMPONENTS 将 traffic_inference 及其 Edge Impulse 模型移出正式构建图
- 已尝试但不应直接重复的路径：
- 直接 app-flash 或保留旧分区数据后覆盖新 partition table；旧数据地址已变化

## 观测

- 关键日志/证据：
- COM7 esptool flash_id 实测 32MB；移除 traffic_inference 后 111.bin=0xAAF1B0；最小 OTA 分区=0xC00000，剩余 0x150E50 (11%)；build_components 与链接 map 均无 traffic_inference；idf.py build 通过
- 与预期不一致的点：
- 未记录

## 结论

- 本次可以确认的事实：32MiB 可容纳 12MiB 双 OTA 槽，但数据分区预算仅余 7.875MiB；当前 resources 固定 4MiB、assets 保留 2MiB，audio 缩为 1MiB、model 缩为 896KiB。正式固件已固定使用 ESP-DL，Edge Impulse 交通声音组件和 manual_v7_1s 模型不再编译或链接。
- 仍然不能确认的事实：
- 尚未实现独立 ota_service；缩减后的 audio/model 是否满足未来产品容量需求尚未板端验证

## 未验证风险

- 下一轮仍需补证据的边界：
- 实现独立 HTTPS ota_service 与启动确认；经用户确认后采用完整分区刷写和数据迁移/清空策略进行板端验证
