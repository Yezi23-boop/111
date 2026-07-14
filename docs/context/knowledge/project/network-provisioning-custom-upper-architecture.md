---
id: network-provisioning-custom-upper-architecture
tags: [project, architecture, wifi, provisioning, ble, softap, esp-idf]
summary: 记录当前仓库向“官方 network_provisioning + 自定义上层网络架构”迁移的长期方向、组件分层与当前落地边界。
last_reviewed: 2026-04-25
memory_type: semantic
scope: repo
owners: components/network_manager, components/network_provisioning_adapter, components/ap_portal_adapter, main/services/network/network_service.c
triggers: network-manager, network_provisioning_adapter, ap_portal_adapter, owner, architecture, 分工
evidence_level: observed
route_area: "Network and provisioning"
---

# 官方 provisioning + 自定义上层网络架构

## 结论

- 当前仓库的长期网络主线，已经从历史 `wifi_provision + network_service` 单体式结构，切换到：
  - 官方 `espressif/network_provisioning` 负责底层 provisioning 内核
  - 自定义上层组件负责项目语义与 UI 门面
- 这条路线已经被当前轮设计和本稳定知识卡固化，不再继续把旧自定义 BLE JSON GATT 协议作为长期主线。

## 目标分层

### `network_provisioning_adapter`

- 作用：
  - 适配官方 provisioning manager
  - 统一封装 `BLE / SOFTAP` 两种 transport
- 规则：
  - 单实例
  - 单 transport
  - transport 切换统一走 `stop -> deinit -> init -> start`

### `wifi_control`

- 作用：
  - 只承载 `Wi-Fi STA runtime control`
- 负责：
  - Wi-Fi 初始化
  - 指定 SSID/密码连接
  - 主动断开
  - 自动重连开关
  - 连接状态与 IP 查询
- 不负责：
  - provisioning transport
  - 凭据持久化
  - recent Wi-Fi 列表

### `ble_control`

- 作用：
  - 表达 BLE 总开关语义
- 负责：
  - BLE enabled 偏好持久化
  - BLE enabled / active 状态查询
- 不负责：
  - 直接启动 BLE provisioning 会话

### 当前运行时边界

- `ble_control` 仍只负责偏好与 active 状态本身。
- 但从当前代码起，`network_manager_set_ble_enabled()` 不再只是改偏好位：
  - 关闭 BLE 时，若当前正跑 BLE provisioning，会立即停止 BLE transport
  - 开启 BLE 时，只启动普通 BLE presence 可发现广播，不自动拉起 BLE provisioning
- 因此“BLE 总开关是否真的影响广播/配网入口”的解释权，当前已经收敛到 `network_manager`，而不是 `ble_control` 自己。

### `network_credentials`

- 作用：
  - 保存最近成功连接的 Wi-Fi 记录
- 当前规则：
  - 最多保留 3 条
  - 按最近成功连接排序
  - 自动连接只尝试最新 1 条

### `network_manager`

- 作用：
  - 项目唯一正式网络统一门面
- 负责：
  - 开机尝试 latest Wi-Fi
  - latest 失败后停在空闲态，等待用户在 Wi-Fi 管理页显式选择 provisioning transport
  - 向 UI 暴露：
    - 使用最近网络
    - 断开联网
    - 显式启动 BLE Provision
    - 显式启动 AP Web Fallback
    - BLE 总开关

### `ap_portal_adapter`

- 作用：
  - 浏览器自定义 AP 页面前端桥接层
- 方向：
  - 页面体验仍由当前项目自己控制
  - SoftAP provisioning 底层内核改为官方 manager
- 当前已落地边界：
  - 已新建独立 `ap_portal_adapter` 组件
  - 已能启动最小 HTTPD，并把同一个 `httpd_handle_t` 通过
    `network_prov_scheme_softap_set_httpd_handle()` 交给官方 SoftAP provisioning scheme
  - 已把旧 AP 门户页面资源迁入 `components/ap_portal_adapter/web/`
  - 当前已提供：
    - `GET /`
    - `GET /app.js`
    - `GET /app.css`
    - `GET /api/status`
    - `POST /api/scan`
    - `POST /api/configure`
  - 其中 `/api/scan` 与 `/api/configure` 当前仍是设备侧占位接口，后续需要继续接通真实 provisioning 行为

