# 蓝牙配网与微信小程序设计

## 背景

当前仓库已经具备一条可工作的本地配网链路：

- `components/wifi_provision` 负责 AP 网页配网协调。
- `components/wifi_provision/src/wifi_driver/wifi_manager.c` 负责 Wi-Fi STA/AP、凭据保存和连接状态。
- `main/network_service.c` 已把联网流程改成后台执行，不再阻塞 UI 启动。

本次目标是在不破坏现有 AP 网页配网的前提下，增加一条“微信小程序 + BLE”的新配网入口，并按用户确认的策略执行：

- BLE 配网是主路径。
- 现有 AP 网页配网保留为兜底路径。
- 第一阶段优先“尽快跑通”，不追求一开始就兼容 Espressif 官方 `wifi_prov_mgr` 标准协议。
- 实施顺序先做 ESP32-S3 端基础代码，再做微信小程序端代码。

## 结论

本任务可行性高，建议采用“自定义 BLE GATT 简化协议 + 复用现有 `wifi_manager` + 保留 AP 兜底”的方案。

不推荐第一阶段直接把当前仓库整体切到 ESP-IDF 官方 `wifi_prov_mgr` BLE 协议，原因是：

- 当前仓库尚未启用蓝牙基础配置，先打开 BLE 能力本身就需要一轮内存和构建验证。
- 官方方案依赖 `protocomm + protobuf + security1/2`，微信小程序端实现成本明显更高。
- 当前仓库已有稳定的 AP 配网底座，先复用现有 Wi-Fi owner 能更快形成最小闭环。

## 分阶段范围

### 阶段 1：ESP32-S3 端基础能力

目标是让固件具备 BLE 配网入口，并与现有 Wi-Fi 路径联通。

阶段 1 范围包括：

- 启用适合 `ESP32-S3` 的 BLE 基础配置，固定采用 `NimBLE`，仅启用 BLE 能力，不引入经典蓝牙。
- 在 `components/wifi_provision` 内新增 BLE 传输层。
- 设计并实现一套轻量自定义 GATT 协议。
- BLE 收到 SSID/密码后，直接复用现有 `wifi_manager_set_credentials()` 和 `wifi_manager_connect()`。
- BLE 配网成功后回传连接结果、IP 和失败状态。
- 保留现有 `wifi_provision_start_apcfg()` 作为兜底入口。
- 联网主流程继续由 `network_service` 负责，不重写当前后台联网模型。

阶段 1 不做：

- 不接入 `wifi_prov_mgr` 官方协议。
- 不做 BLE 侧 Wi-Fi 扫描结果上报。
- 不做复杂安全握手、SRP、PoP 或 protobuf 封装。
- 不改现有 AP 页面 HTML 和网页配网主逻辑。

### 阶段 2：微信小程序端

目标是让用户通过微信小程序完成 BLE 扫描、连接、发送凭据和查看结果。

阶段 2 范围包括：

- 小程序扫描并连接设备 BLE 广播。
- 订阅状态通知 characteristic。
- 用户手动输入 SSID/密码后发送给 ESP32-S3。
- 根据设备返回状态显示“连接中 / 成功 / 失败 / 请切换 AP 兜底”。

阶段 2 仍然不做：

- 不做官方 `protocomm` 客户端兼容。
- 不做复杂路由和账号系统。
- 不做历史设备管理。

## 用户流程

### 首次配网

1. 设备上电，`hardware_init()` 完成基础初始化。
2. `network_service` 发现当前没有已保存 Wi-Fi 凭据。
3. 设备自动进入 BLE 配网广播态。
4. 用户打开微信小程序并扫描到设备。
5. 用户输入 Wi-Fi SSID 和密码并提交。
6. 设备保存凭据，切回 STA 连接目标网络。
7. 设备通过 BLE 通知小程序连接结果：
   - 成功：返回 `connected` 和 IP
   - 失败：返回 `failed`
8. 若 BLE 路径不通，用户可按现有按钮进入 AP 网页配网。

### 已配网设备重连

1. 设备上电。
2. `network_service` 优先调用 `wifi_provision_start_auto()` 走已保存凭据重连。
3. 若成功，正常联网。
4. 若失败，回到当前已有策略：
   - 先进入待重连/失败状态
   - 允许用户使用 BLE 或按钮触发 AP 兜底

## 固件架构设计

### 适合 ESP32-S3 的蓝牙模式

本项目 BLE 配网固定采用：

- `NimBLE`
- 仅启用 BLE 能力
- 不启用经典蓝牙
- 不引入 `Bluedroid`

对应配置基线是：

- `CONFIG_BT_ENABLED=y`
- `CONFIG_BT_NIMBLE_ENABLED=y`

这样选的原因是：

- `ESP32-S3` 的本项目需求只有 BLE 配网，没有经典蓝牙音频或串口透传需求。
- 只启用 `NimBLE` 能把 BLE 能力控制在最小集合，和当前 `LVGL + audio + Wi-Fi + official_chat` 的资源组合更匹配。
- 本地官方例程 `D:\esp-idf\v5.5.3\esp-idf\examples\provisioning\wifi_prov_mgr\sdkconfig.defaults` 也采用了这一档配置。
- 第一阶段目标是“尽快跑通”，`NimBLE` 最小 BLE 能力的实现和验证面最小。

