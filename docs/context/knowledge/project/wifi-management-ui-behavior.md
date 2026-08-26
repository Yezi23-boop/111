---
id: wifi-management-ui-behavior
tags: [project, wifi, ui, provisioning, ble, softap, network-manager]
summary: 记录主界面 Wi-Fi / Bluetooth 图标，以及全屏 Wi-Fi 管理页在当前仓库中的真实行为边界。
last_reviewed: 2026-04-25
memory_type: semantic
scope: component
owners: main/ui/custom/wifi_management_controller.c, main/ui/custom/main_dropdown_controller.c
triggers: wifi-management, wifi-ui, 二次进入, crash, lifecycle, controller
evidence_level: observed
status: active
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
- `main/services/network/network_service.[ch]` 仍存在，但当前已经退化为旧消费者兼容层 + service-ready 探测层，而不是新的 UI 主控制面。

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

## wifi-provision-retry-reasons


- 当前仓库已不再使用旧 `wifi_provision_start_auto()` 路径。
- 当前正式语义是：
  - `network_manager_start()` 先尝试最近成功连接的 latest Wi-Fi
  - 若 latest 失败，停在空闲态，等待用户进入 Wi-Fi 管理页显式点击 `BLE Provision` 或 `AP Web Fallback`
- Wi-Fi 断连原因日志当前由 `wifi_control` 侧的 STA 事件链路承接。
- 2026-06-01 后，`wifi_control_connect()` 只在当前已经连接或正在连接时才会先下发“重连前断开”；冷启动 latest Wi-Fi 连接不会再创建 suppress 窗口，避免首次真实断连事件被误吞掉而不触发自动重试。
- 本次修复的完整板端证据见 `docs/context/runs/2026-06-01-attempt-wifi-autoconnect-retry-fix.md`。


`wifi_control` 的断连日志包含：

```text
connect request: ssid=<ssid> pre_disconnect=<0|1>
STA 断开连接: reason=<reason> suppress=<0|1> auto_reconnect=<0|1> retry=<n>
```

判读：

- `pre_disconnect=0`：本次连接请求没有先制造显式断开清理窗口；冷启动 latest Wi-Fi 应该是这个值。
- `pre_disconnect=1`：本次连接前已有连接或连接尝试，允许先断开旧 STA 状态。
- `suppress=1`：这次断连被视为显式断开或切换连接前的清理事件，不应触发自动重连。
- `suppress=0 auto_reconnect=1 retry<n`：这次断连应进入 `esp_wifi_connect()` 自动重试。
- 冷启动 latest Wi-Fi 首次认证失败如果仍看到 `suppress=1`，说明连接切换标记又误覆盖了真实失败事件，应优先复查 `wifi_control_connect()` 前置断开路径。


- `reason=202`
  - `WIFI_REASON_AUTH_FAIL`
  - 含义：认证失败，常见于密码错误、AP 拒绝当前认证尝试。
- `reason=205`
  - `WIFI_REASON_CONNECTION_FAIL`
  - 含义：连接失败，常作为认证/握手失败后的汇总失败结果出现。
- `reason=15`
  - `WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT`
  - 含义：4-way handshake 超时，常见于密码错误、握手阶段兼容性或链路质量问题。


- 若启动后先反复出现 `202 / 205 / 15`，最后才进入 provisioning，而在重新配网后立即连接成功，则优先怀疑：
  - recent Wi-Fi 中最新一条记录已过期或密码错误
  - 不是 UI、LVGL 或字体问题
- 日志中若出现：
  - `wifi:state: assoc -> run`
  - 随后又 `run -> init`
  - 同时伴随 `reason=15`
  说明已经完成了关联，但 4-way 握手没有完成。


- `sdspi_transaction: cmd=52, R1 response: command not supported`
- `sdspi_transaction: cmd=5, R1 response: command not supported`
  - SD 卡 SPI 模式初始化阶段的兼容探测日志，后续若已 `SD卡挂载成功`，通常不是故障。
- `i2c.master: Please check pull-up resistances whether be connected properly`
  - ESP-IDF I2C 驱动的通用提醒；若后续 codec 设备初始化成功，一般不是当前主故障。
- `Password length matches WPA2 standards, authmode threshold changes from OPEN to WPA2`
  - Wi-Fi 栈根据密码长度自动把鉴权门槛从 OPEN 调整为 WPA2 的信息/提醒日志，不是错误。


- 看到 `202 / 205 / 15` 组合时，优先先核对 recent Wi-Fi 中最新一条记录是否还是最新密码。
- 若重新配网并提交同一 SSID 的新密码后能立刻成功，说明链路和驱动大概率没问题，问题主要在旧记录。



## button-provisioning-entry-mapping


- 当前仓库中，`BOOT` 键不再承担任何 BLE/AP 配网入口语义。
- 当前默认配网入口已经收回到 UI：
  - 主界面下拉菜单中的 `screen_main_Wifi` 打开 Wi-Fi 管理页
  - Wi-Fi 管理页中的 `BLE Provision` / `AP Web Fallback` 显式启动配网
- 对应实现位于：
  - `main/app/hardware_init.c`
  - `main/services/network/network_service.c`
  - `main/ui/custom/main_dropdown_controller.c`
  - `main/ui/generated/events_init.c`
- 当前 BOOT 键仍然会初始化 `espressif__button` 驱动，但只保留：
  - `BUTTON_LONG_PRESS_START` 日志挂点
- 当前 BOOT 键实现继续保持：
  - `GPIO10`
  - `.active_level = 1`
- 这样调整的目的，是避免“物理按键”和“UI 蓝牙总开关”同时控制 BLE/AP 配网入口，导致状态机语义冲突。
- 当前 UI 蓝牙按钮的实际语义是：
  - “手机蓝牙式”BLE 总开关
  - 只控制 BLE enabled 偏好和普通 BLE presence 可发现广播
  - 不自动启动小程序 BLE provisioning
- 当前 UI 蓝牙按钮的行为基线是：
  - BLE 偏好存入 `NVS`
  - 默认值为开启
  - 按钮视觉状态表达 BLE enabled 偏好，不表达实时 provisioning 活动状态
  - Wi-Fi 已连接时也允许 BLE enabled 与普通 BLE presence 并存
- 当前主流程保持为：
  - 无凭据开机：停在空闲态，等待用户进入 Wi-Fi 管理页显式配网
  - BLE 偏好关闭：点击 `BLE Provision` 会提示先打开 Bluetooth
  - 有凭据开机：优先自动联网
  - BLE 启动失败：仍可通过 `AP Web Fallback` 走 AP 网页兜底
- 已完成的验证包括：
  - 源码测试：覆盖 `network_service` BLE 开关控制面、UI 蓝牙按钮绑定、BOOT 配网入口移除
  - `idf.py build`
- 仍未完成的验证：
  - 真机点击 UI 蓝牙按钮时，广播出现/停止是否与按钮 checked 状态完全同步
  - 有凭据时点击蓝牙按钮，toast 提示是否满足实际交互预期


