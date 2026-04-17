---
id: wifi-management-ui-behavior
tags: [project, wifi, ui, provisioning, ble, ap, network-service]
summary: 记录主界面 Wi-Fi 图标、全屏 Wi-Fi 管理页，以及默认配网方式设置在当前仓库中的真实行为边界。
last_reviewed: 2026-04-17
---

# Wi-Fi 管理 UI 行为

- 主界面下拉菜单中的 `screen_main_Wifi` 已接成真实 Wi-Fi 状态入口。
- `screen_main_Bluetooth` 仍保留在 generated 布局里，但运行时隐藏，不再承担主界面联网入口语义。

## 主界面行为

- `screen_main_Wifi` 只承担两件事：
  - 展示当前是否已连接 Wi-Fi
  - 打开全屏 Wi-Fi 管理页
- 图标状态只有两态：
  - 灰色：`network_service_is_wifi_connected()` 为 `false`
  - 蓝色：`network_service_is_wifi_connected()` 为 `true`
- 当前通过 `main/ui/custom/main_dropdown_controller.c` 中的轻量定时同步收敛图标 checked 状态。

## Wi-Fi 管理页

- 入口控制器：
  - `main/ui/custom/wifi_management_controller.c`
- 页面结构分为三块：
  - 顶部状态区
  - 中间主操作区
  - 下方设置区

### 顶部状态区

- 当前展示：
  - `已连接`
  - `正在重新配网`
  - `已断开`
  - `未连接`
- 当前实现只展示 IP，不展示 SSID。

### 中间主操作区

- `进入配网`
  - 当前语义：再次使用已保存凭据联网
  - 对应接口：`network_service_request_connect_with_saved_credentials()`
- `断开联网`
  - 当前语义：断开当前连接，并暂停自动重连
  - 对应接口：`network_service_request_disconnect()`
- `重连`
  - 当前语义：重新进入新的配网流程
  - 对应接口：`network_service_request_reprovision()`

## 默认配网方式

- 设置位置：
  - Wi-Fi 管理页底部
- 当前枚举：
  - `AUTO`
  - `BLE`
  - `AP`
- 持久化位置：
  - namespace: `network_svc`
  - key: `prov_transport`

### 当前行为

- `AUTO`
  - 优先尝试 BLE
  - BLE 启动失败时回退到 AP
- `BLE`
  - 直接尝试 BLE 配网
- `AP`
  - 直接尝试 AP 配网

## 运行时状态收口

- 当前仍沿用 `main/services/network_service.[ch]` 作为统一控制面，没有新建独立 `wifi_service` 文件。
- 但对 UI 暴露的新增 Wi-Fi 语义接口已经包括：
  - `network_service_is_wifi_connected()`
  - `network_service_get_wifi_status()`
  - `network_service_request_connect_with_saved_credentials()`
  - `network_service_request_disconnect()`
  - `network_service_request_reprovision()`
  - `network_service_set_default_provision_transport()`
  - `network_service_get_default_provision_transport()`

## 关键实现边界

- “断开联网”不仅调用 STA 断开，还会关闭底层自动重连闸门。
- 当前自动重连闸门实际落在：
  - `components/wifi_provision/src/wifi_driver/wifi_manager.c`
- 新配网流程开始前，会先停止当前活动的 BLE/AP transport，避免 UI 上存在两套入口同时活跃。
- 当前页面仍是 hand-written LVGL 页，不依赖 GUI Guider 生成新的页面结构。

## 当前残余风险

- 页面当前只展示 IP，不展示 SSID，状态说明仍偏最小实现。
- “重连”会重新进入配网，但当前不会自动清空历史凭据；若用户中途退出新配网流程，后续行为仍可能受旧凭据影响。
- 旧的 `network_service_state_t` 仍服务于 AI 页面等历史消费者，因此当前是“新增 Wi-Fi façade”，不是彻底重命名状态机。
