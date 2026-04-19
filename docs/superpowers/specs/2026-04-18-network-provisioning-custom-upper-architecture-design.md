# 官方 `network_provisioning` + 自定义上层架构设计

## 背景

当前仓库已经形成了较稳定的上层产品语义，但底层网络与配网实现仍主要建立在历史自定义模块之上：

- 主界面已有真实 `Wi-Fi` 状态入口与独立 Wi-Fi 管理页
- BLE/AP 已被收口为 Wi-Fi 内部 transport 的产品认知
- 旧 `components/wifi_provision`
  - 同时承担 BLE/AP 配网 transport
  - 承担部分 Wi-Fi 连接控制
  - 承担部分状态编排
- 旧 `main/services/network_service`
  - 同时承担统一门面、状态聚合与部分配网策略

当前问题是：

- 自定义 BLE/AP 配网协议栈维护成本高
- BLE 总开关仍未真正抽象成“BLE 子系统总控”
- Wi-Fi 连接控制、配网 transport、recent credentials、上层策略仍有耦合
- 当前仓库的长期架构方向，已经开始超过旧 `wifi_provision` 命名和边界能承载的范围

本次设计目标是：

- 底层配网内核切到官方 `espressif/network_provisioning`
- 上层保留并强化当前仓库已经形成的 Wi-Fi 统一门面语义
- AP 兜底保留浏览器自定义网页体验
- 微信小程序路线后置，后续单独作为 BLE provisioning 客户端接入
- 明确 BLE 总开关、Wi-Fi runtime control、recent credentials 与 provisioning transport 的边界

## 设计结论

### 核心路线

- 底层 provisioning 内核：
  - 使用官方 `espressif/network_provisioning`
- 上层架构：
  - 自定义 `network_manager`
  - 自定义 `wifi_control`
  - 自定义 `ble_control`
  - 自定义 `network_credentials`
  - 自定义 `network_provisioning_adapter`
  - 自定义 `ap_portal_adapter`

### 用户路径

- AP 路径：
  - 浏览器访问自定义网页前端
  - 页面通过 `ap_portal_adapter` 与设备交互
  - 底层 SoftAP provisioning 会话仍交给官方 manager
- BLE 路径：
  - 本轮只完成设备端底座
  - 微信小程序客户端后续单独实现

### 主界面产品语义

- `screen_main_Wifi`
  - 继续作为真实 Wi-Fi 状态灯与网络管理页入口
- `screen_main_Bluetooth`
  - 恢复显示
  - 正式语义改为 BLE 总开关
  - 不再承担 BLE 配网主入口语义

### provisioning transport 策略

- 不再保留 `AUTO`
- 默认 provisioning transport 只保留：
  - `BLE`
  - `SOFTAP`
- 设备开机自动尝试 recent list 最新 Wi-Fi
- 若自动连接失败：
  - 自动进入用户当前设置的 provisioning transport

## 非目标

- 本轮不实现微信小程序 provisioning 客户端
- 本轮不继续维护旧自定义 BLE JSON GATT 配网协议
- 本轮不修改官方 `network_provisioning` 组件源码
- 本轮不做最近 3 条 Wi-Fi 的复杂图形管理页
- 本轮不要求一步删除 `network_service`，允许其先作为过渡 shim

## 目标架构

### 1. `wifi_control`

定位：

- 纯 Wi-Fi STA runtime control

职责：

- Wi-Fi 初始化
- 指定 SSID/密码连接
- 主动断开
- 自动重连开关
- 连接状态、IP、SSID 查询

不负责：

- BLE/AP provisioning
- recent Wi-Fi 列表存储
- 上层产品策略

### 2. `ble_control`

定位：

- BLE 子系统总开关控制层

职责：

- 持久化 BLE enabled 偏好
- 提供 `is_enabled()` 查询
- 提供 `is_active()` 查询
- 统一表达“BLE 当前是否允许被业务使用”

不负责：

- 启动 provisioning manager
- 决定 BLE/AP 回退策略
- 直接服务 UI

### 3. `network_credentials`

定位：

- 最近使用 Wi-Fi 列表存储层

职责：

- 最多保存 3 条最近成功连接网络
- 按最近成功连接顺序排序
- 连接成功后前移到队首
- 对外提供 latest 与 list 查询

规则：

