---
id: ble-provisioning-ui-toggle-behavior
tags: [project, ble, provisioning, ui, network-service, nvs, history]
summary: 记录 BLE 总开关与 BLE 配网入口的分离语义，明确主界面蓝牙按钮只启动普通可发现广播，不自动启动小程序配网。
last_reviewed: 2026-04-25
memory_type: semantic
scope: repo
owners: main/ui/custom/wifi_management_controller.c, components/network_manager, components/ble_control
triggers: ble, provisioning, ui, toggle, behavior
evidence_level: observed
---

# BLE 配网 UI 总开关行为

- 当前仓库把“BLE 总开关”和“BLE 配网会话”拆成两个动作。
- `screen_main_Bluetooth` 当前可见，语义类似手机蓝牙开关：表达 BLE enabled 偏好，并启动普通 BLE 可发现广播，不自动启动小程序配网。
- Wi-Fi 配网页面里的 `BLE Provision` 才是官方 BLE provisioning 广播入口。

## 控制分层

- UI 不直接操作底层 provisioning adapter。
- 主界面蓝牙开关统一通过 `network_manager_set_ble_enabled(bool enabled)` 改 BLE enabled 偏好并同步 `ble_presence` 普通广播。
- Wi-Fi 配网页面通过 `network_manager_start_ble_provisioning()` 显式启动小程序配网。
- AP 网页兜底通过 `network_manager_start_softap_provisioning()` 显式启动。
- `components/ble_presence` 是普通 BLE 可发现广播 owner；`components/network_provisioning_adapter` 是官方 BLE/SoftAP provisioning owner。

## NVS 偏好

- BLE 开关偏好存放在：
  - namespace: `network_svc`
  - key: `ble_enabled`
- 默认值为开启，目的是兼容当前仓库“无凭据默认进入 BLE 配网”的既有行为。

## 当前 UI 位置

- 主界面默认入口：
  - `screen_main_Wifi`
  - 展示真实 Wi-Fi 连接状态，并进入 Wi-Fi 管理页
- 主界面蓝牙入口：
  - `screen_main_Bluetooth`
  - 控制 BLE enabled 偏好和普通 BLE 可发现广播
  - 关闭时如果 BLE provisioning 正在运行，会停止该会话
  - 开启时会广播普通 BLE presence 名称 `ESP32S3-723C`，但不会自动广播 provisioning 服务
- BLE 配网入口：
  - `main/ui/custom/wifi_management_controller.c`
  - 用户进入 Wi-Fi 管理页后点击 `BLE Provision`
- 自动路径：
  - 开机无 recent Wi-Fi 或 latest 连接失败时，不再自动启动 BLE provisioning
  - 设备停在空闲态，等待用户明确选择 `BLE Provision` 或 `AP Web Fallback`

## 状态机语义

- `network_service_state_t` 已新增：
  - `NETWORK_SERVICE_STATE_BLE_DISABLED`
- 其含义是：
  - 当前没有 Wi-Fi 凭据
  - 用户主动关闭了 BLE enabled 偏好
  - 服务任务因此不再自动重拉 BLE
- 这用于区分：
  - “尚未启动/离线”
  - “BLE 正在配网”
  - “用户主动关闭 BLE”

## 当前残余风险

- 普通 BLE presence 当前采用 non-connectable advertising，目的是让小程序/扫描工具能发现设备，同时避免手机系统误连占用唯一 BLE connection；部分手机系统蓝牙设置页可能仍不会像经典蓝牙设备一样展示它。
- 若 BLE 配网启动失败，Wi-Fi 管理页只显示简短错误，详细原因仍主要依赖串口日志定位。
- 该文档只覆盖 BLE enabled 与 provisioning 入口的分离边界，不代表项目已经具备完整通用蓝牙业务协议。
