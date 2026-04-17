# WiFi 管理界面与统一门面设计

## 背景

当前仓库已经具备以下基础：

- `components/wifi_provision`
  - 负责 BLE 配网与 AP 网页配网 transport
- `components/wifi_provision/src/wifi_driver/wifi_manager.c`
  - 负责底层 Wi-Fi STA/AP、凭据保存和连接状态
- `main/services/network_service.c`
  - 已承担部分后台联网与配网策略编排职责
- `main/ui/generated/setup_scr_screen_main.c`
  - 主界面下拉菜单中已存在 `screen_main_Wifi` 与 `screen_main_Bluetooth`

当前问题是：

- 主界面 `screen_main_Wifi` 只是一个 `checkable imagebutton`
- 蓝色/灰色图标仅由 UI checked 状态驱动，不是真实 Wi-Fi 连接状态
- BLE/AP 配网能力仍然以底层 transport 细节暴露给上层
- 当前缺少一个统一的“Wi-Fi 管理界面”，导致主界面入口、配网 transport、联网状态之间的语义不统一

本次设计目标是：

- 把“Wi-Fi”收敛成唯一的上层理解对象
- BLE/AP 都下沉为 Wi-Fi 配网内部 transport
- 主界面只保留一个真实的 Wi-Fi 状态入口
- 点击主界面 WiFi 图标后进入一个全新的全屏 Wi-Fi 管理界面
- Wi-Fi 管理界面同时承载联网状态、联网动作和默认配网方式设置

## 设计结论

### 产品交互方向

- 主界面 `screen_main_Wifi`
  - 接成真实 Wi-Fi 状态灯 + Wi-Fi 管理界面入口
- 主界面不再直接暴露 BLE/AP 选择
- BLE/AP 选择仅保留在 Wi-Fi 管理界面的设置区
- Wi-Fi 管理界面采用“分区式单页”
  - 顶部：当前状态区
  - 中间：主操作区
  - 下方：高级设置区

### 统一门面方向

- 上层统一以 `Wi-Fi` 为主题
- `BLE/AP` 只作为 Wi-Fi 配网 transport 存在
- 对外收敛为一组统一 `wifi_service_*` 语义接口
- UI、设置页、业务层不再直接调用 `wifi_provision_start_blecfg()` / `wifi_provision_start_apcfg()` 一类接口

## 主界面设计

### `screen_main_Wifi` 的正式语义

`screen_main_Wifi` 只承担两件事：

1. 展示当前是否已连接 Wi-Fi
2. 进入全新的 Wi-Fi 管理界面

### 图标规则

主界面 WiFi 图标严格只有两态：

- 灰色图标
  - 当前未连接 Wi-Fi
- 蓝色图标
  - 当前已连接 Wi-Fi

以下状态都仍显示灰色：

- 正在使用旧凭据尝试联网
- 正在重新配网
- 用户主动断开后暂停自动重连
- 最近一次联网失败

### 点击行为

- 灰色图标点击
  - 进入全新的 Wi-Fi 管理界面
- 蓝色图标点击
  - 仍进入同一个 Wi-Fi 管理界面

主界面不会直接做：

- 断开联网
- 重新配网
- 选择 BLE/AP

## Wi-Fi 管理界面设计

Wi-Fi 管理界面为全屏独立页面，采用 hand-written LVGL 页面，不继续向 `gui-guider` 生成页面内堆叠复杂运行态逻辑。

### 页面结构

#### 1. 顶部状态区

负责展示：

- 当前 Wi-Fi 总状态文案
- 已连接时的 SSID / IP
- 未连接或失败时的原因提示

建议状态文案范围：

- 未连接
- 正在使用已保存凭据联网
- 已连接
- 正在重新配网
- 已断开
- 联网失败

#### 2. 中间主操作区

固定保留 3 个按钮：

- `进入配网`
  - 含义：再次使用已保存凭据联网
  - 适用于开机后因凭据无效或环境变化未连上 Wi-Fi 的场景
- `断开联网`
  - 含义：断开当前网络连接
