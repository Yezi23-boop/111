---
id: adr-network-provisioning-custom-upper-architecture
tags: decisions, wifi, provisioning, ble, ap, architecture, esp-idf
summary: 用官方 network_provisioning 替换旧自定义 provisioning 内核，同时保留自定义上层网络架构与 AP 网页前端。
last_reviewed: 2026-04-18
memory_type: procedural
scope: repo
owners: docs/context/decisions
triggers: adr, decision, ADR, 20260418, network, provisioning, custom, upper, architecture
evidence_level: design
---
# ADR: 采用官方 `network_provisioning` + 自定义上层网络架构

- Date: 2026-04-18
- Status: accepted
- Context:
  - 当前仓库已形成较清晰的上层 Wi-Fi 产品语义：主界面只保留 Wi-Fi 入口，Wi-Fi 管理页承接联网动作与 transport 设置。
  - 旧 `components/wifi_provision` 同时承担 BLE/AP 配网 transport、部分 Wi-Fi runtime control 与部分策略编排，边界已不足以支撑后续演进。
  - 用户希望同时学习底层与架构，并要求引入真正的 BLE 总开关、recent Wi-Fi 列表，以及长期可维护的网络分层。
  - 用户已确认 AP 路径优先保留浏览器自定义网页体验，微信小程序路径后续再单独作为 BLE 客户端接入。
- Decision:
  - 底层 provisioning 内核切换为官方 `espressif/network_provisioning`。
  - 新建自定义上层组件：
    - `network_manager`
    - `wifi_control`
    - `ble_control`
    - `network_credentials`
    - `network_provisioning_adapter`
    - `ap_portal_adapter`
  - 主界面恢复蓝牙图标，并将其正式定义为 BLE 总开关，而非 BLE 配网主入口。
  - 默认 provisioning transport 不再保留 `AUTO`，只保留 `BLE / SOFTAP`。
  - 开机自动连接 recent Wi-Fi 失败后，设备自动进入用户当前设置的 provisioning transport。
  - AP 路径采用“自定义网页前端 + 官方 SoftAP provisioning 内核”，不修改官方组件源码。
  - 微信小程序路径本轮不实现，后续只作为 BLE provisioning 客户端接入。
- Consequences:
  - 旧 `components/wifi_provision` 将不再作为长期底座，迁移完成后删除。
  - 当前 `main/services/network_service` 仅允许作为过渡期薄 shim，最终由 `network_manager` 取代。
  - 旧自定义 BLE JSON GATT 配网协议将退出长期主线。
  - recent Wi-Fi 列表、BLE 总开关、Wi-Fi runtime control、provisioning transport 的职责边界将明显清晰。
  - 页面层、adapter 层、官方 provisioning 内核层将形成更适合学习与维护的分层结构。
- Rollback Plan:
  - 若官方 `network_provisioning` 与当前项目实际约束冲突过大，可在保留新上层组件命名和边界的前提下，将 `network_provisioning_adapter` 暂时改为适配旧自定义 provisioning 内核，避免重新回到旧命名与旧耦合结构。
  - 若 AP 自定义网页前端与 SoftAP provisioning 复用单 HTTPD handle 的方案验证失败，可退回为“官方 SoftAP provisioning + 独立自定义页面服务”双实例模式，再评估是否继续保留网页壳。