- 自动连接只尝试第 1 条
- recent list 更新时机为“连接成功后”，而不是“收到凭据时”

### 4. `network_provisioning_adapter`

定位：

- 官方 `wifi_prov_mgr` 适配层

职责：

- 初始化/启动/停止/反初始化官方 provisioning manager
- 选择 BLE 或 SoftAP scheme
- 对接 provisioning event
- 向上层抛出项目语义事件

规则：

- 单实例、单 transport、显式 stop + deinit + re-init 切换
- 同一时刻只允许一个 active transport

### 5. `ap_portal_adapter`

定位：

- 自定义 AP 网页前端设备侧桥接层

职责：

- 启动页面 HTTP server
- 注册静态页面与页面语义 API
- 向 `network_provisioning_adapter` 提供 HTTPD handle

关键实现方向：

- 通过 `wifi_prov_scheme_softap_set_httpd_handle()` 让官方 SoftAP provisioning 复用自定义页面 HTTP server
- 页面不直接理解官方 provisioning 协议细节

### 6. `network_manager`

定位：

- 项目唯一正式网络统一门面

职责：

- 开机自动连接 recent list 最新网络
- 自动连接失败后进入用户设置的 provisioning transport
- 统一承接 `Use Saved Wi-Fi / Disconnect / Reprovision`
- 聚合当前网络产品层状态
- provisioning 成功后发起 Wi-Fi 连接
- 连接成功后更新 recent list

## 组件目录与迁移映射

建议新增组件：

- `components/network_manager`
- `components/wifi_control`
- `components/ble_control`
- `components/network_credentials`
- `components/network_provisioning_adapter`
- `components/ap_portal_adapter`

### 旧模块迁移方向

- `components/wifi_provision/src/wifi_driver/wifi_manager.*`
  - 拆到 `components/wifi_control`
- `components/wifi_provision/src/ble_server/*`
  - 不保留，迁移完成后删除
- `components/wifi_provision/src/web_server/*`
  - 页面与 HTTP server 能力迁到 `components/ap_portal_adapter`
- `components/wifi_provision/src/wifi_provision.c`
  - 拆散迁移到：
    - `network_provisioning_adapter`
    - `network_manager`
    - `ap_portal_adapter`
- `main/services/network_service.*`
  - 过渡阶段改成薄 shim
  - 最终由 `network_manager` 替代

## `network_provisioning_adapter` 模型

### transport

- `NONE`
- `BLE`
- `SOFTAP`

### state

- `IDLE`
- `INITIALIZING`
- `ACTIVE_BLE`
- `ACTIVE_SOFTAP`
- `STOPPING`
- `ERROR`

### 切换规则

- 不在同一个 active manager 实例内热切换 scheme
- 所有 transport 切换统一走：
  - `stop()`
  - `deinit()`
  - `init(new_scheme)`
  - `start()`

### BLE 启动规则

- 启动前必须检查 `ble_control_is_enabled()`
- 若 BLE 总开关关闭：
  - BLE transport 启动失败
  - 由 `network_manager` 决定后续收口策略

## `network_manager` 最终状态机

建议主状态：

- `IDLE`
- `CONNECTING_LATEST`
- `CONNECTED`
- `PROVISIONING_BLE`
- `PROVISIONING_SOFTAP`
- `DISCONNECTED_BY_USER`
- `ERROR`

### 开机路径

1. 启动后读取 recent list
2. 若存在 latest：
   - 进入 `CONNECTING_LATEST`
3. 若 latest 连接成功：
   - 进入 `CONNECTED`
4. 若 latest 连接失败：
   - 自动进入用户设置的 provisioning transport
   - `BLE` -> `PROVISIONING_BLE`
   - `SOFTAP` -> `PROVISIONING_SOFTAP`

### 用户动作

- `Use Saved Wi-Fi`
  - 再次尝试 latest network
- `Disconnect`
  - 断开连接
  - 关闭自动重连
  - 进入 `DISCONNECTED_BY_USER`
- `Reprovision`
  - 立即进入当前设置的 provisioning transport

### provisioning 成功

1. adapter 收到凭据
2. `network_manager` 调用 `wifi_control_connect()`
3. 若连接成功：
   - 更新 recent list
   - 进入 `CONNECTED`
4. 若连接失败：
   - 进入 `ERROR`