第一阶段不考虑：

- `Bluedroid`
- 经典蓝牙 SPP
- A2DP
- BLE + BR/EDR 双模共存

### 总体原则

- `wifi_manager` 继续作为唯一 Wi-Fi owner。
- BLE 只负责“收发配网数据”和“展示当前配网状态”，不直接接管 Wi-Fi 驱动主流程。
- AP 配网和 BLE 配网共享同一套凭据保存、连接、状态回调和结果判定。
- 先做最小自定义协议，不在第一阶段引入额外序列化框架。

### 模块划分

#### 1. 继续保留 `components/wifi_provision` 为总协调器

该组件继续对外暴露统一配网 API，并在内部同时持有：

- 现有 AP 网页配网路径
- 新增 BLE 配网路径

建议新增 API：

- `wifi_provision_start_blecfg()`
- `wifi_provision_stop_blecfg()`
- `wifi_provision_is_ble_active()`
- `wifi_provision_get_ble_service_name(...)`

现有 API 保持不变：

- `wifi_provision_init(...)`
- `wifi_provision_start_auto()`
- `wifi_provision_start_apcfg()`
- `wifi_provision_set_credentials(...)`
- `wifi_provision_has_credentials()`

这样可以避免引入第二个独立“配网 owner”组件。

#### 2. 新增 BLE 传输子模块

建议在 `components/wifi_provision/src/ble_server` 下新增：

- `ble_provision_transport.c`
- `ble_provision_transport.h`
- `ble_provision_protocol.c`
- `ble_provision_protocol.h`

职责拆分：

- `ble_provision_transport.*`
  - 负责 NimBLE 初始化、广播、GATT service/characteristic 注册、连接和通知。
- `ble_provision_protocol.*`
  - 负责 JSON 命令解析、状态编码和 payload 边界检查。

### 第一阶段 BLE 协议

采用“一个服务 + 两个 characteristic”的最小模型：

- Service：自定义 128-bit UUID
- RX characteristic：`Write / Write Without Response`
- TX characteristic：`Notify / Read`

消息格式统一使用 UTF-8 JSON 文本。

#### 建议命令

小程序发给设备：

```json
{"cmd":"hello"}
{"cmd":"status"}
{"cmd":"set_wifi","ssid":"MyWiFi","password":"12345678"}
{"cmd":"start_ap_fallback"}
```

设备发给小程序：

```json
{"evt":"hello","name":"ESP32S3-AB12","ver":1,"fallback":"ap"}
{"evt":"status","state":"idle"}
{"evt":"status","state":"connecting","ssid":"MyWiFi"}
{"evt":"status","state":"connected","ssid":"MyWiFi","ip":"192.168.1.23"}
{"evt":"status","state":"failed","reason":"connect_fail"}
{"evt":"status","state":"ap_fallback","url":"http://192.168.100.1/"}
```

### 状态机

建议在 `wifi_provision` 内部增加独立于 STA/AP 的配网状态机：

- `IDLE`
- `BLE_ADVERTISING`
- `BLE_CONNECTED`
- `CREDENTIALS_RECEIVED`
- `STA_CONNECTING`
- `STA_CONNECTED`
- `STA_FAILED`
- `AP_FALLBACK_ACTIVE`

状态切换要求：

- BLE 收到有效凭据后才进入 `STA_CONNECTING`
- `WIFI_STATE_CONNECTED` 时进入 `STA_CONNECTED`
- `WIFI_STATE_CONNECT_FAIL` 时进入 `STA_FAILED`
- 用户从小程序请求兜底时进入 `AP_FALLBACK_ACTIVE`

### 启动策略

第一阶段建议采用最稳妥的策略：

- 有凭据：按当前 `network_service` 流程优先自动连网
- 无凭据：自动启动 BLE 配网广播
- AP 页面不自动弹出，继续保留按钮触发

这样做的好处：

- 不会在无凭据时同时拉起 AP + BLE，减少 RAM 和共存压力
- 现有按钮路径不变，回退最直接
- 用户能明确理解“BLE 是主入口，AP 是备用入口”

### 安全边界

为满足“尽快跑通”，第一阶段安全策略故意收敛：

- BLE 配网 payload 不做 `protocomm` 或 SRP 安全握手
- 不做设备端加密存储增强，只沿用当前 NVS 保存逻辑
- BLE 广播仅在无凭据或显式触发时开启
- 配网成功后自动停止 BLE 广播

这意味着第一阶段适合开发板、近距离内网设备和实验验证，不应直接当作生产级安全方案。

后续若要产品化，再单独升级为：

- PoP / PIN 校验
- 官方 `wifi_prov_mgr` 协议
- 更严格的设备身份和会话安全

## 微信小程序设计

### 页面范围

第一阶段小程序只保留一个主页面，避免多路由复杂度：

