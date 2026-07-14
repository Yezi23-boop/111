---
id: watch-interface-config-layering-review
tags: project, architecture, layering, interface, config, owner, review
summary: 记录 2026-07-07 多 agent 对当前 ESP32-S3 手表固件接口配置与层级衔接的专项审查结论：网络、Hermes、UI/generated、CMake/include 公共面主线可用，但存在 sdkconfig token 治理、contract 漂移、getter 副作用、UI 重动作、generated 依赖 service 和 public header 泄漏等问题。
last_reviewed: 2026-07-14
memory_type: project_knowledge
scope: repo
owners: docs/context/knowledge/project/watch-interface-config-layering-review.md
triggers: 接口配置, 配置入口, 层级衔接, Kconfig, NVS, sdkconfig, SoftAP, watch endpoint, network_manager, generated, public header, CMake
evidence_level: review
status: active
---

# Watch Interface Config Layering Review

## 一句话结论

当前项目不是“分层整体失效”，而是配置入口、接口契约和公共 include 面开始发散。

主线仍成立：

```text
App/UI -> Service -> Manager/Domain -> Driver Adapter -> Vendor/SDK
```

但后续如果继续加 Hermes、BLE、SoftAP、天气、IMU 等能力，必须先收敛这些接口边界：

- 配置入口只保留明确 owner。
- 高频 getter 必须是纯快照。
- UI 只提交 intent 和展示 snapshot，不执行 gate/retry/delay。
- `generated` 只保留对象和事件桥接，不直接读 service。
- public header 不把 SDK/raw handle 轻易泄漏给上层。

## 审查范围

本次由主线程和 4 个专项 subagent 只读审查：

- 网络与配置入口：`network_manager`、`network_service`、BLE、SoftAP、default transport。
- UI 层级衔接：`generated`、`custom`、`ui_refresh_policy`、Hermes 页面 controller。
- CMake/include 公共面：`main/CMakeLists.txt`、`components/*/include`、`check_layering.py` 缺口。
- AI Memory Watch / Hermes：Kconfig、NVS、SoftAP、WS、sync、inbox、server contract。

本文件保留初始审查结论，并在下方单独记录后续整改状态。

## 2026-07-14 整改状态

- 已将 host preview 从 `main/ui/agent_preview` 迁到 `tools/ui_preview`。
- 已将 `main/services` 按窄 owner 子目录整理，Memory Watch runtime 收敛到 `services/memory_watch`。
- 已将主屏电量/天气刷新迁到 `main/ui/custom/main_screen_runtime`，`ui/generated` 不再直接 include feature/service。
- 已将天气拆为 `services/weather/weather_service` 与内部 `weather_http_client`；client 返回 DTO，service 独占 mutex 和 snapshot。
- 已确认 IMU 当前链路为 `services/sensors/imu_service -> board_imu + imu_sensor -> qmi8658c`，旧“service 直连 qmi8658c”结论已过期。

## 配置入口矩阵

| 配置项 | 当前入口 | Owner 判断 | 风险 |
| --- | --- | --- | --- |
| Wi-Fi recent 凭据 | `network_credentials` NVS blob | 合理，`network_manager` 编排，`network_credentials` 存储 | `network_manager_get_status()` 读路径会触发 pending 凭据保存/清理 |
| BLE enabled 偏好 | `ble_control` NVS `network_svc/ble_enabled` | 合理，BLE 偏好不归 UI | UI 点击路径目前执行 gate/quiet/retry |
| 默认配网 transport | `network_manager` 内存态默认 BLE | Owner 应为 `network_manager` | 是否持久化语义不清，`network_service` 注释口径不一致 |
| SoftAP captive portal | `ap_portal_adapter` HTTPD/DNS/DHCP option 114 | 合理，adapter 拥有门户壳 | 门户也承接 Memory Watch 配置 schema，需明确为例外或拆扩展层 |
| Memory Watch endpoint | NVS `memory_watch/base_url/device_id/device_token/timeout_ms/allow_http` | 合理，`memory_watch_service` 是配置 owner | 默认 fallback 入口过多 |
| Memory Watch Kconfig fallback | `CONFIG_MEMORY_WATCH_DEFAULT_*` | 可作为开发期 fallback | tracked `sdkconfig` 不应保留真实 device token |
| Hermes/MiMo/API key | server env / Docker data | 合理，ESP32 不持有 | 固件侧不得新增 Hermes/MiMo/API Server 私有配置 |
| Weather API/location | `main/services/weather/weather_http_client.c` | 已收敛 | HTTP/JSON 留在内部 client，snapshot 由 weather service 持有 |
| IMU board facts | `main/app/board_imu.*` | 已收敛 | `imu_service` 通过 `imu_sensor` adapter 使用 QMI8658C |

