---
id: wifi-management-ui-behavior
tags: [project, wifi, ui, provisioning, ble, softap, network-manager]
summary: 记录主界面 Wi-Fi / Bluetooth 图标，以及全屏 Wi-Fi 管理页在当前仓库中的真实行为边界。
last_reviewed: 2026-04-19
---

# Wi-Fi 管理 UI 行为

- 主界面下拉菜单中的 `screen_main_Wifi` 已接成真实 Wi-Fi 状态入口。
- `screen_main_Bluetooth` 已恢复显示，并接成真实 BLE 总开关入口。

## 主界面行为

- `screen_main_Wifi` 只承担两件事：
  - 展示当前是否已连接 Wi-Fi
  - 打开全屏 Wi-Fi 管理页
- 图标状态只有两态：
  - 灰色：`network_manager_get_status().wifi_connected` 为 `false`
  - 蓝色：`network_manager_get_status().wifi_connected` 为 `true`
- 当前通过 `main/ui/custom/main_dropdown_controller.c` 中的轻量定时同步收敛图标 checked 状态。

### `screen_main_Bluetooth`

- 当前仍保留在主界面下拉菜单中。
- 当前语义是 BLE 总开关，不是 BLE 配网活动指示灯。
- 图标状态只有两态：
  - 灰色：`network_manager` 视角下 `ble_enabled == false`
  - 蓝色：`network_manager` 视角下 `ble_enabled == true`
- 当前不会因为 BLE 没在广播就自动变灰；只要 BLE 总开关偏好仍是开启，图标就保持高亮。
- 若底层 BLE 总开关更新失败，主界面会通过 toast 提示。
- 当前真实运行时语义已经补齐：
  - 从蓝色切到灰色时，若当前正处于 BLE provisioning，会立即停止 BLE transport
  - 从灰色切到蓝色时，若当前未连网、没有活跃 provisioning、且默认 transport 为 BLE，会立即重新拉起 BLE provisioning
  - 若默认 transport 当前为 SoftAP，则蓝牙图标只改变 BLE 总开关偏好，不会抢占当前 SoftAP provisioning

## Wi-Fi 管理页

- 入口控制器：
  - `main/ui/custom/wifi_management_controller.c`
- 页面结构分为三块：
  - 顶部状态区
  - 中间主操作区
  - 下方设置区

### 顶部状态区

- 当前展示：
  - `Connected`
  - `Connecting`
  - `Provisioning`
  - `Disconnected`
  - `Offline`
  - `Error`
- 当前实现只展示 IP，不展示 SSID。
- 为规避当前字体/编码链路下的中文显示问题，Wi-Fi 管理页可见文案已统一使用英文。

### 中间主操作区

- `Use Saved Wi-Fi`
  - 当前语义：再次使用已保存凭据联网
  - 对应接口：`network_manager_use_latest_wifi()`
- `Disconnect`
  - 当前语义：断开当前连接，并暂停自动重连
  - 对应接口：`network_manager_disconnect()`
- `Reprovision`
  - 当前语义：重新进入新的配网流程
  - 对应接口：`network_manager_reprovision()`

## 默认配网方式

- 设置位置：
  - Wi-Fi 管理页底部
- 当前枚举：
  - `BLE`
  - `SoftAP`

### 当前行为

- `BLE`
  - 直接尝试 BLE 配网
- `SoftAP`
  - 直接尝试 SoftAP 配网

## 运行时状态收口

- 当前 UI 正式控制面已经切到 `components/network_manager`。
- 当前由 UI 直接使用的接口包括：
  - `network_manager_get_status()`
  - `network_manager_use_latest_wifi()`
  - `network_manager_disconnect()`
  - `network_manager_reprovision()`
  - `network_manager_set_default_transport()`
  - `network_manager_set_ble_enabled()`
- `main/services/network_service.[ch]` 仍存在，但当前已经退化为旧消费者兼容层 + service-ready 探测层，而不是新的 UI 主控制面。

## 关键实现边界

- “断开联网”不仅调用 STA 断开，还会关闭底层自动重连闸门。
- 当前自动重连闸门实际落在：
  - `components/wifi_control`
- 新配网流程开始前，会先停止当前活动的 BLE/AP transport，避免 UI 上存在两套入口同时活跃。
- 若设备开机时没有 recent Wi-Fi，且默认 transport 为 BLE 但 BLE 总开关已关闭：
  - 当前会停在合法空闲态等待用户手动操作
  - 不再把后台网络服务直接打成启动失败
- 当前页面仍是 hand-written LVGL 页，不依赖 GUI Guider 生成新的页面结构。

## 当前残余风险

- 页面当前只展示 IP，不展示 SSID，状态说明仍偏最小实现。
- “重连”会重新进入配网，但当前不会自动清空历史凭据；若用户中途退出新配网流程，后续行为仍可能受旧凭据影响。
- 旧的 `network_service_state_t` 仍服务于 AI 页面等历史消费者，因此当前仍保留一层兼容 shim，但新的 UI 和联网控制语义已经以 `network_manager` 为准。