- `重连`
  - 含义：重新开始配网，让用户重新选择/输入网络

#### 3. 下方设置区

固定保留：

- `默认配网方式`
  - 单选：`AUTO / BLE / AP`

### 用户确认的主操作语义

- `进入配网`
  - 不是新配网
  - 是再次使用已保存凭据联网
- `断开联网`
  - 断开当前连接
- `重连`
  - 重新开始配网
  - 旧凭据不直接参与这次流程

## 统一状态机设计

上层不再以 `BLE/AP` 作为主状态，而以 `Wi-Fi` 为主状态。

建议统一主状态：

- `WIFI_SERVICE_STATE_IDLE`
- `WIFI_SERVICE_STATE_CONNECTING_WITH_SAVED_CREDENTIALS`
- `WIFI_SERVICE_STATE_CONNECTED`
- `WIFI_SERVICE_STATE_PROVISIONING`
- `WIFI_SERVICE_STATE_DISCONNECTED_BY_USER`
- `WIFI_SERVICE_STATE_ERROR`

### 状态含义

- `IDLE`
  - 服务已启动，但当前未连接，也未在配网
- `CONNECTING_WITH_SAVED_CREDENTIALS`
  - 正在使用已保存凭据联网
- `CONNECTED`
  - 已连接 Wi-Fi
  - 主界面 WiFi 图标显示蓝色
- `PROVISIONING`
  - 正在进行新的配网流程
  - 实际 transport 通过附加字段区分 `AUTO / BLE / AP`
- `DISCONNECTED_BY_USER`
  - 用户主动断开
  - 当前已断开，且暂停自动重连
- `ERROR`
  - 当前流程失败，需要页面提示

### 图标映射

只有 `CONNECTED` 时主界面 WiFi 图标为蓝色，其余所有状态均为灰色。

## 按钮与状态映射

### `进入配网`

作用：

- 再次使用已保存凭据联网

前提：

- 已保存凭据存在

状态迁移建议：

- `IDLE -> CONNECTING_WITH_SAVED_CREDENTIALS`
- `DISCONNECTED_BY_USER -> CONNECTING_WITH_SAVED_CREDENTIALS`
- `ERROR -> CONNECTING_WITH_SAVED_CREDENTIALS`

结果：

- 成功：进入 `CONNECTED`
- 失败：回到 `IDLE` 或 `ERROR`

### `断开联网`

作用：

- 断开当前网络连接

状态迁移建议：

- `CONNECTED -> DISCONNECTED_BY_USER`

结果：

- 主界面 WiFi 图标立即变灰
- 暂停自动重连

### `重连`

作用：

- 重新开始新的配网
- 用户重新选择/输入网络

状态迁移建议：

- `IDLE / ERROR / DISCONNECTED_BY_USER / CONNECTED -> PROVISIONING`

结果：

- 成功：进入 `CONNECTED`
- 失败：进入 `ERROR` 或回到 `IDLE`

## 模块职责收敛

### `wifi_manager`

继续作为唯一底层 Wi-Fi owner，仅负责：

- STA 连接/断开
- AP 启停
- 凭据保存/读取
- 当前连接状态/IP

不负责：

- 是否应该自动联网
- 是否应该进入配网
- 是否应该暂停重连

### `wifi_provision`

收敛为 Wi-Fi 配网 transport 层，仅负责：

- 启动 BLE 配网
- 启动 AP 配网
- 停止当前配网 transport
- 上报 transport 是否活跃

它知道 BLE/AP 实现细节，但不直接服务上层 UI/业务。

### `wifi_service`

未来统一的对外 Wi-Fi 门面，负责：

- 统一状态机
- 区分“旧凭据重试联网”和“重新配网”
- 管理用户主动断开后的暂停自动重连
- 保存默认配网方式 `AUTO / BLE / AP`
- 向 UI 和业务层输出统一状态与动作接口

### UI 控制器

建议新增 hand-written 控制器，例如：

- `wifi_ui_controller.c/.h`
- `wifi_management_view.c/.h`

职责：

