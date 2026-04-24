---
id: softap-captive-portal-auto-popup
tags: [project, wifi, softap, captive-portal, ap-portal, provisioning]
summary: 记录当前仓库 SoftAP 自动弹页能力的真实归属边界：官方 `prov-*` endpoint 负责真实配网，自动弹页由 `ap_portal_adapter` 外挂的 DNS 劫持、404 redirect 与 DHCP Option 114 壳层提供。
last_reviewed: 2026-04-24
---

# SoftAP 自动弹页壳层边界

## 结论

- 当前仓库的 SoftAP 自动弹页能力不在官方 `network_provisioning` 组件内部。
- 官方 `proto-ver / prov-session / prov-scan / prov-config / prov-ctrl` 只负责真实配网协议。
- 自动弹页由 `ap_portal_adapter` 挂载的 Captive Portal 壳层提供，核心是：
  - DNS 劫持：把系统探测域名回答到 `WIFI_AP_DEF`
  - HTTP 404 redirect：把未知探测路径引到 `/`
  - DHCP Option 114：把门户 URI 写进 SoftAP DHCP server
- 不能用“全量重定向 handler”吞掉所有请求；否则会误伤已注册的门户资源和官方 `prov-*` endpoint。

## 代码归属

- `components/ap_portal_adapter/src/ap_portal_adapter.c`
  - 负责 SoftAP 门户 HTTPD 生命周期
  - 负责设置 DHCP Option 114
  - 负责启动/停止 Captive Portal DNS
- `components/ap_portal_adapter/src/ap_portal_routes.c`
  - 负责门户静态资源
  - 负责 `HTTPD_404_NOT_FOUND` redirect 到 `/`
- `components/captive_portal_dns/src/captive_portal_dns.c`
  - 负责 UDP/53 DNS 劫持
- `components/network_manager/src/network_manager.c`
  - 只负责编排 SoftAP provisioning 生命周期，不应直接持有 DNS/redirect 细节

## 实施边界

- 必须保留官方 `prov-*` endpoint 的原始行为，不能把它们重定向成门户 HTML。
- DHCP Option 114 的 URI 缓冲必须是静态存储，因为 `esp_netif_dhcps_option()` 只保留指针，不复制字符串内容。
- `ap_portal_adapter_start()` 的执行时机早于 `network_provisioning_adapter_start_softap()`；若这里直接假设
  `WIFI_AP_DEF` 已经存在，会在设置 DHCP Option 114 时得到 `ESP_ERR_INVALID_STATE`。当前实现需要先确保默认
  AP netif 已补建，再继续写 URI。
- 停止顺序以“先停 DNS、再解绑 official HTTPD handle、最后停 HTTPD”为基线，避免下一轮 SoftAP 周期重启时端口和句柄残留。
- Captive Portal 活跃期的 `httpd_uri / httpd_txrx / httpd_parse` 探测噪声日志可以在
  `ap_portal_adapter` 启动时临时降到 `ERROR`，并在 stop 时恢复，避免 Android/iOS 的探测
  `404 / bad request / socket recv 113` 警告淹没真正的配网主线日志。

## 验证重点

- 手机或电脑连接 SoftAP 后，系统是否会访问未知探测路径并被 303 到 `/`
- 门户首页加载后，`prov_client.js` 是否仍能正常访问官方 `prov-*`
- SoftAP 停止时 DNS/HTTPD 是否一并收尾，避免后续 UDP/53 bind 失败
