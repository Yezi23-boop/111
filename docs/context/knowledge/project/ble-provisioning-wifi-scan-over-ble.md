---
id: ble-provisioning-wifi-scan-over-ble
tags: [project, ble, provisioning, wifi, scan, wechat, miniapp]
summary: 历史知识卡：记录仓库早期“自定义 BLE GATT + 微信小程序”方案里的 scan_wifi 协议；当前仓库正式实现已切到官方 network_provisioning 路线，本卡不再代表当前代码现状。
last_reviewed: 2026-04-22
---

# BLE 配网下发附近 Wi-Fi 列表（历史卡）

## 结论

- 本卡描述的是仓库早期“自定义 BLE GATT + 微信小程序”探索阶段的协议约定。
- 自 `2026-04-18` 起，当前仓库的正式网络底座已经切到：
  - 官方 `espressif/network_provisioning`
  - 上层 `network_manager + network_provisioning_adapter + ap_portal_adapter`
- 因此本卡中的 `scan_wifi / set_wifi / hello / status` JSON 协议，不应再作为当前代码排障或功能设计依据。
- 当前涉及 BLE / SoftAP 配网现状时，应优先阅读：
  - `docs/context/knowledge/project/network-provisioning-custom-upper-architecture.md`
  - `docs/context/knowledge/project/wifi-management-ui-behavior.md`
  - `docs/context/knowledge/project/storage-and-provisioning-paths.md`

## 仍然保留本卡的原因

- 它仍可用于解释：
  - 为什么仓库里还会出现“微信小程序 BLE 配网”的历史讨论
  - 为什么部分早期知识卡会提到 `scan_wifi`、JSON notify 分帧和自定义 UUID
  - 为什么早期 `CHANGELOG` 中存在“自定义 BLE 配网”相关记录

## 历史协议摘要

- 微信小程序在 BLE 会话建立后，会向设备发送：
  - `{"cmd":"scan_wifi"}`
- 设备扫描附近 `2.4G` Wi-Fi，并通过 BLE notify 分批返回列表。
- 用户在小程序中点选 SSID 后，再发送：
  - `{"cmd":"set_wifi","ssid":"...","password":"..."}`

## 历史归一化规则

- 过滤空 SSID
- 同 SSID 去重，保留 `RSSI` 更强的一条
- 若重复 SSID 中任一条为加密网络，则结果视为 `encrypted=true`
- 按 `RSSI` 从强到弱排序
- 最多保留前 `12` 条
- BLE notify 每批固定只返回 `1` 条

## 历史失败边界

- 同一时刻只允许一个在途 Wi-Fi 扫描。
- 如果 BLE 配网会话里重复请求 `scan_wifi`，而前一轮仍未结束，设备会返回：
  - `{"evt":"wifi_scan","state":"failed","reason":"scan_busy"}`
- 若底层扫描启动失败，则返回：
  - `{"evt":"wifi_scan","state":"failed","reason":"scan_start_failed"}`
- 若扫描任务已经开始，但中途遇到 `esp_wifi_scan_start()`、AP 记录分配或结果获取失败，则返回：
  - `{"evt":"wifi_scan","state":"failed","reason":"scan_failed"}`

## 历史客户端约定

- 当前小程序工程路径为：
  - `C:\Users\ye\Desktop\eps32_ble`
- BLE 完成 notify 订阅并跑完自动 `hello/status` 后，会自动发起一轮 `scan_wifi`
- 页面会保留：
  - “重新扫描”按钮
  - 手动输入 SSID 兜底
- 对开放网络：
  - 允许密码留空
  - 页面提示“开放网络可直接连接”
- 隐藏网络不进入扫描列表，继续依赖手动输入 SSID
- 对手动输入且不在扫描列表中的 SSID：
  - 默认按“需要密码”处理
  - 不再沿用上一次已选网络的 `encrypted` 状态

## 对后续 agent 的建议

- 若任务是当前仓库正式配网链路，请不要继续沿本卡实现新功能。
- 若任务是清理历史上下文或回溯旧实现来源，可把本卡当作协议考古资料使用。
- 若后续彻底移除微信小程序旧方案，可考虑把本卡迁到更明确的 `history/` 或在标题继续强化“历史卡”标记。
