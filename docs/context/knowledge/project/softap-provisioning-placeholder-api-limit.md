---
id: softap-provisioning-placeholder-api-limit
tags: [project, wifi, provisioning, softap, ap-portal, network-manager, diagnostics]
summary: 记录 SoftAP 门户从旧设备侧 JSON 占位 API 迁移到浏览器端官方 provisioning client 之后的真实边界：主链路已切到 `prov-*` endpoint，旧 `/api/scan`、`/api/configure` 仅保留兼容提示。
last_reviewed: 2026-04-24
---

# SoftAP 门户官方客户端迁移边界

## 结论

- 自 `2026-04-22` 起，SoftAP 门户正式主链路已迁到“浏览器端官方 provisioning client”。
- 门户页面现在通过官方 `proto-ver / prov-session / prov-scan / prov-config` 完成版本探测、会话建立、扫描和凭据下发。
- 旧 `/api/scan` 与 `/api/configure` 不再承载真实配网逻辑，只保留兼容提示接口。
- 自 `2026-04-24` 起，SoftAP 门户开始补齐“自动弹页外壳”：`ap_portal_adapter` 负责 404 redirect、DNS 劫持与 DHCP Option 114，官方 `prov-*` endpoint 继续只负责真实配网协议。
- SoftAP 主链路若出现“AP 能起来，但浏览器始终配不成”，还要重点检查：
  - `ap_portal_adapter_start()` 是否真的接进了 SoftAP 生命周期
  - 复用 HTTPD 的 `max_uri_handlers` 是否足够容纳“自定义门户路由 + 官方 `prov-*` endpoint”
  - `network_prov_scheme_softap_set_httpd_handle()` 是否传入了 `httpd_handle_t` 变量地址，而不是句柄值本身
  - SoftAP transport 启动前是否已准备好默认 AP netif（`esp_netif_create_default_wifi_ap()`）
- 板端日志若出现以下组合，通常说明 SoftAP transport 生命周期正常：
  - `network_prov_mgr: Provisioning stopped`
  - `wifi:mode : sta (...) + softAP (...)`
  - `network_prov_mgr: Provisioning started with service name : NET_PROV_AP`
  - `NETWORK_SERVICE: network state: ... -> PORTAL_REQUIRED`
- 这类情况下，如果浏览器页面能打开但仍无法完成扫描/配置，优先怀疑：
  - `components/ap_portal_adapter/src/ap_portal_routes.c`
  - 是否已暴露 `/prov_client.js` 与 `/prov_proto_bundle.js` 静态资源路由
  - 旧 `/api/scan` 与 `/api/configure` 会返回兼容提示，而不是实际执行配网
  - Captive Portal 壳层是否已启动：DNS 是否把系统探测域名回答到 `WIFI_AP_DEF`，404 是否被重定向到 `/`
  - DHCP Option 114 是否已经写到 SoftAP DHCP server，且 URI 缓冲是否保持全生命周期有效

## 证据

- 上下文卡：
  - `docs/context/knowledge/project/network-provisioning-custom-upper-architecture.md`
  - `docs/context/knowledge/project/storage-and-provisioning-paths.md`
- 代码：
  - `components/ap_portal_adapter/src/ap_portal_routes.c`
  - `components/ap_portal_adapter/src/ap_portal_adapter.c`
  - `components/captive_portal_dns/src/captive_portal_dns.c`
  - `components/network_manager/src/network_manager.c`
  - `components/ap_portal_adapter/web/app.js`
  - `components/ap_portal_adapter/web/prov_client.js`
- 关键代码事实：
  - 页面入口 `app.js` 改为 ES module，并继续导入 `prov_client.js`
  - `prov_client.js` 直接访问官方 `proto-ver / prov-session / prov-scan / prov-config`
  - `/api/scan` 与 `/api/configure` 降级为兼容提示 JSON，不再是主流程
  - `ap_portal_adapter` 必须在 SoftAP 路径中显式启动，并把 HTTPD handle 交给官方 provisioning
  - 自动弹页能力不在官方 `network_provisioning` 组件内部，而是由 `ap_portal_adapter` 外挂 Captive Portal 壳层提供
  - 当前壳层由三部分组成：`HTTPD_404_NOT_FOUND -> /`、DNS 劫持到 `WIFI_AP_DEF`、DHCP Option 114
  - 当前仓库使用的 IDF `protocomm_httpd_start()` 在 external HTTPD 模式下，会把 `config->data.handle` 当成 `httpd_handle_t *` 再解引用一次
  - 因此 `network_prov_scheme_softap_set_httpd_handle()` 这里必须传“句柄变量地址”，不能直接传句柄值本身
  - 官方 `wifi_prov_mgr` SoftAP 示例会在启动 provisioning 之前显式调用 `esp_netif_create_default_wifi_ap()`；如果仓库长期只建了 STA netif，可能出现“客户端能连上 AP，但 official provisioning HTTP 请求一进来就被设备端直接断开”的症状
  - 若 HTTPD handler 槽位不足，会在 URI 注册阶段直接返回 `ESP_ERR_HTTPD_HANDLERS_FULL`
  - `network_manager_reprovision()` 会先 stop active transport，再 start 当前默认 transport

## 如何解读日志

- `NET_PROV_BLE` 出现，说明 BLE provisioning 入口已经成功拉起。
- 切到 SoftAP 后出现 `NET_PROV_AP`，说明 transport 切换路径本身也是通的。
- 日志里没有看到“收到 Wi-Fi 凭据并开始 STA 连接”的后续证据时，不要先断定是 Wi-Fi 驱动或 NimBLE 崩了。
- 对 SoftAP 路径，先确认浏览器是否已成功加载 `prov_client.js` 与 `prov_proto_bundle.js`，并真正访问了官方 `prov-*` endpoint。
- 若目标是“连上热点后系统自动弹页”，还要单独检查系统是否先打到了未知探测路径，再由 404 redirect 引到 `/`；不能只看首页能否手动打开。
- SoftAP 成功后，设备会主动停止 provisioning 并关闭 AP/HTTPD；浏览器随后继续轮询
  `prov-config` 时，可能只看到网络断开，而不是一个“最终成功响应”。
- 因此门户前端不能把“ApplyConfig 之后的 fetch 网络错误”一概当成失败；若错误发生在
  轮询连接状态阶段，更合理的解释通常是“设备已接受凭据并正在切回 STA”。
- 如果日志只看到 `NET_PROV_AP` 和 AP STA `join/leave`，但看不到任何后续配网行为，优先怀疑门户 HTTPD 没真正启动，或 URI handler 槽位不够导致关键路由未挂上。
- 如果日志在 `AP 门户 HTTPD 已启动并复用给 SoftAP provisioning` 之后立刻崩在 `httpd_find_uri_handler()` / `httpd_register_uri_handler()`，优先怀疑 external HTTPD handle 传错了一层指针。
- 如果仍有旧页面或旧脚本调用 `/api/scan`、`/api/configure`，现在只会收到兼容提示，不会触发真实配网。
- 如果门户首页能手动打开，但系统始终不自动弹页，优先检查 Captive Portal DNS 是否真的占住 UDP/53，以及 DHCP Option 114 URI 是否跟随当前 SoftAP IP 更新。

## 当前适用边界

- 本卡说明“当前 SoftAP 门户正式主链路已迁到浏览器端官方 provisioning client”后的有效边界。
- 它不证明 BLE 客户端链路一定可用；BLE 仍需要独立客户端去完成官方 provisioning 协议交互。
- 如果未来要彻底删除旧 `/api/scan`、`/api/configure` 兼容提示接口，这张卡还需要继续更新。