## 当前合理的接口衔接

- Wi-Fi 管理页直接调用 `network_manager` 表达用户意图是合理的，新 UI 没有继续依赖旧 `network_service` Wi-Fi facade。
- BLE / SoftAP provisioning 主链路仍合理：`network_manager -> network_provisioning_adapter / ap_portal_adapter -> official network_prov_mgr/httpd`。
- Hermes 页面 controller 调用 `memory_watch_service` 的录音、发送、取消、前台状态和 snapshot API 是合理的；UI 不拼 HTTP、不持有 token、不直连 Hermes/MiMo。
- `memory_watch_service` 持有 endpoint/NVS、foreground、pending、conversation、inbox snapshot；`memory_watch_voice_client` 实际承担 watch endpoint HTTP adapter 职责。
- `watch_notification_center` 只管理本地 surfaced ledger 和顶层气泡，不联网、不成为 inbox 持久 owner。

## P0 问题

### P0: tracked `sdkconfig` 中存在默认 device token

`sdkconfig` 当前包含非空 `CONFIG_MEMORY_WATCH_DEFAULT_DEVICE_TOKEN` 配置行。即使它是 watch endpoint device token，不是 Hermes/MiMo/API key，也不应该作为共享默认配置长期留在 tracked `sdkconfig`。

建议：

- 生产/共享配置中保持 `CONFIG_MEMORY_WATCH_DEFAULT_DEVICE_TOKEN` 为空。
- 开发阶段真实 token 走 SoftAP/NVS，或走本地不提交配置注入。
- 已进入 tracked 配置的 token 应按泄露处理，至少轮换。

### P0: watch contract 未覆盖当前真实主路径

`server/watch_voice_endpoint/watch_contract.v1.json` 仍主要覆盖 V1 HTTP：health、voice/text、cancel、inbox。当前真实主路径已经包含：

- `/v1/watch/ws`
- `/v1/watch/sync`

风险：

- WS auth frame、audio frame、ACK、ASR-ready、final reply、sync delta shape 没有 machine-readable contract 约束。
- server tests 和 firmware source tests 有覆盖，但接口事实分散，后续容易漂移。

建议：

- 扩展 `watch_contract.v1.json`，或新增 V2 contract。
- source test 锁住 server route、固件 path 常量、字段名、状态枚举和超时。

## P1 问题

### P1: `network_manager_get_status()` 不是纯快照

当前 `network_manager_get_status()` / `network_manager_get_state()` 会调用 runtime refresh。refresh 会推进状态、保存/清理 pending provisioning 凭据、处理 latest 失败回退。

风险：

- UI 或 service 读状态时可能改变状态。
- 状态推进 owner 变模糊，排障时无法判断是谁改变了网络状态。

建议：

- 新增纯 `network_manager_get_snapshot(out)`。
- UI timer、`network_service_get_wifi_status()`、天气任务等高频读取改用纯 snapshot。
- 状态推进只留在 manager task、adapter event callback 和显式 command。

### P1: 主界面 BLE enable 在 UI 路径执行重动作

主下拉 Bluetooth 点击路径现在会执行 foreground gate、HTTPS quiet window、`network_manager_set_ble_enabled()`，并在内存不足后 `vTaskDelay()` 重试。

