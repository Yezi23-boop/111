# SoftAP Official Provisioning Client Design

**日期：** 2026-04-22

## 背景

当前仓库已经完成以下链路：

- `network_provisioning_adapter` 可以启动官方 `espressif/network_provisioning` 的 `SoftAP` transport
- `ap_portal_adapter` 可以启动自定义门户页面，并与官方 `protocomm_httpd` 复用同一个 HTTPD handle
- Wi-Fi 管理页可以把默认 transport 切到 `SoftAP`，并通过 `network_manager_reprovision()` 进入 `PORTAL_REQUIRED`

但当前门户网页仍然配不成网，根因不是 SoftAP transport 没起来，而是：

- `GET /api/status` 只返回占位状态
- `POST /api/scan` 与 `POST /api/configure` 当前都返回 `501 Not Implemented`

在进一步讨论后，新的设计目标从“尽快补通”调整为“长期解耦”：

- UI 界面允许完全自定义
- 协议层不再塞在设备侧 JSON bridge
- 门户前端自己实现官方 provisioning client
- 设备侧尽量只保留静态页面资源和官方 `prov-*` endpoint

因此本轮目标不是新增一套设备侧 JSON 协议，而是打通“浏览器自定义 UI -> 前端 official provisioning client -> 官方 `prov-*` endpoint -> official provisioning manager”的闭环。

## 目标

- UI 外观与交互可以自由定制
- 协议层和 UI 层解耦
- 前端直接对接官方 `prov-session / prov-scan / prov-config / prov-ctrl / proto-ver`
- 设备侧不再长期维护一套自定义 `/api/scan`、`/api/configure` 桥接协议
- 尽量复用官方 `network_provisioning` 已有的会话、扫描、配置、状态机能力

## 非目标

- 不修改当前 Wi-Fi 管理页交互语义
- 不在设备侧额外扩一层新的业务 JSON bridge
- 不在本轮引入新的安全版本；继续沿当前 `NETWORK_PROV_SECURITY_0`
- 不顺手重构 `network_manager`、`wifi_control`、`network_provisioning_adapter` 的大边界
- 不把浏览器页面退回成官方默认示例 UI；仍保留当前项目自定义页面入口

## 方案比较

### 方案 A：设备侧 JSON bridge 到官方 provisioning

做法：

- 保留 `/api/status`、`/api/scan`、`/api/configure`
- 设备侧把 JSON 请求转换成对官方 provisioning 的调用
- 返回给网页的仍是简化 JSON

优点：

- 前端改动最小
- 不需要把 protobuf/protocomm 带进网页
- 设备侧可以统一兜底错误映射与日志

缺点：

- 设备侧长期要维护“官方协议 + 自定义桥接协议”两层契约
- 协议层和 UI 层没有真正解耦，只是把耦合转移到固件端
- 扫描路径需要谨慎处理，因为官方公开 API 对 scan 的暴露不完整

### 方案 B：自定义 UI + 前端 official provisioning client

优点：

- 最符合“自定义 UI、协议层独立”的长期解耦目标
- 设备侧最干净，只提供静态资源与官方 `prov-*` endpoint
- UI 想怎么画都行，只要调用前端 `prov_client`

缺点：

- 前端必须理解 protocomm/protobuf 和官方会话流程
- 页面脚本改动会明显大于 JSON bridge 路线

### 方案 C：`configure` 走官方 API，`scan` 自己实现

优点：

- 配置链路简单
- 不需要碰 protocomm

缺点：

- 扫描逻辑与官方 SoftAP 策略分叉
- 后续更容易出现“网页扫描结果”和官方 provisioning 实际行为不一致

## 选型

采用方案 B。

理由：

- 用户当前明确优先级是“解耦”，而不是“前端最少改动”
- UI 层与协议层分离后，后续改页面风格、布局、品牌文案时不需要碰 provisioning 协议实现
- 设备侧不再背一套额外 JSON API，长期边界更清晰：门户页面是页面，provisioning 协议是官方协议

## 设计

### 1. 总体结构

改成两层前端结构：

- `portal_ui`
  - 只负责界面、按钮、列表、表单、状态提示
- `prov_client`
  - 只负责官方 provisioning 协议
  - 管理 `proto-ver / prov-session / prov-scan / prov-config / prov-ctrl`

设备侧结构保持收敛：

- `ap_portal_adapter`
  - 负责静态页面资源与门户入口
- `network_provisioning_adapter`
  - 继续负责拉起官方 `SoftAP` provisioning manager
- 官方 `protocomm_httpd`
  - 继续提供 `prov-*` endpoint

页面与协议的关系变成：

- UI 不直接理解设备底层状态机
- UI 只调用前端 `prov_client`
- `prov_client` 才负责官方会话与消息编解码

### 2. 前端协议客户端职责

`prov_client` 需要实现以下能力：

- 获取并校验 `proto-ver`
- 建立 `prov-session`
- 发起 Wi-Fi 扫描
- 拉取扫描状态与扫描结果
- 提交 Wi-Fi 凭据
- 触发 apply config
- 轮询 Wi-Fi provisioning 状态
- 在必要时调用 `prov-ctrl` 做 reset/reprov

由于当前设备使用 `NETWORK_PROV_SECURITY_0`，本轮不需要引入加密握手，但前端仍然必须保留“session 先建立，再访问其它 endpoint”的流程边界。

### 3. 页面分层

页面脚本拆成两个职责文件：

