---
id: wifi-provision-removal-migration-checklist
tags: [project, migration, wifi, provisioning, ble, softap, architecture]
summary: 梳理删除旧 components/wifi_provision 前必须完成的接口迁移、真实依赖点、阶段顺序、风险与验证方法。
last_reviewed: 2026-04-21
---

# `wifi_provision` 删除迁移清单

## 结论

- 当前仓库已经完成旧 `components/wifi_provision` 的退场。
- 这次迁移的最终结果是：
  - 运行时代码、构建依赖、源码测试都已不再依赖旧组件
  - 网络正式 owner 已收敛为 `network_manager + wifi_control + ble_control + network_provisioning_adapter + ap_portal_adapter`
  - `components/wifi_provision` 已物理删除
- 后续如果再遇到历史 `wifi_provision_*` 语义，应按本卡的替代映射迁到新组件，而不是恢复旧目录。

## 最终迁移结果

截至 `2026-04-21` 当前已完成的迁移进度：

- 已完成：
  - `main/app/hardware_init.c` 不再 `#include "wifi_provision.h"`
  - `main/app/hardware_init.c` 不再调用 `wifi_provision_init(...)`
  - `components/official_chat/application.cc` 已改为使用 `wifi_control_set_power_save(...)`
  - `components/official_chat/mcp_server.cc` 已改为使用 `wifi_control_is_connected()` / `wifi_control_get_ip(...)`
  - `components/wifi_control` 已补 `wifi_control_set_power_save(bool)`
  - `components/official_chat/CMakeLists.txt` 已显式增加 `wifi_control` 依赖
  - `main/CMakeLists.txt` 已移除 `wifi_provision`
  - `components/official_chat/CMakeLists.txt` 已移除 `wifi_provision`
  - 主工程运行时代码（排除 `components/wifi_provision` 自身）已无 `wifi_provision_*` 直接调用
  - 旧 `components/wifi_provision` 源码测试已迁到新架构基线
  - `components/wifi_provision` 已物理删除
- 仍需持续注意：
  - 历史 spec / plan / 知识卡里仍可能出现 `wifi_provision` 作为旧 owner 的描述，阅读时要按“历史信息”理解

### 运行时代码

- `main/app/hardware_init.c`
  - 已不再依赖 `wifi_provision`
- `components/official_chat/application.cc`
  - 已改用 `wifi_control_set_power_save(false/true)`
- `components/official_chat/mcp_server.cc`
  - 已改用 `wifi_control_is_connected()`
  - 已改用 `wifi_control_get_ip(...)`

### 构建依赖

- `main/CMakeLists.txt`
  - 已不再包含 `wifi_provision`
- `components/official_chat/CMakeLists.txt`
  - 已不再包含 `wifi_provision`
- `components/wifi_provision/CMakeLists.txt`
  - 已随旧组件一并删除

### 测试与上下文依赖

- 旧源码测试已迁移：
  - `tests/test_wifi_provision_ble_source.py`
  - `tests/test_wifi_runtime_helper_source.py`
  - `tests/test_ble_wifi_scan_source.py`
  - `tests/test_ble_transport_source.py`
    均已删除
- 新测试基线已切到：
  - `tests/test_network_provisioning_transport_source.py`
  - `tests/test_ap_portal_http_api_source.py`
  - `tests/test_wifi_network_runtime_source.py`
  - `tests/test_nonblocking_boot_source.py`
- 多份历史设计/计划/知识卡仍把 `wifi_provision` 作为 owner，需要在删除阶段同步收口。

## 旧公网接口替代映射表

