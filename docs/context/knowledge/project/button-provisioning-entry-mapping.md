---
id: button-provisioning-entry-mapping
tags: [project, ble, provisioning, ap, button, esp32-s3]
summary: 记录当前仓库 BOOT 按键已不再承担 BLE/AP 配网入口，默认配网入口改为主界面下拉菜单中的蓝牙按钮。
last_reviewed: 2026-04-17
---

# 配网按键入口映射

- 当前仓库中，`BOOT` 键不再承担任何 BLE/AP 配网入口语义。
- 当前默认配网入口已经收回到 UI：
  - 主界面下拉菜单中的 `screen_main_Bluetooth`
- 对应实现位于：
  - `main/app/hardware_init.c`
  - `main/services/network_service.c`
  - `main/ui/custom/main_dropdown_controller.c`
  - `main/ui/generated/events_init.c`
- 当前 BOOT 键仍然会初始化 `espressif__button` 驱动，但只保留：
  - `BUTTON_LONG_PRESS_START` 日志挂点
- 当前 BOOT 键实现继续保持：
  - `GPIO10`
  - `.active_level = 1`
- 这样调整的目的，是避免“物理按键”和“UI 蓝牙总开关”同时控制 BLE/AP 配网入口，导致状态机语义冲突。
- 当前 UI 蓝牙按钮的实际语义是：
  - 名义上是“蓝牙总开关”
  - 第一版实际只控制 BLE 配网广播/入口
- 当前 UI 蓝牙按钮的行为基线是：
  - BLE 偏好存入 `NVS`
  - 默认值为开启
  - 按钮视觉状态表达“实时 BLE 活动状态”，不是持久化偏好
  - 有 Wi-Fi 凭据时，UI 不允许主动开启 BLE
- 当前主流程保持为：
  - 无凭据开机且 BLE 偏好开启：`network_service` 自动进入 BLE 配网
  - 无凭据开机且 BLE 偏好关闭：进入 `BLE_DISABLED`，不自动拉 BLE，也不自动切 AP
  - 有凭据开机：优先自动联网
  - BLE 启动失败：仍可回退到 AP 网页兜底
- 已完成的验证包括：
  - 源码测试：覆盖 `network_service` BLE 开关控制面、UI 蓝牙按钮绑定、BOOT 配网入口移除
  - `idf.py build`
- 仍未完成的验证：
  - 真机点击 UI 蓝牙按钮时，广播出现/停止是否与按钮 checked 状态完全同步
  - 有凭据时点击蓝牙按钮，toast 提示是否满足实际交互预期