- 主界面 `screen_main_Wifi` 灰/蓝图标同步
- 点击 `screen_main_Wifi` 进入 Wi-Fi 管理界面
- 驱动 Wi-Fi 管理界面上的 3 个主按钮
- 同步顶部状态区与下方默认配网方式设置

## 统一对外接口建议

建议未来统一暴露以下接口语义：

- `wifi_service_start()`
- `wifi_service_get_state()`
- `wifi_service_get_status(wifi_service_status_t *status)`
- `wifi_service_request_connect_with_saved_credentials()`
- `wifi_service_request_disconnect()`
- `wifi_service_request_reprovision()`
- `wifi_service_stop_provisioning()`
- `wifi_service_set_default_provision_transport(...)`
- `wifi_service_get_default_provision_transport()`
- `wifi_service_has_credentials()`
- `wifi_service_clear_credentials()`

## 旧接口处理建议

### 底层内部接口保留

以下接口可继续保留为底层 transport 实现能力：

- `wifi_provision_start_blecfg()`
- `wifi_provision_start_apcfg()`
- `wifi_provision_stop_blecfg()`
- `wifi_provision_is_ble_active()`
- `wifi_provision_is_ap_active()`

### 上层旧接口逐步废弃

以下接口不应继续作为上层正式入口：

- `network_service_request_ble()`
- `network_service_request_portal()`
- `network_service_set_ble_enabled()`

迁移期可先保留兼容层，但新 UI 与业务逻辑不应继续新增对这些接口的依赖。

## 推荐迁移路径

建议分 4 步迁移：

### 第 1 步：主入口从 Bluetooth 按钮迁到 WiFi 按钮

- 给 `screen_main_Wifi` 绑定 hand-written 控制器
- checked 状态改由真实连接态驱动
- 点击后进入新的 Wi-Fi 管理界面
- `screen_main_Bluetooth` 不再承担主入口语义

### 第 2 步：新增 hand-written Wi-Fi 管理界面

建议新增：

- `wifi_management_view.c/.h`
- `wifi_ui_controller.c/.h`

先把完整页面和状态展示跑起来，底层暂时仍可复用现有 `network_service`

### 第 3 步：把当前 `network_service` 语义升级为统一 Wi-Fi 门面

- 先不急于物理改名文件
- 优先新增统一 Wi-Fi 接口
- 新 UI 只调用统一接口
- 旧接口暂时作为兼容层保留

### 第 4 步：下沉 BLE/AP 细节，设置区接默认 transport

- 设置区接入 `AUTO / BLE / AP`
- 主界面只调用统一 Wi-Fi 入口
- BLE/AP 只作为设置区中的 transport 偏好出现
- 旧的 BLE/AP 直接入口逐步退出上层

## 风险与注意事项

- 当前 `screen_main_Wifi` 只有灰/蓝两态图标，因此“连接中”“配网中”不能靠图标表达，只能靠 Wi-Fi 管理界面的状态文案表达。
- 当前仓库里 `network_service` 已开始承担 Wi-Fi 编排职责，但其现有命名仍带有“网络服务”历史语义；迁移时应避免上层继续新增对旧命名行为的误解。
- 当前主界面 `Bluetooth` 按钮已经接入真实 BLE 控制逻辑，迁移 WiFi 主入口时需要同步重新定义该按钮的产品语义，避免主界面存在两个互相竞争的联网入口。

## 验收标准

- 主界面 `screen_main_Wifi` 只由真实 Wi-Fi 连接状态控制蓝/灰图标
- 点击灰色或蓝色 WiFi 图标，都进入同一个全屏 Wi-Fi 管理界面
- Wi-Fi 管理界面具备：
  - 顶部状态区
  - `进入配网 / 断开联网 / 重连` 三个主按钮
  - `AUTO / BLE / AP` 默认配网方式设置区
- `进入配网` 与 `重连` 的语义能够被状态机正确区分
- 用户主动断开后，系统不会自动重连，必须等待用户手动动作
- 上层新代码不再直接依赖 BLE/AP 底层 transport 接口