| 旧接口 | 当前旧语义 | 新归属 / 替代方案 | 迁移说明 |
| --- | --- | --- | --- |
| `wifi_provision_init(cb)` | 初始化旧配网总协调器，并注册旧 Wi-Fi 状态回调 | 删除该调用；启动入口统一收口到 `network_service_start() -> network_manager_start()` | `network_manager_start()` 内部已经会初始化 `ble_control / wifi_control / network_credentials / network_provisioning_adapter`。旧 `cb` 不建议保留 1:1 替代；若未来需要事件通知，应单独设计 `network_manager` 事件面。 |
| `wifi_provision_start_auto()` | 有凭据连 STA，无凭据进旧 AP 配网 | `network_manager_start()` | 新语义已经改为“优先 latest recent，失败后按当前默认 transport 进入 provisioning”。 |
| `wifi_provision_connect_saved()` | 用已保存 STA 凭据重连 | `network_manager_use_latest_wifi()` | 注意语义已收敛为“再次尝试 latest recent”，不再沿用旧 `wifi_manager` 的直接 saved STA 概念。 |
| `wifi_provision_disconnect_sta()` | 主动断开当前 STA | `network_manager_disconnect()` | 新语义会同时进入 `DISCONNECTED_BY_USER`，并暂停自动重连，符合当前 UI 产品定义。 |
| `wifi_provision_start_blecfg()` | 直接启动 BLE 配网 | `network_manager_set_default_transport(BLE)` + `network_manager_set_ble_enabled(true)` + `network_manager_reprovision()` | 若仍暂时走兼容层，也可用 `network_service_request_ble()` 过渡。长期不要再让业务层直连 transport 细节。 |
| `wifi_provision_stop_blecfg()` | 停止 BLE 配网广播 | 当前无完全等价的长期公网接口 | 如果语义是“关闭 BLE 总开关”，用 `network_manager_set_ble_enabled(false)`；如果需要“只停当前 provisioning，但不改变 BLE enabled 偏好”，应先补 `network_manager_stop_provisioning()`，再删旧组件。 |
| `wifi_provision_start_apcfg()` | 直接启动 AP 门户 | `network_manager_set_default_transport(SOFTAP)` + `network_manager_reprovision()` | 若仍暂时走兼容层，也可用 `network_service_request_portal()` 过渡。 |
| `wifi_provision_stop_active_transport()` | 停止当前 BLE/AP transport | 当前无长期公网等价接口 | 删除旧组件前，建议显式新增 `network_manager_stop_provisioning()`；不要把这个责任重新塞回 UI 或兼容层。 |
| `wifi_provision_is_ble_active()` | 查询 BLE transport 是否活跃 | `network_manager_is_ble_active()` | 若调用方还在兼容层，也可暂时用 `network_service_is_ble_active()`。 |
| `wifi_provision_is_ap_active()` | 查询 AP transport 是否活跃 | `network_manager_get_status().state == NETWORK_MANAGER_STATE_PROVISIONING_SOFTAP` | 若调用方还在兼容层，也可用 `network_service_get_wifi_status().ap_active`。 |
| `wifi_provision_get_ble_service_name(...)` | 读取旧自定义 BLE 广播名 | 当前新架构未暴露长期公网替代 | 若微信小程序或调试工具仍需要展示广播名，应把该能力加到 `network_provisioning_adapter`，而不是保留旧 `wifi_provision`。若当前没有真实消费者，可直接删除。 |
| `wifi_provision_is_connected()` | 查询 Wi-Fi 是否已连接 | `wifi_control_is_connected()` 或 `network_manager_get_status().wifi_connected` | 低层 runtime 用 `wifi_control`；业务层/UI 用 `network_manager` 或 `network_service`。 |
| `wifi_provision_get_ip(...)` | 获取当前 IP | `wifi_control_get_ip(...)` 或 `network_service_get_ip(...)` | `official_chat/mcp_server.cc` 这类纯 runtime helper，优先直接迁到 `wifi_control`。 |
| `wifi_provision_set_power_save(bool)` | 控制 Wi-Fi 省电模式 | 当前新架构缺口：应新增 `wifi_control_set_power_save(bool)` | 这是删除旧组件前必须补的一条接口，因为 `official_chat/application.cc` 仍真实依赖它。 |
| `wifi_provision_set_auto_reconnect_enabled(bool)` | 设置底层自动重连 | `wifi_control_set_auto_reconnect_enabled(bool)` | 可直接替换。 |
| `wifi_provision_is_auto_reconnect_enabled()` | 查询底层自动重连 | `wifi_control_is_auto_reconnect_enabled()` | 可直接替换。 |
| `wifi_provision_set_credentials(ssid, password)` | 直接写入 STA 凭据 | 当前不建议保留 1:1 旧语义 | 新架构里 recent list 表达“最近成功连接”，不是“预写入待连接凭据”。如仍需要这个能力，应单独定义 `network_manager_connect_with_credentials(...)` 或 provisioning adapter 注入入口，而不要把 recent list 当成 saved STA 配置。 |
| `wifi_provision_has_credentials()` | 查询是否存在 saved STA 凭据 | `network_manager_get_recent_networks(NULL, 0, &count)` | 新架构里该判断应解释为“recent 列表是否为空”。这和旧 `wifi_manager_has_credentials()` 不是完全同义，迁移时必须显式接受语义变化。 |

