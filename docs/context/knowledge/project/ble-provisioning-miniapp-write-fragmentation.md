---
id: ble-provisioning-miniapp-write-fragmentation
tags: [project, ble, provisioning, wechat, miniapp, nimble]
summary: 记录微信小程序 BLE 写入的 20 字节兼容边界、自定义 JSON 分片重组方案，以及官方 protocomm BLE 路线的 MTU 处理边界。
last_reviewed: 2026-04-25
memory_type: semantic
scope: repo
owners: components/network_provisioning_adapter, components/ble_control
triggers: ble, provisioning, miniapp, write, fragmentation
evidence_level: observed
---

# 微信小程序 BLE 写入分片兼容

## 结论

- 微信小程序官方文档对 `wx.writeBLECharacteristicValue()` 明确给出兼容建议：单次写入尽量不要超过 `20` 字节。
- 当前仓库的 BLE 配网协议使用 UTF-8 JSON：
  - `hello`
  - `status`
  - `set_wifi`
  - `start_ap_fallback`
- 其中 `set_wifi` 和 `start_ap_fallback` 在真实场景下很容易超过 `20` 字节，因此不能把“小程序单次整包写入 JSON”当成稳定方案。

## 当前仓库中的处理方式

- 小程序侧：
  - 所有命令统一编码为 `JSON + '\\n'`
  - 按 `20` 字节分片顺序写入 RX characteristic
  - 相邻分片之间保留短暂间隔，避免并发写入
- 固件侧：
  - `ble_provision_transport.c` 新增 RX 分片缓存
  - 兼容两类输入：
    - 旧客户端：单次完整 JSON，且首字节是 `{`、末字节是 `}`
    - 新客户端：按 `\\n` 结尾的分片流，累计到换行后再一次性交给上层协议解析

## 为什么不用只依赖 MTU

- 微信小程序的 `wx.setBLEMTU()` 只在安卓 `5.1+` 有效。
- iOS 侧受系统限制，不适合作为当前最小闭环的唯一保障。
- 因此本仓库选择“应用层分片 + 固件端重组”，优先保证跨平台可用性。

## 官方 protocomm BLE 路线的差异

- 官方 `wifi_prov_mgr` / `protocomm_ble` 的 protobuf endpoint 不能沿用旧 JSON 的“20 字节普通写入分片”：
  - 普通 GATT write 每片会被固件当作一次独立 protobuf 请求处理。
  - 若需要超过单包 MTU 的请求，必须依赖 GATT prepare write/execute write；微信小程序通用 API 不直接暴露这层能力。
- 当前微信小程序官方配网客户端采用“先 `wx.setBLEMTU()`，再单包写入 protobuf”的策略：
  - Android 真机优先请求 `512` MTU。
  - 若 MTU 协商不可用或失败，保守按 `20` 字节 payload 上限校验。
  - 若 `prov-config` 请求超过当前 payload 上限，小程序应直接报错，避免把半包写入固件导致连接被关闭。
- 官方 `prov-scan` 的结果读取也需要控制单次响应大小；当前小程序按小批次拉取扫描结果，避免附近热点过多时一次 read 回包超过 MTU。
- 因此，旧 JSON 协议可应用层分片；官方 protocomm 协议默认不做应用层分片，优先通过 MTU 承接常见 SSID/密码长度。

## 对后续 agent 的建议

- 如果继续沿用当前 JSON 协议，默认继续保留 `JSON + '\\n' + 20 字节分片` 这套写法。
- 如果继续完善官方 BLE provisioning 小程序端，优先验证 `wx.setBLEMTU()` 真机日志和 `prov-config` payload 长度；不要把 protobuf 请求直接拆成多次普通 write。
- 如果后续协议字段继续变长，不需要优先改 MTU；当前方案已经能承接更长的请求。
- 若以后要切换到二进制帧协议，记得同时更新：
  - 小程序侧分片编码
  - 固件侧 RX 重组与帧边界判定