- `portal_ui.js`
  - 绑定 DOM
  - 渲染网络列表
  - 收集 `ssid/password`
  - 更新 loading / warning / error / success 提示
- `prov_client.js`
  - 隔离 protobuf/protocomm 细节
  - 对外只暴露高层方法，例如：
    - `initSession()`
    - `scanWifi()`
    - `sendWifiConfig(ssid, password)`
    - `applyWifiConfig()`
    - `getWifiStatus()`

这样以后 UI 重画时，可以只换 `portal_ui.js + html/css`，不必重写 provisioning 协议部分。

### 4. 扫描流程

扫描流程按官方 SoftAP 客户端约定执行：

1. 确保 session 已建立
2. 发送 `prov-scan` start 请求
3. 查询扫描状态
4. 分批拉取扫描结果
5. 在前端将结果整理成页面需要的结构

页面层如果需要排序、文案高亮、加锁图标等视觉处理，只放在 UI 层做，不要回写进协议层。

### 5. 配置流程

配置流程按官方客户端约定执行：

1. 确保 session 已建立
2. 发送 `prov-config` set config
3. 发送 `prov-config` apply config
4. 轮询 `prov-config` 状态，直到连接成功或失败

页面层不要把“配置请求已发出”直接显示成“联网成功”。
必须区分：

- credentials sent
- connecting
- connected
- failed

### 6. 状态与错误处理

前端 `prov_client` 需要统一把官方响应翻译成稳定的前端内部状态，例如：

- `idle`
- `session_ready`
- `scanning`
- `scan_done`
- `config_sent`
- `connecting`
- `connected`
- `connect_failed`
- `protocol_error`

同时统一错误分类：

- `transport_error`
- `protocol_error`
- `invalid_response`
- `session_error`
- `scan_error`
- `configure_error`

这样 UI 可以自由决定怎么提示，而不需要理解 protobuf 字段细节。

### 7. 设备侧变化

设备侧改动尽量最小：

- 保留官方 `prov-*` endpoint
- 保留当前自定义门户静态页面入口
- 现有 `/api/status` 可保留作门户可达性/静态信息用途，但不再作为正式配网主通道
- `/api/scan`、`/api/configure` 可删除、废弃，或保留为显式“已迁移到官方客户端”的兼容占位接口

如果保留旧 `/api/*` 路径，建议它们只用于兼容提示，不再承载真正配网逻辑，避免未来出现“双主线”。

## 文件边界

重点改动文件预计为：

- `components/ap_portal_adapter/web/app.js`
  - 收敛为 UI 入口或拆分后保留壳文件
- `components/ap_portal_adapter/web/prov_client.js`
  - 新增前端官方 provisioning client
- `components/ap_portal_adapter/web/index.html`
  - 若需要调整脚本加载顺序或状态区域结构，做最小修改
- `components/ap_portal_adapter/src/ap_portal_routes.c`
  - 视情况保留 `/api/status`
  - `/api/scan`、`/api/configure` 不再作为正式主链路
- `components/network_provisioning_adapter/src/network_provisioning_adapter.c`
  - 仅在需要补明确日志/状态说明时做最小调整

测试预计涉及：

- `tests/test_ap_portal_http_api_source.py`
- 若仓库已有前端静态资源/源码级测试，再补一份针对 `prov_client` 的资源或源码约束测试

## 验证计划

### 源码级

- 校验页面脚本不再把 `/api/scan`、`/api/configure` 当主路径
- 校验新增 `prov_client` 已覆盖 `proto-ver / prov-session / prov-scan / prov-config`
- 校验 UI 与协议层是分文件或分模块职责
- 校验设备侧仍保留官方 SoftAP provisioning 启动路径

### 构建级

- 先执行 `D:\esp-idf\v5.5.3\esp-idf\export.ps1`
- 然后执行 `idf.py build`

若本轮涉及 `sdkconfig`，则必须先：

- `idf.py fullclean`
- 再 `idf.py build`

### 运行级

最小闭环：

1. 进入 Wi-Fi 管理页
2. 切到 `SoftAP`
3. 点击 `Reprovision`
4. 浏览器连接设备 SoftAP 门户
5. 页面先完成 `proto-ver + prov-session`
6. 点击扫描，确认通过官方 `prov-scan` 返回真实 Wi-Fi 列表
7. 提交 SSID/密码，确认通过官方 `prov-config` 下发并进入连接中
8. 观察板端日志中 provisioning/Wi-Fi 状态迁移

成功标准：

- 页面不再依赖 `/api/scan`、`/api/configure`
- 扫描能经由官方 `prov-scan` 返回真实列表
- 配置请求能经由官方 `prov-config` 下发到 provisioning manager
- 后续联网成功或失败的结果能从现有状态链路观察到

## 风险

- 前端需要接入 protobuf/protocomm，脚本复杂度会上升
- SoftAP 场景下扫描过重，可能影响 AP beacon 与门户连通性
- 官方 manager 在成功后可能自动停止 provisioning，页面上的提示需要接受“请求刚成功，门户很快断开”这一现实
- 若前端协议层和 UI 层没有真正拆开，后续仍会重新耦合在一起

## 回滚

- 保持当前文件边界不变
- 若前端 official client 路线推进不顺，可回退到页面占位态或重新启用设备侧 JSON bridge 方案
- 不改 Wi-Fi 管理页和 `network_manager_reprovision()` 的外部语义，因此回滚范围主要落在门户静态资源与 `ap_portal_adapter` 局部路由
