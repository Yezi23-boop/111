---
id: attempt-2026-05-04-softap-captive-portal-official-provisioning
tags: softap, captive-portal, provisioning, attempt-log
summary: softap-captive-portal-official-provisioning；结果：success。
last_reviewed: 2026-05-04
garden_status: keep-evidence
garden_reviewed: 2026-05-16
memory_type: episodic
scope: task
owners: components/ap_portal_adapter/src/ap_portal_adapter.c, components/ap_portal_adapter/src/ap_portal_routes.c, components/captive_portal_dns/src/captive_portal_dns.c, components/network_manager/src/network_manager.c, docs/context/knowledge/project/softap-captive-portal-auto-popup.md
triggers: SoftAP captive portal prov-session prov-config auto popup
evidence_level: observed
route_area: "SoftAP portal evidence"
---

# Attempt Log: softap-captive-portal-official-provisioning

## 背景

- 本次要验证什么：记录 SoftAP 自动弹页和官方 prov-* endpoint 的真实边界，避免后续把门户壳和官方配网协议混在一起重写。
- 对应任务或计划：SoftAP captive portal + official network_provisioning 桥接
- 结果状态：success

## 环境

- 分支/工作区状态：未记录
- 设备/串口/板型：未涉及或未记录
- 关键前置条件：未记录

## 操作

- 修改过的文件或 owner：
- components/ap_portal_adapter/src/ap_portal_adapter.c
- components/ap_portal_adapter/src/ap_portal_routes.c
- components/captive_portal_dns/src/captive_portal_dns.c
- components/network_manager/src/network_manager.c
- docs/context/knowledge/project/softap-captive-portal-auto-popup.md
- 执行的命令或动作：
- 保留官方 proto-ver / prov-session / prov-scan / prov-config endpoint 承担真实配网
- 在 ap_portal_adapter 外挂 DNS 劫持、HTTP 404 redirect 和 DHCP Option 114
- ap_portal_adapter_start() 早于 official SoftAP start 时先确保 WIFI_AP_DEF 存在
- 已尝试但不应直接重复的路径：
- 不要用全量 redirect handler 吞掉 prov-* endpoint
- 不要假设 SoftAP netif 在设置 DHCP Option 114 前已经存在
- 不要把临时凭据或自定义 /api/scan 重新当成正式配网主线

## 观测

- 关键日志/证据：
- softap-captive-portal-auto-popup.md 记录 404 redirect、DNS 劫持和 DHCP Option 114 壳层边界
- 内存记录要求验证 /generate_204 探测、station IP、凭据应用和 SERVICE_READY 回归
- 与预期不一致的点：
- 未记录

## 结论

- 本次可以确认的事实：SoftAP 自动弹页归 ap_portal_adapter；真实配网归官方 network_provisioning prov-* endpoint，门户前端应保持薄壳。
- 仍然不能确认的事实：
- 不同手机系统 captive portal 探测路径差异仍需真机覆盖

## 未验证风险

- 下一轮仍需补证据的边界：
- 若再次排查 SoftAP，先看 AP netif/DHCP Option 114/DNS/HTTPD handle 复用，而不是重写 provisioning 协议
