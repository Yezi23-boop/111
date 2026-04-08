---
id: ble-stop-delay-stack-overflow
tags: [project, ble, provisioning, freertos, stack, overflow, esp32-s3]
summary: 记录 BLE 配网成功后延时关闭 BLE transport 的临时任务栈过小会触发 stack overflow，当前仓库已把 `ble_stop_delay` 栈从 2048 提升到 4096。
last_reviewed: 2026-04-08
---

# BLE 延时关闭任务栈溢出

## 结论

- 当前仓库在 BLE 配网成功后，会延迟约 `600ms` 再关闭 BLE transport，确保小程序能收到最后一条 `connected` 终态通知。
- 这条延迟关闭路径如果放在 `2048` 字节的小栈任务里，可能在调用：
  - `ble_provision_transport_stop()`
  - `ble_gap_terminate()`
  - controller / NimBLE 清理链
  时触发 `stack overflow`
- 当前修复是把 `ble_stop_delay` 任务栈固定提升到 `4096`

## 适用现象

- 串口先看到：
  - `BLE 终态通知缓冲完成，关闭 BLE 配网`
  - `GAP procedure initiated: terminate connection`
- 紧接着出现：
  - `***ERROR*** A stack overflow in task ble_stop_delay has been detected.`

## 原因

- 问题不在 Wi-Fi 扫描本身，也不在 notify 协议格式。
- 根因是延迟关闭 BLE 的独立任务栈太小，无法覆盖 BLE terminate 进入 controller 清理时的实际调用深度。
- 崩溃发生在 `ble_stop_delay` 任务上下文，而不是主配网任务或 NimBLE host task。

## 对后续 agent 的建议

- 若后续再次调整 BLE 终态关闭路径，优先保持：
  - 独立任务存在
  - 栈不低于 `4096`
- 若未来这条路径继续变重，例如增加更多日志、统计或清理动作，优先先审视栈预算，再判断是否需要继续上调。
