---
id: ble-provisioning-nimble-uuid-byte-order
tags: [project, ble, provisioning, nimble, uuid, esp32-s3]
summary: 记录当前仓库自定义 BLE GATT UUID 在 NimBLE 下必须使用 little-endian 字节序，否则小程序会出现已连接但找不到目标服务。
last_reviewed: 2026-04-08
---

# BLE 配网 NimBLE UUID 字节序

## 结论

- 当前仓库自定义 BLE GATT 使用 `BLE_UUID128_INIT(...)` 定义 128-bit UUID 时，必须按 `little-endian` 字节序填写。
- 若直接按人类阅读的 canonical UUID 文本顺序拆字节填入，微信小程序虽然仍可能通过设备名连接成功，但在 `getBLEDeviceServices` 后会报：
  - `未找到目标服务 1C5ADFB4-6B3F-BFF4-EA4A-820304901A02`
- 典型板端现象是：
  - 已开始广播
  - `BLE client connected`
  - 约 1-2 秒后立刻 `BLE client disconnected`
  - 没有后续 `BLE notify=1` 或配网命令日志

## 当前协议对应的正确写法

- 小程序约定的 canonical UUID：
  - Service: `1C5ADFB4-6B3F-BFF4-EA4A-820304901A02`
  - RX: `1C5ADFB5-6B3F-BFF4-EA4A-820304901A02`
  - TX: `1C5ADFB6-6B3F-BFF4-EA4A-820304901A02`
- NimBLE `BLE_UUID128_INIT(...)` 里应写为：
  - Service: `02 1A 90 04 03 82 4A EA F4 BF 3F 6B B4 DF 5A 1C`
  - RX: `02 1A 90 04 03 82 4A EA F4 BF 3F 6B B5 DF 5A 1C`
  - TX: `02 1A 90 04 03 82 4A EA F4 BF 3F 6B B6 DF 5A 1C`

## 证据

- 当前仓库实机联调中，板端日志表现为：
  - 广播正常
  - 手机/小程序可以连上
  - 小程序端提示“未找到目标服务”
  - 板端很快断开连接并重新广播
- ESP-IDF 自带 NimBLE GATT 示例也按 little-endian 顺序传入 `BLE_UUID128_INIT(...)`，例如文档中的：
  - canonical characteristic UUID `00001525-1212-EFDE-1523-785FEABCD123`
  - 宏实参写成 `0x23, 0xd1, ... , 0x00, 0x00`

## 对后续 agent 的建议

- 若再次出现“连接成功但找不到 service/characteristic”，先不要急着怀疑微信小程序 API。
- 第一检查项应是：
  - `ble_provision_transport.c` 中 `BLE_UUID128_INIT(...)` 的字节顺序
- 修改自定义 UUID 后，必须至少重新做：
  - 源码级 UUID 顺序检查
  - `idf.py build`
  - 重新 flash 板子
