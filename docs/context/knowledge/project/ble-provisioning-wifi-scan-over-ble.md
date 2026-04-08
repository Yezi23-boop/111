---
id: ble-provisioning-wifi-scan-over-ble
tags: [project, ble, provisioning, wifi, scan, wechat, miniapp]
summary: 记录当前仓库 BLE 配网新增的 scan_wifi 能力，包括扫描结果归一化规则、BLE 回包格式，以及微信小程序侧的自动扫描与手动兜底行为。
last_reviewed: 2026-04-08
---

# BLE 配网下发附近 Wi-Fi 列表

## 结论

- 当前仓库继续沿用自定义 BLE GATT 配网协议，不切换到官方 `wifi_prov_mgr`。
- BLE 连接建立并完成 `hello/status` 后，微信小程序会自动发送：
  - `{"cmd":"scan_wifi"}`
- ESP32-S3 会扫描附近 `2.4G` Wi-Fi，并通过 BLE notify 分批返回列表。
- 用户在小程序中点选 SSID 后，仍沿用原有：
  - `{"cmd":"set_wifi","ssid":"...","password":"..."}`

## 当前协议新增内容

- 新增请求：
  - `{"cmd":"scan_wifi"}`
- 新增事件：
  - `{"evt":"wifi_scan","state":"started"}`
  - `{"evt":"wifi_scan","state":"batch","items":[{"ssid":"xxx","rssi":-48,"encrypted":true}],"more":true}`
  - `{"evt":"wifi_scan","state":"done","total":N}`
  - `{"evt":"wifi_scan","state":"failed","reason":"scan_busy|scan_start_failed|scan_failed"}`

现有 `hello`、`status`、`set_wifi`、`start_ap_fallback` 行为保持不变。

## 扫描结果归一化规则

当前仓库把 AP 网页和 BLE 扫描结果统一收敛到一套共享规则：

- 过滤空 SSID
- 同 SSID 去重，保留 `RSSI` 更强的一条
- 若重复 SSID 中任一条为加密网络，则结果视为 `encrypted=true`
- 按 `RSSI` 从强到弱排序
- 最多保留前 `12` 条
- BLE notify 每批固定只返回 `1` 条

这样做的目标是：

- 保持单条 BLE JSON 明显低于当前 notify 负载上限
- 页面默认展示“最可能是用户想连的网络”
- 避免 AP 页面和 BLE 页面各自维护不同的筛选逻辑

## 失败与并发边界

- 同一时刻只允许一个在途 Wi-Fi 扫描。
- 如果 BLE 配网会话里重复请求 `scan_wifi`，而前一轮仍未结束，设备会返回：
  - `{"evt":"wifi_scan","state":"failed","reason":"scan_busy"}`
- 若底层扫描启动失败，则返回：
  - `{"evt":"wifi_scan","state":"failed","reason":"scan_start_failed"}`
- 若扫描任务已经开始，但中途遇到 `esp_wifi_scan_start()`、AP 记录分配或结果获取失败，则返回：
  - `{"evt":"wifi_scan","state":"failed","reason":"scan_failed"}`

## 微信小程序侧约定

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

- 若只是在当前协议上继续扩展配网体验，优先保持 `scan_wifi + set_wifi` 这条简单链路，不要过早切到 `protocomm`
- 若后续要继续扩大扫描结果字段，优先评估 BLE 包体增长，避免直接把 `channel`、`authmode`、`bssid` 一次性全部塞回当前 JSON
- 若真机出现“已连 BLE 但列表一直不出”，优先检查：
  - 小程序是否真的发送了 `scan_wifi`
  - 设备是否返回了 `started`
  - 板端日志里是否有扫描开始、完成或失败原因
