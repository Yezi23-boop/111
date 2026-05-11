---
id: ble-provisioning-terminal-notify-flush-window
tags: [project, ble, provisioning, wechat, notify, timing]
summary: 历史卡：记录旧自定义 BLE 配网在返回终态 `connected` 后若立即关闭 transport，可能导致微信小程序收不到最后一条成功通知；当前正式配网 owner 已迁到 network_provisioning_adapter。
last_reviewed: 2026-05-08
memory_type: semantic
scope: repo
owners: components/network_provisioning_adapter, components/ble_control
triggers: ble, provisioning, terminal, notify, flush, window
evidence_level: observed
status: superseded
superseded_by: docs/context/knowledge/project/network-provisioning-custom-upper-architecture.md
---

# BLE 配网终态通知冲刷窗口

## 结论

- 本卡记录的是旧自定义 BLE 配网链路的历史经验。
- 当前正式配网链路已经迁到 `components/network_provisioning_adapter` 的官方 `network_provisioning / wifi_prov_mgr` 适配层；不要再从旧 `components/wifi_provision` 路径开始排查。
- 旧链路在 `WIFI_STATE_CONNECTED` 时，会先通过 notify 返回：
  - `{"evt":"status","state":"connected","ssid":"...","ip":"..."}`
- 如果固件在发送这条 notify 后立即调用 `ble_provision_transport_stop()`，微信小程序侧存在收不到最终成功回包的风险。

## 历史处理方式

- 旧 `components/wifi_provision/src/wifi_provision.c` 曾引入：
  - `BLE_NOTIFY_FLUSH_DELAY_MS`
  - `wifi_provision_schedule_ble_stop()`
  - `wifi_provision_delayed_ble_stop_task()`
- 旧链路在 BLE 配网成功后：
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
  - 当前 `network_provisioning_adapter` / 官方 manager 是否存在等价的过早 stop/deinit 路径
  - 刷到板子上的镜像是否来自当前网络 owner，而不是旧 `wifi_provision` 历史分支
  - 小程序侧是否在断线时错误清空了最后一条结果
