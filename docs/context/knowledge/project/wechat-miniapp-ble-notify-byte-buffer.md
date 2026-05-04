---
id: wechat-miniapp-ble-notify-byte-buffer
tags: [project, wechat, miniapp, ble, notify, parser, wifi-scan]
summary: 记录微信小程序侧 BLE notify 需要按字节缓冲拆帧，而不是按字符串直接拼接，否则在 ESP32 按 20 字节分片上行时容易一直等不到 hello/status/wifi_scan 完整回包。
last_reviewed: 2026-04-08
memory_type: semantic
scope: repo
owners: components/network_provisioning_adapter, components/ble_control
triggers: wechat, miniapp, ble, notify, byte, buffer
evidence_level: observed
---

# 微信小程序 BLE notify 字节缓冲拆帧

## 结论

- 当前小程序侧不能再把 BLE notify 当作“每次一定是一条完整字符串”来处理。
- 当 ESP32 端把 `hello`、`status`、`wifi_scan started/batch/done` 按 `20` 字节安全分片上行时，小程序需要：
  - 先按 `Uint8Array` 追加到字节缓冲
  - 以 `\n` 作为消息边界拆帧
  - 只在拿到完整帧后再做 `UTF-8` 解码和 `JSON.parse`

## 适用现象

- 板端串口已经能看到：
  - `BLE notify=1`
  - `收到 BLE Wi-Fi 扫描请求`
  - `BLE Wi-Fi 扫描完成`
  - 多次 `GATT procedure initiated: notify`
- 小程序却仍提示：
  - `设备暂未返回 hello/status/wifi_scan 回包`
  - 或日志里没有任何 `收到设备消息: ...`

## 原因

- 旧实现先把每次 notify 直接解码成字符串，再做文本拼接。
- 当上行消息在 BLE 层被拆成多个片段时，旧实现可能把半条 UTF-8 或半条 JSON 当作完整文本处理，导致：
  - 一直拼不出完整 JSON
  - 旧会话残片污染下一次连接

## 当前仓库的修复要点

- 小程序页 `index.js` 中引入 `notifyByteBuffer: Uint8Array`
- `characteristicValueHandler` 改为把原始 `ArrayBuffer` 交给 `consumeNotifyValue()`
- `consumeNotifyValue()` 逻辑固定为：
  - 字节缓冲追加
  - 找 `0x0A` 换行
  - 提取完整帧
  - 完整帧再 `decodeUtf8()` 和 `applyIncomingMessage()`
- 连接开始前、连接重置时、页面卸载时都必须同时清空：
  - `notifyByteBuffer`
  - `notifyTextBuffer`

## 对后续 agent 的建议

- 若后续仍扩展 BLE 上行事件，优先保持：
  - 设备端一条 JSON + `\n`
  - 小程序端字节缓冲拆帧
- 若再次遇到“板端 notify 明明已发，小程序却仍超时”，先查：
  - `notifyByteBuffer` 是否还存在
  - 连接/断开时是否清空了旧缓冲
  - 小程序日志里是否已经出现 `收到设备消息: ...`