## 推荐删除顺序

### 阶段 1：先迁运行时 helper，不碰组件删除

目标：

- 让 `official_chat` 不再依赖 `wifi_provision.h`
- 让主启动链路不再依赖 `wifi_provision_init(...)`

动作：

- 在 `wifi_control` 中补 `wifi_control_set_power_save(bool)`
- 将 `components/official_chat/application.cc` 迁到：
  - `wifi_control_set_power_save(...)`
- 将 `components/official_chat/mcp_server.cc` 迁到：
  - `wifi_control_is_connected()`
  - `wifi_control_get_ip(...)`
- 将 `main/app/hardware_init.c` 中：
  - 删除 `#include "wifi_provision.h"`
  - 删除 `wifi_provision_cb(...)`
  - 删除 `wifi_provision_init(...)`
- 保持冷启动仍只由：
  - `app_main.c` 中的 `network_service_start()`
  - `network_service_start()` 内部的 `network_manager_start()`
    负责网络初始化

阶段完成标志：

- 运行时代码中不再 `#include "wifi_provision.h"`
- 该阶段在当前仓库已完成

### 阶段 2：迁构建依赖和兼容 helper

目标：

- 让主工程和 `official_chat` 构建链不再显式依赖 `wifi_provision`

动作：

- 从 `components/official_chat/CMakeLists.txt` 移除 `wifi_provision`
- 从 `main/CMakeLists.txt` 移除 `wifi_provision`
- 若阶段 1 发现仍有旧 helper 缺口：
  - 优先补到 `wifi_control`
  - transport 生命周期控制补到 `network_manager`
  - 不再新增新的 `wifi_provision` 兼容层

阶段完成标志：

- `main` 与 `official_chat` 的 `REQUIRES` 不再包含 `wifi_provision`
- 该阶段在当前仓库已完成

### 阶段 3：迁源码测试

目标：

- 测试基线从“旧组件存在”切到“新架构已完整承接”

动作：

- 重写或删除直接断言旧源码结构的测试：
  - `tests/test_wifi_provision_ble_source.py`
  - `tests/test_wifi_runtime_helper_source.py`
  - `tests/test_ble_wifi_scan_source.py`
  - `tests/test_nonblocking_boot_source.py`
  - `tests/test_official_chat_source.py`
- 新测试重点改成：
  - `hardware_init` 不再初始化 `wifi_provision`
  - `official_chat` 改用 `wifi_control`
  - `network_manager / network_provisioning_adapter / ap_portal_adapter` 承接 BLE/SoftAP 路径

阶段完成标志：

- 测试不再把 `components/wifi_provision/**` 当成必要存在物
- 该阶段在当前仓库已完成

### 阶段 4：删除旧组件与历史残留

动作：

- 删除：
  - `components/wifi_provision/include/wifi_provision.h`
  - `components/wifi_provision/src/wifi_provision.c`
  - `components/wifi_provision/src/wifi_driver/*`
  - `components/wifi_provision/src/ble_server/*`
  - `components/wifi_provision/src/web_server/*`
  - `components/wifi_provision/html/*`
  - `components/wifi_provision/CMakeLists.txt`
- 更新文档：
  - 把仍把 `wifi_provision` 写成当前 owner 的知识卡、spec、plan 标记为历史信息或同步改写

