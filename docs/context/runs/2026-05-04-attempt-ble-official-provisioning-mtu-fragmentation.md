---
id: attempt-2026-05-04-ble-official-provisioning-mtu-fragmentation
tags: ble, provisioning, miniapp, attempt-log
summary: ble-official-provisioning-mtu-fragmentation；结果：success。
last_reviewed: 2026-05-04
garden_status: keep-evidence
garden_reviewed: 2026-05-16
memory_type: episodic
scope: task
owners: C:/Users/ye/Desktop/eps32_ble, docs/context/knowledge/project/wechat-miniapp-official-ble-provisioning.md, docs/context/knowledge/project/ble-provisioning-miniapp-write-fragmentation.md
triggers: BLE provisioning MTU protobuf prov-scan fragmentation
evidence_level: observed
route_area: "BLE miniapp provisioning evidence"
---

# Attempt Log: ble-official-provisioning-mtu-fragmentation

## 背景

- 本次要验证什么：记录微信小程序官方 BLE provisioning 客户端的 MTU 与 payload 边界，避免后续把官方 protobuf 请求按旧 JSON GATT 分片。
- 对应任务或计划：official BLE provisioning miniapp reliability
- 结果状态：success

## 环境

- 分支/工作区状态：未记录
- 设备/串口/板型：未涉及或未记录
- 关键前置条件：未记录

## 操作

- 修改过的文件或 owner：
- C:/Users/ye/Desktop/eps32_ble
- docs/context/knowledge/project/wechat-miniapp-official-ble-provisioning.md
- docs/context/knowledge/project/ble-provisioning-miniapp-write-fragmentation.md
- 执行的命令或动作：
- 对齐官方 proto-ver / prov-session / prov-scan / prov-config protocomm BLE client
- 小程序侧增加 wx.setBLEMTU() 协商和发送前 payload 上限保护
- prov-scan 结果分批读取，避免忙碌环境下 BLE payload 溢出
- 已尝试但不应直接重复的路径：
- 不要把官方 protobuf transaction 按旧 JSON GATT 文本协议拆分
- 不要在 AP 很多时一次性塞完整 scan result

## 观测

- 关键日志/证据：
- 内存记录指出官方 protocomm BLE writes fail 或 scan results break 的根因是沿用旧 JSON-path fragmentation 假设
- 与预期不一致的点：
- 未记录

## 结论

- 本次可以确认的事实：官方 BLE provisioning 请求应按 protocomm/protobuf 交易处理；旧自定义 JSON GATT 只作为历史镜像理解。
- 仍然不能确认的事实：
- 不同手机 BLE 栈的实际 MTU 协商结果仍需真机覆盖

## 未验证风险

- 下一轮仍需补证据的边界：
- BLE 配网异常先查 MTU、payload 上限和 prov-scan batch，而不是重写固件端 GATT 协议