## 主界面与 Wi-Fi 管理页职责

### 主界面 `screen_main_Wifi`

职责：

- 真实 Wi-Fi 连接状态灯
- 进入 Wi-Fi 管理页

图标规则：

- 灰色：未连接
- 蓝色：已连接

### 主界面 `screen_main_Bluetooth`

职责：

- BLE 总开关快捷入口

图标规则：

- 灰色：BLE disabled
- 蓝色：BLE enabled

注意：

- 表达的是 BLE enable state
- 不表达“当前是否正在 BLE 配网”

### Wi-Fi 管理页

职责：

- 展示当前网络处境
- 承接：
  - `Use Saved Wi-Fi`
  - `Disconnect`
  - `Reprovision`
- 承接 provisioning transport 设置

设置项只保留：

- `BLE`
- `SOFTAP`

不再保留：

- `AUTO`

## Public API 方向

### `network_manager`

建议对 UI / 业务层暴露：

- `network_manager_start()`
- `network_manager_get_status()`
- `network_manager_use_latest_wifi()`
- `network_manager_disconnect()`
- `network_manager_reprovision()`
- `network_manager_set_ble_enabled()`
- `network_manager_is_ble_enabled()`
- `network_manager_set_default_transport()`
- `network_manager_get_default_transport()`
- `network_manager_get_recent_networks()`
- `network_manager_connect_recent_by_index()`

### `network_provisioning_adapter`

建议对 `network_manager` 暴露：

- `init()`
- `start_ble()`
- `start_softap()`
- `stop()`
- `switch_transport()`
- `is_active()`
- `get_transport()`
- `get_status()`

## 实施顺序

### 第一阶段：底座接入

- 添加 `espressif/network_provisioning`
- 确认与当前 IDF 版本的构建链兼容
- 建立 `network_provisioning_adapter` 最小闭环

### 第二阶段：控制层拆分

- 从旧 `wifi_manager` 拆出 `wifi_control`
- 新建 `network_credentials`
- 新建 `ble_control`

### 第三阶段：编排层建立

- 新建 `network_manager`
- 接入 recent Wi-Fi 自动连接
- 接入失败后自动进入 provisioning transport 的策略

### 第四阶段：AP 页面接入

- 新建 `ap_portal_adapter`
- 迁移旧 `apcfg.html`
- 复用自定义 HTTPD handle 给 SoftAP provisioning

### 第五阶段：UI 迁移

- 主界面 Wi-Fi 图标改由 `network_manager` 驱动
- 主界面蓝牙图标恢复并接为 BLE 总开关
- Wi-Fi 管理页改为调 `network_manager`

### 第六阶段：收口旧模块

- `network_service` 退化为 shim
- 删除旧 `wifi_provision`

## 验证计划

### 源码级

- `network_manager` 头文件与状态机契约测试
- `ble_control` 总开关语义测试
- `network_credentials` recent list 测试
- `network_provisioning_adapter` transport 生命周期测试
- 主界面蓝牙/Wi-Fi 图标语义测试

### 构建级

- `idf.py build`

### 真机级

- 开机自动连接 latest network
- latest 连接失败后自动进入当前 provisioning transport
- BLE disabled 时，BLE transport 启动被拒绝
- `SOFTAP` 模式下浏览器能正常打开自定义门户页
- provisioning 成功后 recent list 被更新
- 主界面蓝牙图标能真实控制 BLE 总开关

## 风险与边界

- 官方 provisioning manager 无微信小程序现成客户端，本轮只落设备端底座
- 自定义 AP 网页前端需要重做页面与设备之间的接口映射，不能直接复用旧 WebSocket 协议
- 过渡阶段 `network_service` 与 `network_manager` 会并存，需控制好薄 shim 边界
- 若后续需要更高安全性，AP 网页与 BLE 客户端的 security 策略需单独评估

## 决策摘要

- 保留当前仓库已经形成的上层 Wi-Fi 产品语义
- 替换旧自定义 provisioning 内核为官方 `network_provisioning`
- AP 路径采用“自定义网页前端 + 官方 SoftAP provisioning 内核”
- 微信小程序后续只做 BLE provisioning 客户端
- 主界面恢复蓝牙图标，并将其正式定义为 BLE 总开关
- 默认 provisioning transport 只保留 `BLE / SOFTAP`
