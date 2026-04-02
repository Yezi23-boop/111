---
id: ble-provisioning-wechat-feasibility
tags: [project, wifi, provisioning, ble, wechat, esp32-s3]
summary: 当前仓库新增 BLE 配网时，推荐先走“自定义 BLE GATT 主路径 + 现有 AP 网页兜底 + 先固件后小程序”的最小闭环方案。
last_reviewed: 2026-04-02
---

# BLE 配网与微信小程序可行性

## 结论

- 当前仓库为 ESP32-S3 增加 BLE 配网是可行的。
- 第一阶段最稳的方案不是直接上 ESP-IDF 官方 `wifi_prov_mgr`，而是：
  - `components/wifi_provision` 继续作为唯一配网协调器
  - 新增自定义 BLE GATT 简化协议
  - 继续复用现有 `wifi_manager` 保存凭据和 STA 连接
  - 保留现有 AP 网页配网作为兜底

## 为什么先不选官方 `wifi_prov_mgr`

- 当前仓库还未启用 BLE 基础配置，先打开 BLE 能力就需要一轮资源验证。
- 官方方案依赖 `protocomm + protobuf + security1/2`，微信小程序端实现难度更高。
- 当前仓库已有稳定的 AP 配网底座，先复用本地 Wi-Fi owner 更容易快速形成闭环。

## 第一阶段推荐边界

- 固件侧先支持：
  - BLE 广播
  - 小程序写入 SSID/密码
  - 设备回传 `connecting / connected / failed`
- 小程序侧先支持：
  - 扫描 BLE 设备
  - 连接设备
  - 手动输入 SSID/密码
  - 查看结果和 AP 兜底提示

第一阶段暂不做：

- 设备端 Wi-Fi 扫描列表
- 官方 `wifi_prov_mgr` 协议兼容
- SRP/PoP 等完整安全握手

## 适合 ESP32-S3 的蓝牙模式

- 当前方案固定选择 `NimBLE`，只启用 BLE 能力，不引入经典蓝牙。
- 不启用经典蓝牙。
- 不启用 `BTDM` 双模。
- 不把 `Bluedroid` 作为第一阶段默认栈。

推荐配置键：

- `CONFIG_BT_ENABLED=y`
- `CONFIG_BT_ENABLED=y`
- `CONFIG_BT_NIMBLE_ENABLED=y`

原因：

- 当前目标只有 BLE 配网，没经典蓝牙需求。
- 这套配置对 `ESP32-S3` 的 RAM 压力更小。
- 与当前仓库已有的 `Wi-Fi + LVGL + audio` 组合更容易共存。

## 资源与风险

- 主要风险来自 BLE 打开后的 RAM 增量，以及与当前 `LVGL + audio + official_chat` 的组合占用。
- 启动时不建议默认同时拉起 BLE 和 AP，优先 BLE，AP 仍由按钮触发。
- 第一阶段的 BLE 配网不适合直接作为生产安全方案，应明确其“开发板与近距离调试优先”的边界。

## 推荐实施顺序

1. 先补 `sdkconfig.defaults` 并打开 `NimBLE`
2. 在 `components/wifi_provision` 内接入 BLE transport
3. 调整 `network_service`，无凭据时默认启动 BLE 配网
4. 确认 AP 网页兜底仍然可用
5. 再编写微信小程序最小客户端
