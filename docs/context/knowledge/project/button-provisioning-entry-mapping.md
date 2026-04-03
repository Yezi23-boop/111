---
id: button-provisioning-entry-mapping
tags: [project, ble, provisioning, ap, button, esp32-s3]
summary: 记录当前仓库 BOOT 按键与配网入口的映射：单击强制进入 BLE 配网，三连击强制进入 AP 网页兜底，并补充从 AP 切回 BLE 时的资源收口要求。
last_reviewed: 2026-04-03
---

# 配网按键入口映射

- 当前仓库的按键入口已调整为：
  - 单击：强制进入 BLE 配网
  - 三连击：强制进入 AP 网页兜底
- 对应实现位于：
  - `main/hardware_init.c`
  - `components/wifi_provision/src/wifi_provision.c`
- 单击回调不再绑定 `BUTTON_PRESS_DOWN`，而是改为绑定 `espressif__button` 组件提供的 `BUTTON_SINGLE_CLICK`，避免三连击过程中的第一次按下提前触发 BLE。
- 单击回调不再直接调用 `wifi_provision_start_blecfg()`，而是调用 `network_service_request_ble()`。
- 三连击回调负责显式调用 `wifi_provision_start_apcfg()`，作为 AP 兜底入口。
- 为了让“强制 BLE 配网”真正具备切换语义，`wifi_provision_start_blecfg()` 现在会在当前 transport 为 AP 时先执行：
  - `ws_server_stop()`
  - `wifi_manager_stop_ap()`
- 这样做的目的是避免设备已经处于 AP 配网时，用户再次单击按键后出现 AP 与 BLE 两条路径同时挂着、状态机语义不清的问题。
- `network_service_request_ble()` 还会负责清掉 `network_service` 内部的 `s_portal_requested` 锁存，并把网络状态切回 `BLE_PROVISIONING`，避免“AP 已收口但状态仍卡在 `PORTAL_REQUIRED`”。
- 当前仍保持以下主流程不变：
  - 无凭据开机：`network_service` 自动进入 BLE 配网
  - 有凭据开机：优先自动联网
  - BLE 启动失败：回退到 AP 网页兜底
- 已完成的验证包括：
  - 源码测试：覆盖 `BUTTON_SINGLE_CLICK` 绑定、`network_service_request_ble()` 入口，以及 AP -> BLE 时 portal 锁存清理
  - `idf.py build`
- 尚未包含“单击切 BLE / 三连击切 AP”的新固件真机按键验证。