- 设备扫描区
- 连接状态区
- SSID 输入框
- 密码输入框
- “发送配网”按钮
- “改用 AP 配网”提示区

### 小程序文件划分

建议新建仓库目录：

- `wechat-miniapp/app.js`
- `wechat-miniapp/app.json`
- `wechat-miniapp/app.wxss`
- `wechat-miniapp/pages/index/index.wxml`
- `wechat-miniapp/pages/index/index.js`
- `wechat-miniapp/pages/index/index.wxss`
- `wechat-miniapp/utils/ble-client.js`
- `wechat-miniapp/utils/ble-protocol.js`

职责拆分：

- `ble-client.js`
  - 封装微信 BLE API：扫描、连接、发现服务、写入和订阅通知
- `ble-protocol.js`
  - 封装 JSON 编解码、命令构造和状态解析
- `pages/index/index.js`
  - 处理页面状态、表单和错误提示

### 小程序交互流程

1. 点击“开始扫描”
2. 发现设备名符合前缀，如 `ESP32S3-XXXX`
3. 用户选择一个设备连接
4. 页面自动发送 `hello` 和 `status`
5. 用户填写 SSID/密码并点击提交
6. 页面订阅设备通知并展示：
   - 正在连接
   - 连接成功
   - 连接失败
   - 建议改用 AP 配网

### 为什么不做设备端扫描结果回传

第一阶段不把设备端 Wi-Fi 扫描结果通过 BLE 推给小程序，原因是：

- payload 会明显变长，需要分包、分页或 chunking
- 微信小程序和 NimBLE 的 MTU/通知联调复杂度更高
- 手动输入 SSID/密码更容易快速形成首个闭环

待最小链路跑通后，再单独增加 `scan_wifi` 命令。

## 资源与风险分析

### RAM / Flash

启用 BLE 后需要重点验证：

- `CONFIG_BT_ENABLED`
- `CONFIG_BT_NIMBLE_ENABLED`

主要风险：

- 与当前 `LVGL + audio + official_chat` 组合后的内存峰值叠加
- NimBLE 任务栈与 host/controller 内存占用
- 启用 BLE 后的链接体积增加

当前分区表里 `factory` 已有 `8M`，Flash 空间上总体可行，但仍需以实际 `idf.py build` 产物为准。

### CPU 与实时性

BLE 配网阶段主要是低频 JSON 收发，不是高吞吐场景。

对 CPU 的主要影响来自：

- BLE 协议栈后台任务
- Wi-Fi 连接期的共存调度

第一阶段不涉及大流量音频或图像 through BLE，因此 CPU 风险可控。

### Wi-Fi / BLE 共存

ESP32-S3 支持 BLE + Wi-Fi 共存，但本任务要注意：

- 正在 STA 连接时不要频繁切换 BLE 广播状态
- AP 兜底与 BLE 不建议默认同时开启
- 成功联网后应尽快停 BLE，回收资源

### 启动与事件循环风险

当前 `wifi_manager_init()` 已负责：

- `esp_netif_init()`
- `esp_event_loop_create_default()`
- `esp_wifi_init()`

因此 BLE 侧不能重复初始化与 Wi-Fi 冲突的全局基础设施，尤其要避免：

- 重复创建默认 event loop
- 重复创建同名 netif

## 验证计划

### 固件侧

最小验证闭环：

1. `sdkconfig.defaults` 增加 BLE 关键配置
2. 按项目规则执行：
   - `. D:\esp-idf\v5.5.3\esp-idf\export.ps1`
   - `idf.py fullclean`
   - `idf.py build`
3. 烧录后观察日志：
   - 无凭据时自动进入 BLE 广播
   - 收到 SSID/密码后开始 STA 连接
   - 成功后打印 IP 并停止 BLE
4. 按键触发 AP 网页配网，确认旧路径不回归

### 小程序侧

最小验证闭环：

1. 能扫描到设备
2. 能建立 BLE 连接
3. 能成功写入 `set_wifi`
4. 能收到 `connecting / connected / failed`
5. 失败时能看到 AP 兜底提示

## 回滚策略

若 BLE 路线引入构建或运行时不稳定，回滚策略应保持最小化：

1. 只关闭 BLE 相关 `sdkconfig.defaults` 配置
2. 在 `wifi_provision` 中去掉 `wifi_provision_start_blecfg()` 调用
3. 保留现有 AP 网页配网代码不动

这样可以快速回退到当前已工作的 AP-only 版本。

## 后续演进

待第一阶段跑通后，再考虑以下增强项：

- 小程序端增加设备端 Wi-Fi 扫描结果展示
- 增加 BLE 配网超时和重试策略
- 增加简易 PIN / PoP
- 评估是否迁移到 ESP-IDF 官方 `wifi_prov_mgr`

## 参考依据

- 本地官方例程：`D:\esp-idf\v5.5.3\esp-idf\examples\provisioning\wifi_prov_mgr`
- ESP-IDF Wi-Fi Provisioning 文档：<https://docs.espressif.com/projects/esp-idf/en/v5.1/esp32/api-reference/provisioning/wifi_provisioning.html>
