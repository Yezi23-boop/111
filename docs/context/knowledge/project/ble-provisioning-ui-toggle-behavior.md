---
id: ble-provisioning-ui-toggle-behavior
tags: [project, ble, provisioning, ui, network-service, nvs, history]
summary: 记录 BLE 配网总开关的当前保留能力与历史入口变化，明确主界面默认联网入口已经迁移到 Wi-Fi 按钮，蓝牙按钮不再承担主路径语义。
last_reviewed: 2026-04-17
---

# BLE 配网 UI 总开关行为

- 当前仓库仍保留 BLE 配网总开关能力，但主界面默认联网入口已经迁移到 `screen_main_Wifi`。
- `screen_main_Bluetooth` 对象目前仍保留在 generated 布局中，但运行时已被隐藏，不再承担主路径语义。
- 第一版 BLE 开关仍只控制当前仓库已有的 BLE 配网能力，不扩展新的蓝牙业务。

## 控制分层

- UI 不直接操作 `wifi_provision`。
- UI 统一通过 `network_service` 控制 BLE 配网入口。
- 当前相关接口为：
  - `network_service_set_ble_enabled(bool enabled)`
  - `network_service_is_ble_enabled(void)`
  - `network_service_is_ble_active(void)`
- 默认配网方式切换已迁移到新的 Wi-Fi 管理页设置区，不再要求用户从主界面直接理解 BLE/AP 细节。

## NVS 偏好

- BLE 开关偏好存放在：
  - namespace: `network_svc`
  - key: `ble_enabled`
- 默认值为开启，目的是兼容当前仓库“无凭据默认进入 BLE 配网”的既有行为。

## 当前 UI 位置

- 主界面默认入口：
  - `screen_main_Wifi`
  - 展示真实 Wi-Fi 连接状态，并进入 Wi-Fi 管理页
- BLE 相关选择位置：
  - `main/ui/custom/wifi_management_controller.c`
  - 通过“默认配网方式：AUTO / BLE / AP”表达，而不是主界面单独按钮
- 因此本卡不再描述“主界面点击蓝牙按钮后会发生什么”，只保留 BLE 开关能力本身的边界。

## 状态机语义

- `network_service_state_t` 已新增：
  - `NETWORK_SERVICE_STATE_BLE_DISABLED`
- 其含义是：
  - 当前没有 Wi-Fi 凭据
  - 用户主动关闭了 BLE 配网入口
  - 服务任务因此不再自动重拉 BLE
- 这用于区分：
  - “尚未启动/离线”
  - “BLE 正在配网”
  - “用户主动关闭 BLE”

## 当前残余风险

- 当前 Wi-Fi 管理页只提供“默认配网方式”选择，不提供更细的 BLE 调试信息，例如广播名或启动失败错误码。
- 若 BLE 作为默认 transport 启动失败，`AUTO` 路径会回退到 AP；但纯 `BLE` 模式下仍主要依赖日志定位失败原因。
- 该文档只覆盖“BLE 配网入口能力的保留边界”，不代表项目已经具备完整的通用蓝牙业务开关架构。