建议：

- UI 只提交“enable/disable BLE”意图。
- gate、quiet window、NO_MEM retry、delay 放到 network owner/service task。
- UI 只展示 snapshot/toast。

### P1（已完成）: `generated/widgets_init.c` 直接 include service

`main/ui/generated/widgets_init.c` 直接 include `services/power_service.h` 并调用 `power_service_get_snapshot()` 刷新电量。这破坏 generated 只负责对象结构的边界，也容易被 GUI Guider 重生成覆盖。

建议：

- generated 只保留 LVGL 对象和事件桥接。
- 电量/天气/网络等数据刷新迁到 `custom` controller 或 runtime updater。
- 加 source test 禁止 `main/ui/generated` include `services/*`、`features/*` 和 driver adapter。

完成证据：电量/天气刷新已迁到 `main/ui/custom/main_screen_runtime.*`，`check_layering.py --verbose` 为 `warning_count=0`。

### P1: SoftAP 门户承接 Memory Watch 配置 schema

当前 `ap_portal_adapter` 通过 callback 避免反向依赖 `memory_watch_service`，不回显 token，方向可工作。但它已经持有 `base_url/device_id/device_token/timeout_ms/allow_http` schema，属于门户壳承接产品配置。

建议二选一：

- 明确写成过渡例外，并补 source test：不打印 token、不回显 token、只走 callback、不吞 official `prov-*` endpoint。
- 或后续拆出 app/memory_watch portal extension，`ap_portal_adapter` 只保留 HTTPD 壳和静态路由挂载能力。

### P1（已完成）: `imu_service` 直接依赖 `qmi8658c`

文档推荐链路是：

```text
imu_service -> board_imu -> qmi8658c
```

但当前 `imu_service` 仍直接 include/call `qmi8658c_init_bus()`、`qmi8658c_probe()`、`qmi8658c_config()`。这和 board facts / driver adapter 的长期边界不一致。

建议：

- 后续把 bus/probe/config 接缝收敛到 `board_imu` 或窄 board adapter。
- `imu_service` 保留运行 profile、窗口、状态机和 snapshot。

完成证据：当前实现为 `services/sensors/imu_service -> board_imu + imu_sensor -> qmi8658c`，service 不 include `qmi8658c.h`。

### P1（已完成）: 天气模块混层

`main/services/weather/weather_http_client.c` 直接持有 URL/API key/location、`esp_http_client` 和 JSON 解析。它同时像 feature、service 和 SDK adapter。

建议：

- 抽成 `weather_service` snapshot + 内部 `weather_http_client`。
- API key/location/base URL 迁出 HTTP 函数，先用最小配置结构承接。
- UI 只读 snapshot，不直接触发 HTTP SDK 路径。

完成证据：`services/weather/weather_service` 持有任务、刷新策略与 snapshot；`weather_http_client` 只返回值类型 DTO，不反向写 service。

## P2 问题

### P2: `network_service` 兼容面偏宽

`network_service` 当前定位是 service-ready/probe/legacy shim，但头文件仍暴露 connect/disconnect/reprovision/default transport/request BLE/AP 等产品动作。

建议：

- 冻结 `network_service` 新增产品动作 API。
- 新 UI/feature 网络动作直接走 `network_manager`。
- `network_service` 只保留 ready/probe/power-save/legacy shim。

### P2: `memory_watch_service` public API 偏宽

`memory_watch_service.h` 聚合 endpoint config、foreground、录音、发送、取消、snapshot、conversation、inbox。方向可控，但继续扩展会变成产品总线。

建议：

- 外部 public API 暂不大改，先冻结新增。
- 内部按 inbox store、sync worker、upload/WS、foreground gate 拆文件。
- `copy_endpoint_config()` 视为敏感接口，因为它会复制 device token，只允许明确 owner/bridge 使用。

### P2: public header 泄漏 SDK/raw handle

当前公共面风险包括：

