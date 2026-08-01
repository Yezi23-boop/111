---
id: plan-2026-08-01-ota-provider-delta-decoupling
tags: plan, ota, provider, delta, onenet, remote-manifest
summary: 将 OneNET 与自建 HTTPS OTA 收敛到通用升级计划，保留三步 UI 与已有 delta/fallback 执行路径。
last_reviewed: 2026-08-01
memory_type: task
scope: task
status: archived
owners: main/services/ota, main/ui/custom/ota_maintenance_view.c, tests
triggers: OTA provider, OneNET delta, self-hosted HTTPS delta, ota_update_plan
evidence_level: evidence
---

# OTA Provider 与 Delta 解耦计划

## 目标

保留 OneNET 完整包和自建 HTTPS manifest 两个来源；provider 只负责协议与任务解析，`ota_service` 只编排 `CHECK -> DOWNLOAD -> ACTIVATE`，transport 只负责 HTTPS、校验、Flash staging、激活和 delta fallback。

## 已完成

- [x] 新增 `ota_update_plan_t`，统一完整包、delta、校验摘要、基线和当前会话 Authorization。
- [x] 新增编译期 `ota_provider` adapter；OneNET 与自建 manifest 不引入运行时注册表。
- [x] OneNET provider 继续负责版本上报、SOTA check、下载 URL/Authorization、pending 状态上报；检查前恢复版本上报动作。
- [x] `ota_service` 移除 OneNET JSON、`tid` 和 manifest 解析，只消费通用 plan；delta、重试、完整包回退和 activate 不分来源。
- [x] UI 只保留通用三步按钮：检查更新、下载更新、重启安装；UI 只读取通用快照。
- [x] 板测统一调用 `check -> download -> activate`，不再维护 provider 分支。
- [x] 保留 `fetch_headers()`、Range 续传、MD5/SHA-256、patch 头/基线校验、staged 和 rollback 相关既有行为。

## 验证

- `uv run python -m pytest tests -q`：472 passed，1 个既有 DeprecationWarning。
- `idf.py build`：通过；默认 OneNET 配置下 `111.bin` 为 0xAB8F20，最小 app 分区余量约 11%。
- OneNET provider 聚焦测试：14 passed。
- `git diff --check`：通过；仅有既有 CRLF 转换提示。

## 后续接入 OneNET Delta

只需在 OneNET provider 将差分任务字段填入 `has_delta`、`baseline_version`、`patch_url`、`patch_size`、`patch_sha256`、`target_sha256`。若 patch 格式兼容 `esp_delta_ota`，无需修改 service/UI；格式不兼容时仅新增 transport backend。
