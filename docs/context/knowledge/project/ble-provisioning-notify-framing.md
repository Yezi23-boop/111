---
id: ble-provisioning-notify-framing
tags: [project, ble, provisioning, notify, framing, wechat, miniapp]
summary: 记录当前仓库 BLE 配网 notify 回包需要显式补换行分隔，否则微信小程序在连续 notify 场景下可能把多条 JSON 粘连后解析失败。
last_reviewed: 2026-04-08
memory_type: semantic
scope: repo
owners: components/network_provisioning_adapter, components/ble_control
triggers: ble, provisioning, notify, framing
evidence_level: observed
---

# BLE 配网 notify 分帧

## 结论

- 当前仓库 BLE 配网的上行回包不能只发送裸 JSON。
- 在微信小程序连续接收 `hello`、`status`、`wifi_scan started/batch/done` 多条 notify 时，若每条消息之间没有分隔符，小程序可能把多条 JSON 粘在一起，最终表现为：
  - 板端日志已经出现多次 `notify`
  - 小程序仍提示“设备暂未返回 wifi_scan 回包”
- 当前修复策略是：
  - 设备端 notify 时为每条 JSON 追加 `\n`
  - 使用 `ble_gatts_notify_custom()` 直接发送带分隔符的 mbuf
  - 再按 `20` 字节安全分片发送每条上行消息，避免微信侧未协商更大 MTU 时收不到完整 JSON

## 适用现象

- 串口能看到：
  - `BLE notify=1`
  - `收到 BLE Wi-Fi 扫描请求`
  - `BLE Wi-Fi 扫描完成`
  - 一串 `GATT procedure initiated: notify`
- 但小程序端没有进入：
  - `收到 Wi-Fi 列表 batch`
  - 或 `Wi-Fi 扫描完成`

## 原因

- 小程序侧 notify 解析器允许两种输入：
  - 单条完整 JSON
  - 多条以 `\n` 分隔的 JSON
- 设备端此前直接用 `ble_gatts_notify()` 从 characteristic 当前值发裸 JSON，没有显式分隔符。
- 当多个 notify 在短时间内送达时，小程序可能收到拼接后的文本流，导致 JSON 解析失败。
- 即使已经补了 `\n`，若单条 notify 长度仍超过微信侧当前可稳定接收的 payload，大概率仍只会得到残片，导致小程序一直等不到完整回包。

## 对后续 agent 的建议

- 若 BLE 上行继续扩展更多事件，默认保持：
  - 一条事件 = 一条以 `\n` 结尾的 JSON
  - 设备端按 `20` 字节安全分片发送
- 若后续再出现“串口明确 notify，但小程序提示未返回回包”，优先检查：
  - 设备端 notify 是否仍保留 `\n`
  - 设备端上行是否仍按 `20` 字节分片
  - 小程序是否仍在按换行拆帧
