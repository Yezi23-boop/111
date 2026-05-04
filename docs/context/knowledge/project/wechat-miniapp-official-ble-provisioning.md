---
id: wechat-miniapp-official-ble-provisioning
tags: [project, wifi, provisioning, ble, wechat, miniapp]
summary: 记录微信小程序 BLE 配网应对齐当前官方 network_provisioning 协议，而不是继续沿用历史 JSON GATT 协议。
last_reviewed: 2026-04-25
memory_type: semantic
scope: repo
owners: components/network_provisioning_adapter, components/ble_control, components/network_manager
triggers: wechat, miniapp, official, ble, provisioning
evidence_level: observed
---

# 微信小程序官方 BLE 配网路线

## 结论

- 当前小程序端应优先实现“官方 protocomm BLE client”。
- 小程序协议层参考 SoftAP 配网页的官方 `proto-ver / prov-session / prov-scan / prov-config` 流程。
- 历史 `hello / status / scan_wifi / set_wifi` JSON GATT 协议只能作为旧镜像兜底，不再作为新架构主线。

## 端点与 UUID

- 官方 BLE provisioning 默认服务 UUID：
  - `2D9BED07-060F-877C-9B43-436B4D247517`
- ESP-IDF 5.5.3 当前端点 UUID 映射：
  - `prov-scan`：`0xFF50`
  - `prov-session`：`0xFF51`
  - `prov-config`：`0xFF52`
  - `proto-ver`：`0xFF53`
- GATT characteristic UUID 由 service UUID 作为 base，并替换底层 UUID byte `12/13` 为端点 16-bit UUID。
- 官方文档也允许通过 characteristic user description descriptor `0x2901` 读取端点名；但微信小程序端为了降低 descriptor API 兼容风险，可以优先使用固定端点 UUID 推导。

## 小程序实现边界

- 小程序先扫描官方设备名 `NET_PROV*` 或官方服务 UUID。
- 连接后发现服务和特征，建立端点名到 characteristic 的映射。
- BLE request 模式是：
  - 向对应 endpoint characteristic 写入一个完整 protobuf 请求
  - 再读取同一个 characteristic 等待响应
- 不再使用历史自定义 RX/TX notify 串口模型来承载官方协议。

## 风险

- 微信小程序 BLE 长包能力仍需真机验证。
- 官方 protocomm BLE 通常把一次 GATT write 视作一个完整请求，不等价于历史 JSON 协议里的 20 字节分片重组。
- `proto-ver / prov-session / prov-scan` 包体较小，优先用于最小闭环。
- `prov-config` 携带 SSID/密码，可能超过 20 字节；若真机失败，需要评估 `wx.setBLEMTU()` 或固件侧兼容层。
