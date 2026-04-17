---
id: wechat-miniapp-ble-provisioning-handoff-config
tags: [project, ble, provisioning, wechat, miniapp, handoff]
summary: 记录微信小程序 BLE 配网接手时必须确认的工程位置、协议基线、当前入口形态、真机限制与常见误判，避免把固件镜像不同步误当成小程序问题。
last_reviewed: 2026-04-17
---

# 微信小程序 BLE 配网接手配置

- 小程序工程位于 `C:\Users\ye\Desktop\eps32_ble`，主页面是 `miniprogram/pages/index/index.*`，BLE 协议封装在 `miniprogram/utils/ble-provision.js`。
- 当前固件默认的人机入口已经不是 BOOT 键，而是主界面下拉菜单中的 `screen_main_Bluetooth`：
  - 当前名义上是“蓝牙总开关”
  - 第一版实际只控制 BLE 配网广播/入口
  - 有 Wi-Fi 凭据时，UI 不允许主动开启 BLE
- 当前协议基线固定为自定义 128-bit GATT：
  - Service: `1C5ADFB4-6B3F-BFF4-EA4A-820304901A02`
  - RX: `1C5ADFB5-6B3F-BFF4-EA4A-820304901A02`
  - TX: `1C5ADFB6-6B3F-BFF4-EA4A-820304901A02`
- 微信小程序写 BLE 时不能把长 JSON 当单包发送；当前兼容策略是：
  - 每片 `20` 字节
  - 片间隔 `60ms`
  - 整帧以 `\n` 结尾
- 固件侧已增加换行分帧重组逻辑，因此小程序端不得去掉 `\n` 结尾，也不应并发写多个分片。
- 最近联调里出现过串口重新枚举失败、端口消失、未能再次稳定 flash 的情况，因此“小程序 BLE 配网异常”不能直接认定为小程序 bug，必须先确认板端实际刷入的是支持以下两项修复的新镜像：
  - 广播负载修复：设备名移到 scan response，避免 advertising data 超过 `31` 字节
  - 分片接收修复：支持多次 `Write` 重组为一条以 `\n` 结尾的 JSON 指令
- 接手时优先验证顺序应为：
  1. 板端日志出现 `BLE provisioning advertising`
  2. 真机微信扫描并连接
  3. 先验证 `hello`
  4. 再验证 `status`
  5. 最后验证 `set_wifi`
- 若 `hello/status` 正常但 `set_wifi` 失败，优先怀疑分片、换行、旧固件，而不是先重写小程序页面。
