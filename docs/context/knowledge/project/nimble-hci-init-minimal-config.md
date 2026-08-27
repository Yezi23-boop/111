---
id: nimble-hci-init-minimal-config
tags: [project, ble, provisioning, nimble, esp32-s3, sdkconfig]
summary: 记录 ESP32-S3 板端在单击进入 BLE 后出现 `hci inits failed / nimble host init failed` 时，当前仓库采用的最小 NimBLE peripheral 配置收敛方案，以及 `sdkconfig.defaults` 中无效 BTDM 键的处理。
last_reviewed: 2026-08-07
memory_type: semantic
scope: repo
owners: components/network_provisioning_adapter, components/ble_control
triggers: nimble, hci, init, minimal, config
evidence_level: observed
status: active
---

# NimBLE HCI 初始化最小配置

## 现象

- 板端在单击按键后，已确认会进入 BLE 启动路径。
- 串口日志出现：
  - `BLE_INIT: hci inits failed`
  - `BLE_INIT: nimble host init failed`
  - `ble_prov: nimble init failed, rc=-1`
- 失败发生在 IDF `nimble_port_init()` 内部的 `esp_nimble_hci_init()` 阶段，而不是广播字段设置阶段。

## 与已知问题的区分

- 这不是此前已修复的 advertising payload 超长问题。
- advertising payload 问题的典型现象是：
  - `ble_prov: BLE host task started`
  - `ble_prov: set adv fields failed, rc=4`
- 当前故障更早，尚未进入 advertising data 配置阶段。

## 当前仓库中的判断

- 当前 `sdkconfig` 展开的 NimBLE 配置比 BLE 配网实际需求更重，包含：
  - `CENTRAL`
  - `OBSERVER`
  - `GATT_CLIENT`
  - BLE 5.x 相关功能
  - `EXT_SCAN`
  - 较高的 `CONFIG_BT_CTRL_BLE_MAX_ACT`
- 对当前自定义 BLE 配网方案来说，最小必需能力只有：
  - `PERIPHERAL`
  - `BROADCASTER`
  - `GATT_SERVER`

## 当前修复方式

- 将蓝牙配置收敛到“最小 BLE peripheral/server”档：
  - 关闭 `CONFIG_BT_NIMBLE_ROLE_CENTRAL`
  - 关闭 `CONFIG_BT_NIMBLE_ROLE_OBSERVER`
  - 关闭 `CONFIG_BT_NIMBLE_GATT_CLIENT`
  - 关闭 `CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT`
  - 关闭 `CONFIG_BT_NIMBLE_EXT_SCAN`
  - 将 `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` 收到 `1`
  - 将 `CONFIG_BT_CTRL_BLE_MAX_ACT` 收到 `2`
- 这些修改同时写入：
  - `sdkconfig.defaults`
  - 当前工作树中的 `sdkconfig`

## 关于 `sdkconfig.defaults` 的一个注意点

- 官方部分 NimBLE 例程的 `sdkconfig.defaults` 会写：
  - `CONFIG_BTDM_CTRL_MODE_BLE_ONLY=y`
  - `CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY=n`
  - `CONFIG_BTDM_CTRL_MODE_BTDM=n`
- 但当前 `ESP32-S3` 目标实际走的是 `components/bt/controller/esp32s3/Kconfig.in -> esp32c3/Kconfig.in` 这套 controller Kconfig。
- 在当前仓库的配置解析里，上述 `BTDM_CTRL_MODE_*` 会被当成 unknown symbol 并报警。
- 因此当前仓库已将这些无效键从 `sdkconfig.defaults` 中移除，避免每次配置都出现误导性 warning。

## 当前验证结论

- 已完成：
  - 源码级配置测试
  - `idf.py fullclean`
  - `idf.py build`
- 尚未完成：
  - 刷机后板端再次验证
  - 确认 `hci inits failed` 是否已被真实消除

## 对后续 agent 的建议

- 如果再次遇到 `hci inits failed / nimble host init failed`，先看当前 `sdkconfig` 是否又被撑回了多角色/多特性档，而不是先怀疑 advertising payload 或微信小程序。
- 对当前 BLE 配网功能，不要默认保留 central/observer/gatt client；除非需求升级，否则维持最小 peripheral/server 档。
- 在没有新实机证据前，应把这轮配置收敛视为“高可信修复方向”，但不是“已实机确认的最终结论”。
