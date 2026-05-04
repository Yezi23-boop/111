---
id: ble-provisioning-terminal-notify-flush-window
tags: [project, ble, provisioning, wechat, notify, timing]
summary: 记录 BLE 配网在返回终态 `connected` 后若立即关闭 transport，可能导致微信小程序收不到最后一条成功通知；当前固件通过短暂延迟 stop 解决。
last_reviewed: 2026-04-02
memory_type: semantic
scope: repo
owners: components/network_provisioning_adapter, components/ble_control
triggers: ble, provisioning, terminal, notify, flush, window
evidence_level: observed
---

# BLE 配网终态通知冲刷窗口

## 结论

- 当前仓库的 BLE 配网在 `WIFI_STATE_CONNECTED` 时，会先通过 notify 返回：
  - `{"evt":"status","state":"connected","ssid":"...","ip":"..."}`
- 如果固件在发送这条 notify 后立即调用 `ble_provision_transport_stop()`，微信小程序侧存在收不到最终成功回包的风险。

## 当前处理方式

- `components/wifi_provision/src/wifi_provision.c` 现在引入：
  - `BLE_NOTIFY_FLUSH_DELAY_MS`
  - `wifi_provision_schedule_ble_stop()`
  - `wifi_provision_delayed_ble_stop_task()`
- 设备在 BLE 配网成功后：
  1. 先发送 `connected` 状态
  2. 把 `current_transport` 置回 `NONE`
  3. 启动一个很短的延迟 stop 任务
  4. 等待通知冲刷窗口后再停止 BLE transport

当前窗口值为：

- `600ms`

这个值的目标不是做长时间保活，而是给 notify 一个稳定送达机会。

## 适用边界

- 该结论主要针对当前仓库的：
  - 自定义 NimBLE GATT
  - 微信小程序客户端
  - JSON 文本 notify 回包
- 这不是通用 BLE 最佳实践，只是当前“开发板近场配网”闭环的稳定性补丁。

## 后续建议

- 若后续再次出现“小程序已收到 connecting，但成功后页面直接断开且没有 connected 回包”，优先检查：
  - 是否仍存在立即 stop BLE 的路径
  - 刷到板子上的镜像是否已包含本修复
  - 小程序侧是否在断线时错误清空了最后一条结果