- `ap_portal_adapter.h` 暴露 `esp_http_server.h` / `httpd_handle_t`。
- `co5300_panel.h` 暴露 `esp_lcd_panel_io.h`、raw panel/io handle 和 LCD callback 类型。
- `i2c_manager.h` 暴露 `driver/i2c*.h`、GPIO 常量和 `i2c_master_bus_handle_t`。

有些是当前 driver adapter 协作需要，不一定马上改；但它们让上层更容易越过 owner。

建议：

- 新增 public header source test，默认禁止上层组件公共头泄漏 SDK handle。
- 确需暴露的 adapter 用 allowlist 记录原因。

### P2: `main` 大 component 编译期隔离不足

`main/CMakeLists.txt` 把 app/services/features/ui/generated/fonts 合成一个大 component，并通过 `INCLUDE_DIRS` 和 `REQUIRES` 暴露大量底层 SDK。

建议：

- 短期不拆 component，先补 source test/check。
- 中期评估 `PRIV_REQUIRES` 和目录 include allowlist。
- 不为追求分层一次性大拆 `main`，避免工程震荡。

### P2: `lvgl_port` 反向通知 UI runtime

`lvgl_port` 通过前向声明调用 `ui_refresh_policy_notify_touch()`。当前只通知 activity，不碰 `lv_obj_t *`，风险可控，但方向不干净。

建议：

- 先作为过渡例外记录。
- 后续改为 input activity callback 注册，由 UI/runtime policy 注册回调。

## 检查工具缺口

`uv run python scripts/context/check_layering.py --verbose` 当前只报 preview mock 和一个已知亮度例外，漏掉了更重要的边界问题。

建议新增检查：

- `main/ui/generated` 禁止 include `services/*`、`features/*`、driver adapter 和 SDK HTTP/LCD/Wi-Fi 头。
- `main/ui` 禁止直接 include/call driver adapter；允许 command/snapshot 走明确 allowlist。
- `main/services` 禁止直接 include/call `qmi8658c_*`、`axp2101_*`、`co5300_panel_*`、`touch_ft5x06_*` 等，diagnostic 例外必须登记。
- `components/*` 禁止反向引用 `ui_*`、`main/services`、`main/features`，当前 `lvgl_port -> ui_refresh_policy_notify_touch()` 先列 known exception。
- `components/*/include/*.h` 扫描 SDK/raw handle 泄漏。
- `main/CMakeLists.txt` 扫描底层 SDK 是否通过 `REQUIRES` 暴露给整个 `main`。
- `agent_preview/host_runner/mock` 单独分类，避免 mock warning 掩盖真实问题。

## 推荐整改顺序

1. 处理 P0 配置和 contract。
   - 清理 tracked `sdkconfig` 默认 token。
   - 补 `/v1/watch/ws` 与 `/v1/watch/sync` contract 或 V2 contract。

2. 收敛网络 getter 和 UI BLE 重路径。
   - 新增纯 network snapshot。
   - UI 和天气高频读取迁到纯 snapshot。
   - BLE enable 改为 UI intent，owner task 执行 gate/quiet/retry。

3. 收敛 generated 和检查工具。
   - 把 `widgets_init.c` 中 service 读取迁出。
   - 扩展 `check_layering.py` / source tests，先让真实风险可见。

4. 明确 SoftAP 配置写入边界。
   - 过渡例外 + source test，或拆 memory_watch portal extension。

5. 处理中期结构债。
   - `memory_watch_service` 内部拆文件。
   - weather service / HTTP adapter 拆层。
   - `imu_service -> board_imu -> qmi8658c` 收敛。
   - public header raw handle 泄漏加 allowlist 或收窄。

## 不建议立即做

- 不推倒现有分层。
- 不新增大 `ResourceManager`、`session_router` 或默认 `ui_manager`。
- 不马上大拆 `main` component。
- 不为了命名洁癖立刻重命名 `memory_watch_voice_client`；先在文档和测试里明确它是 watch endpoint HTTP adapter。
- 不把 Hermes/MiMo/API Server 配置下放到 ESP32。