## UI 语义

### 主界面 `screen_main_Wifi`

- 继续是主网络入口
- 图标语义：
  - 灰色表示当前 Wi-Fi 未连接
  - 蓝色表示当前 Wi-Fi 已连接
- 点击后进入 Wi-Fi 管理页，而不是直接进入 BLE 配网

### 主界面 `screen_main_Bluetooth`

- 恢复显示
- 正式语义改为 BLE 总开关
- 它表达的是 `BLE enabled`，不是“当前是否正在 BLE 配网”
- 当前运行时 checked 状态已按 `network_manager` 的 `ble_enabled` 收敛，而不是按 `ble_active`

### Wi-Fi 管理页

- 仍是联网操作主界面
- 当前目标动作：
  - `Use Saved Wi-Fi`
  - `Disconnect`
  - `BLE Provision`
  - `AP Web Fallback`
- 页面不再要求用户先选择 transport 再点击 `Reprovision`。
- 不再保留 `AUTO`
- 当前 UI 已直接对接 `network_manager`，不再让页面直接依赖旧 `network_service` Wi-Fi façade
- `network_service` 当前已退化为：
  - 旧接口兼容层
  - `service ready` 探测层
  - 不再直接管理 `wifi_provision` 或 transport 生命周期

## 自动回退策略

- 开机先读取 recent Wi-Fi 列表
- 若存在 latest：
  - 先尝试 latest
- 若 latest 连接失败：
  - 停在合法空闲态，等待用户显式点击 `BLE Provision` 或 `AP Web Fallback`
- 若用户显式点击 `BLE Provision`：
  - 启动前必须检查 `ble_control_is_enabled()`
- 若没有 recent，且默认 transport 是 `BLE`，但 BLE 总开关已关闭：
  - 当前会停在合法空闲态
  - 不再把 `network_manager_start()` / `network_service_start()` 直接打成启动失败

## 当前实现进度

截至 `2026-04-21`，当前仓库已确认：

- 已落地：
  - `espressif/network_provisioning` 组件依赖
  - `network_provisioning_adapter` 最小生命周期
  - `wifi_control` 纯 STA runtime control 组件
  - `ble_control`
  - `network_credentials`
  - `network_manager`
  - `ap_portal_adapter` 最小 HTTPD handle 复用接缝
  - `ap_portal_adapter` 页面资源迁移与 HTTP API 壳
  - 主界面 `screen_main_Wifi / screen_main_Bluetooth` 对 `network_manager` 的 UI 接线
  - `wifi_management_controller` 对 `network_manager` 的 UI 接线
  - `network_service` 收敛为 `network_manager` 之上的兼容 shim
  - 旧 `components/wifi_provision` 已物理删除
- 仍待继续：
  - `ap_portal_adapter` 设备侧真实 scan/configure 接口

## 删除旧 `wifi_provision` 的执行入口

- 删除旧组件时，不要再从历史 BLE/AP 设计卡倒推当前代码。
- 应优先参考：
  - `docs/context/knowledge/project/wifi-provision-removal-migration-checklist.md`
- 这张迁移清单已经把：
  - 旧公网接口到新组件的替代映射
  - 当前真实直接依赖点
  - 删除顺序
  - 风险与验证
    单独收口，后续以该卡为准推进。

## 和旧上下文的关系

- 旧 `ble-provisioning-ui-toggle-behavior.md`、`wifi-management-ui-behavior.md` 仍可作为“当前 UI 语义演进历史”的参考。
- 旧 `wifi_provision` 相关 spec / plan / 知识卡，在当前代码基线下都应按“历史方案”理解。
- 但凡涉及“长期网络底座如何分层”的问题，应优先以本卡为准，而不是继续把旧 `wifi_provision` 视为长期正式架构。