阶段完成标志：

- 全仓运行时代码、构建依赖、源码测试都不再引用 `components/wifi_provision`
- 该阶段在当前仓库已完成；当前只剩历史文档层面的旧描述需要按需继续收口

## 主要风险

### 1. 启动顺序回归

- 风险：
  - 删除 `wifi_provision_init(...)` 后，如果误以为仍有其他地方初始化 Wi-Fi/BLE，会导致冷启动后网络底座未准备好
- 真实安全边界：
  - 当前应以 `network_service_start() -> network_manager_start()` 为唯一网络启动链路
- 验证重点：
  - 开机日志中仍能看到 `wifi_control_init / ble_control_init / network_provisioning_adapter_init` 对应的成功路径

### 2. `official_chat` 运行时 helper 缺口

- 风险：
  - `wifi_provision_set_power_save()` 当前没有现成新替代，若直接删组件会先打碎 `official_chat`
- 处理原则：
  - 必须先补 `wifi_control_set_power_save(bool)`，再迁 `official_chat`

### 3. “saved credentials” 语义漂移

- 风险：
  - 旧 `wifi_provision_has_credentials()` 表达的是旧 STA 凭据存在；新架构表达的是 recent Wi-Fi 列表非空
- 处理原则：
  - 删除阶段必须显式接受这次语义升级
  - 不要把 `network_credentials` 误当成旧 `wifi_manager` NVS 凭据镜像

### 4. provisioning stop 语义还未完全公网化

- 风险：
  - 旧组件提供了 `stop_blecfg` / `stop_active_transport`
  - 新架构当前主要暴露的是“重连 / 重新配网 / BLE 总开关”，并没有长期公网 `stop provisioning`
- 处理原则：
  - 若删除前出现新的真实调用方，应先把 stop 能力补到 `network_manager`
  - 不要回退到让 UI 或兼容层直接操作 adapter

## 回滚策略

- 阶段 1 和阶段 2 都应单独提交，确保可以逐阶段回滚。
- 在旧组件真正删除前，不要修改 `components/wifi_provision` 内部逻辑去“配合新架构”，避免把回滚面扩大到新旧两边。
- 若某阶段迁移后出现板端联网回归，应优先回滚该阶段对调用方的替换，不要先恢复整套旧组件内部行为。

## 验证清单

### 源码级验证

- `hardware_init.c` 不再包含 `wifi_provision.h`
- `hardware_init.c` 不再调用 `wifi_provision_init(...)`
- `official_chat` 不再调用 `wifi_provision_*`
- `main/CMakeLists.txt` 与 `components/official_chat/CMakeLists.txt` 不再 `REQUIRES wifi_provision`
- `components/wifi_provision` 目录已不存在
- 删除阶段完成后，全仓搜索 `wifi_provision` 允许剩余：
  - 历史文档
  - 负向断言测试
  - 第三方 managed component 内部实现名

### 构建验证

- `idf.py build`
- 删除 `wifi_provision` 组件的最终阶段前后，都各做一次完整构建验证

### 真机验证

- 冷启动后仍能正常进入：
  - latest Wi-Fi 自动重连
  - BLE provisioning
  - SoftAP provisioning
- `official_chat` 页面中：
  - Wi-Fi 连接态读取正常
  - IP 读取正常
  - power save 开关行为保持不回归
- Wi-Fi 管理页中：
  - `Use Saved Wi-Fi`
  - `Disconnect`
  - `Reprovision`
  - `BLE / SoftAP` 选择
    都保持当前产品语义

## 最小实施建议

- 第一轮不要试图“一次删干净全部旧文件”。
- 推荐拆成 4 次独立可回退改动：
  1. 补 `wifi_control_set_power_save` 并迁 `official_chat`
  2. 移除 `hardware_init` 对 `wifi_provision_init` 的依赖
  3. 移除 `CMake` 与测试依赖
  4. 最后删除 `components/wifi_provision`

这条顺序最稳，也最符合当前仓库“官方 provisioning 内核 + 自定义上层架构”的既定方向。
