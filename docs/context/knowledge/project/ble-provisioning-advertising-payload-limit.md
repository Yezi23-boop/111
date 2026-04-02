---
id: ble-provisioning-advertising-payload-limit
tags: [project, ble, provisioning, nimble, esp32-s3]
summary: 记录当前仓库 BLE 配网在 NimBLE 广播阶段遇到的 31 字节 payload 上限问题、实机证据和修复方式。
last_reviewed: 2026-04-02
---

# BLE 配网广播负载上限

## 结论

- 当前仓库的 BLE 配网在 `ESP32-S3 + NimBLE + 自定义 128-bit Service UUID` 组合下，若把 `flags + tx power + 完整设备名 + 128-bit UUID` 同时塞进 advertising data，会超过传统广播包 `31` 字节上限。
- 在本仓库里，这个问题的实机表现为：
  - `ble_prov: BLE host task started`
  - 紧接着 `ble_prov: set adv fields failed, rc=4`
- `rc=4` 在 NimBLE 中对应 `BLE_HS_EMSGSIZE`，即消息尺寸超限，不是控制器未启动，也不是 GATT 注册失败。

## 当前仓库中的证据

- 设备名默认会带 MAC 后缀，例如 `ESP32S3-723C`。
- `ble_provision_transport.c` 原先在 `ble_gap_adv_set_fields()` 中同时放入：
  - `BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP`
  - `tx power`
  - `complete name`
  - `128-bit service UUID`
- 对于带后缀的设备名，这组字段会超过 `31` 字节。

## 修复方式

- 主 advertising data 只保留：
  - `flags`
  - `128-bit service UUID`
- 将完整设备名挪到 scan response：
  - `ble_gap_adv_rsp_set_fields(&scan_rsp_fields)`

这样可以同时满足：

- 主广播可被正常发出。
- 手机侧仍可通过 scan response 看到完整设备名。
- 不需要改 GATT UUID，也不需要缩短对外展示名。

## 实机验证结果

- 擦除 `nvs` 分区后，设备以“无凭据”路径启动。
- 修复前日志为：
  - `ble_prov: BLE host task started`
  - `ble_prov: set adv fields failed, rc=4`
- 修复后日志为：
  - `ble_prov: BLE host task started`
  - `ble_prov: BLE provisioning advertising: ESP32S3-723C`

## 对后续 agent 的建议

- 如果后续继续往 advertising data 中追加字段，先重新估算 `31` 字节上限。
- 若需要继续扩展广播内容，优先把非必要展示字段放到 scan response，而不是继续堆进主 advertising data。
- 若再次出现 `set adv fields failed, rc=4`，优先排查 advertising payload 长度，而不是先怀疑 BLE 控制器初始化。
