---
id: run-foreground-gate-and-ble-session-ownership-2026-07-29
tags: context, runs, attempt-log, foreground-session, runtime-gate, ble, provisioning, freertos
summary: 收敛 foreground gate 的 fail-fast 语义，并修复 BLE/SoftAP provisioning session 的 gate ownership、普通 BLE 让路与退出恢复路径。
last_reviewed: 2026-07-29
memory_type: run
scope: firmware
status: partial
owners: main/services/runtime_gate/foreground_runtime_gate.c, main/services/network/network_service.c, components/network_manager/src/network_manager.c, main/ui/custom/wifi_management_controller.c
evidence_level: source-and-build
---

# Attempt Log: Foreground Gate and BLE Session Ownership

## 背景

继续 `2026-07-14-watch-foreground-session-lifecycle-plan.md`：阶段 3 的异步 provisioning 已基本建立，但复查发现 SoftAP 没有持有强前台 gate，普通 BLE presence 转 provisioning 时 gate 来源没有转移，且退出后普通 BLE 偏好没有明确恢复。阶段 4 的 gate 仍保留无生产调用的 quiet window、无效 timeout 参数和泛化 `FUTURE_PAGE` owner。

## 本轮改动

- 将 `foreground_runtime_gate_acquire(owner, timeout_ms)` 收敛为 `foreground_runtime_gate_try_acquire(owner)`，明确立即失败、不等待的语义。
- 删除无生产调用者的 quiet-window API 和内部时间状态。
- 删除泛化 `FOREGROUND_RUNTIME_OWNER_FUTURE_PAGE`，未来页面必须增加具体 owner。
- BLE provisioning 启动时，无论 gate 是本 worker 新申请还是普通 BLE presence 已持有，都将 gate 责任转移给当前 provisioning session。
- SoftAP provisioning 启动前申请同一强前台 gate；失败时释放；network manager 先停止普通 BLE presence，再创建 SoftAP/provisioning。
- provisioning stop 成功后释放 gate；若用户 BLE 偏好仍为开启，则由 network owner 重新申请 gate 并异步恢复普通 BLE presence。
- Wi-Fi 管理页在 provisioning pending/active 期间锁定 BLE、SoftAP、断开和已保存网络重试，避免 UI 回调同步销毁或切换重资源。
- host UI mock 与 source tests 同步到新 gate API。

## 验证

- 聚焦 source tests：`65 passed`。
- `uv run python scripts/context/check_layering.py --verbose`：`warning_count=0`，既有 known exceptions `2`。
- `git diff --check`：无 whitespace error；仅已有 CRLF/LF 转换提示。
- `. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`：通过；`111.bin=0xabe1f0`；最小 app 分区剩余 `0x341e10`（23%）。
- 尚未执行真实 Wi-Fi 管理页 BLE/SoftAP provisioning 操作，因此不能据此宣称阶段 3 完成。

## 结论与下一步

阶段 4 的 API 语义已完成收敛；阶段 3 的代码缺口已修复，但仍需 COM7 真机覆盖：普通 BLE presence -> BLE provisioning -> 配网完成 -> gate 释放 -> presence 恢复，以及普通 BLE presence -> SoftAP provisioning、页面退出异步 stop 和 stop 超时 fail-closed。
