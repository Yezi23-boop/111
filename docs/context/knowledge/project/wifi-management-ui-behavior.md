---
id: wifi-management-ui-behavior
tags: [project, wifi, ui, provisioning, ble, softap, network-manager]
summary: 记录主界面 Wi-Fi / Bluetooth 图标，以及全屏 Wi-Fi 管理页在当前仓库中的真实行为边界。
last_reviewed: 2026-04-25
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
  - 从灰色切到蓝色时，会启动普通 BLE 可发现广播 `ESP32S3-723C`，但不自动启动小程序配网
  - Wi-Fi 已连接时也允许 BLE enabled 保持开启，两者不再互斥

## Wi-Fi 管理页

- 入口控制器：
  - `main/ui/custom/wifi_management_controller.c`
- 页面结构分为三块：
  - 顶部状态区
  - 中间主操作区
  - AP 网页兜底入口

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
  - 已被拆成显式入口，不再要求用户先选择 transport 再重启配网
- `BLE Provision`
  - 当前语义：启动官方 BLE provisioning 广播，供微信小程序连接
  - 对应接口：`network_manager_start_ble_provisioning()`
  - 当前边界：若主界面蓝牙总开关关闭，会提示先打开 Bluetooth，不会偷偷开启 BLE
  - 启动前会先停止普通 `ble_presence` 广播，让官方 provisioning adapter 独占 NimBLE host
- `AP Web Fallback`
  - 当前语义：保留 AP 网页配网兜底
  - 对应接口：`network_manager_start_softap_provisioning()`

### 运行时锁定边界

- 当 `network_manager` 处于：
  - `NETWORK_MANAGER_STATE_PROVISIONING_BLE`
  - `NETWORK_MANAGER_STATE_PROVISIONING_SOFTAP`
  - 或 `status.ble_active == true`
- Wi-Fi 管理页会临时锁住：
  - `BLE Provision`
  - `AP Web Fallback`
- 这样做是为了避免用户在小程序连接或 AP 门户会话中重复点击，把已经建立的 BLE/SoftAP 会话自己抖掉。

## 运行时状态收口

- 当前 UI 正式控制面已经切到 `components/network_manager`。
- 当前由 UI 直接使用的接口包括：
  - `network_manager_get_status()`
  - `network_manager_use_latest_wifi()`
  - `network_manager_disconnect()`
  - `network_manager_start_ble_provisioning()`
  - `network_manager_start_softap_provisioning()`
  - `network_manager_set_ble_enabled()`
- `main/services/network_service.[ch]` 仍存在，但当前已经退化为旧消费者兼容层 + service-ready 探测层，而不是新的 UI 主控制面。

## 关键实现边界

- “断开联网”不仅调用 STA 断开，还会关闭底层自动重连闸门。
- 当前自动重连闸门实际落在：
  - `components/wifi_control`
- 新配网流程开始前，会先停止当前活动的 BLE/AP transport，避免 UI 上存在两套入口同时活跃。
- 普通蓝牙可发现广播由 `components/ble_presence` 独立承载；官方 BLE provisioning 使用 `network_provisioning_adapter`，二者通过 `network_manager` 串行切换，避免两个 BLE owner 同时持有 NimBLE host。
- 用户显式点击：
  - `Use Saved Wi-Fi`
  - `Disconnect`
  - `Reprovision`
  之前，会先清掉上一轮 provisioning 尚未落盘的 pending Wi-Fi 记录，避免旧 SSID 在迟到的连网结果中被误提升为 latest recent。
- 若设备开机时没有 recent Wi-Fi，且默认 transport 为 BLE 但 BLE 总开关已关闭：
  - 当前会停在合法空闲态等待用户手动操作
  - 不再把后台网络服务直接打成启动失败
- 若设备开机没有 recent Wi-Fi，或 latest Wi-Fi 连接失败：
  - 当前不会自动启动 BLE 小程序配网
  - 用户必须进入 Wi-Fi 管理页点击 `BLE Provision` 或 `AP Web Fallback`
- 当前页面仍是 hand-written LVGL 页，不依赖 GUI Guider 生成新的页面结构。
- Wi-Fi 管理页返回主界面时，当前使用 `lv_screen_load_anim(..., true)` 删除旧 screen。
  因此控制器不能只靠静态缓存指针判断“页面已创建”；每次二次打开前都必须确认
  `lv_obj_is_valid(s_screen)`，并在 `LV_EVENT_DELETE` 时清空所有子控件缓存指针，
  否则再次进入页面时会对悬空按钮调用 `lv_obj_add_state()`，在 `lv_style_get_prop()`
  内触发 `LoadProhibited`。

## 当前残余风险

- 页面当前只展示 IP，不展示 SSID，状态说明仍偏最小实现。
- 门户前端成功提示仍依赖浏览器侧的固定轮询窗口；若某些路由器认证或 DHCP 偏慢，页面可能先提示“still connecting”，随后设备其实已经成功切回 STA。
- 旧的 `network_service_state_t` 仍服务于 AI 页面等历史消费者，因此当前仍保留一层兼容 shim，但新的 UI 和联网控制语义已经以 `network_manager` 为准。
