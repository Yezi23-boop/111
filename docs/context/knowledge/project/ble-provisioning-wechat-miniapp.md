---
id: ble-provisioning-wechat-miniapp
tags: knowledge, ble, provisioning, wechat-miniapp, nimble, gatt, notify, fragmentation
summary: 微信小程序 BLE 配网全链路事实合并卡：官方 protocomm 协议对齐、NimBLE UUID 字节序、31 字节广播 payload 上限、20 字节写分片重组、notify 换行与字节缓冲拆帧、UI 开关语义、配网成功后停止 BLE 的栈溢出修复。
status: active
last_reviewed: 2026-08-06
memory_type: semantic
scope: repo
owners: components/network_manager, components/network_provisioning_adapter, C:/Users/ye/Desktop/eps32_ble
triggers: ble, provisioning, wechat, miniapp, nimble, uuid, notify, fragmentation, stack overflow, 配网, 小程序, 蓝牙
evidence_level: observed
---

# BLE 配网与微信小程序（合并卡）

> 2026-08-06 由 9 张同域碎片合并而成，原文按小节完整保留，可按小节标题独立检索。

## ble-provisioning-advertising-payload-limit


## 结论

- 当前仓库的 BLE 配网在 `ESP32-S3 + NimBLE + 自定义 128-bit Service UUID` 组合下，若把 `flags + tx power + 完整设备名 + 128-bit UUID` 同时塞进 advertising data，会超过传统广播包 `31` 字节上限。
- 在本仓库里，这个问题的实机表现为：
  - `ble_prov: BLE host task started`
  - 紧接着 `ble_prov: set adv fields failed, rc=4`
- `rc=4` 在 NimBLE 中对应 `BLE_HS_EMSGSIZE`，即消息尺寸超限，不是控制器未启动，也不是 GATT 注册失败。

## 当前仓库中的证据

- 设备名默认会带 MAC 后缀，例如 `ESP32S3-723C`。
- `ble_provision_transport.c` 原先在 `ble_gap_adv_set_fields()` 中同时放入：
  - `BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP`
  - `tx power`
  - `complete name`
  - `128-bit service UUID`
- 对于带后缀的设备名，这组字段会超过 `31` 字节。

## 修复方式

- 主 advertising data 只保留：
  - `flags`
  - `128-bit service UUID`
- 将完整设备名挪到 scan response：
  - `ble_gap_adv_rsp_set_fields(&scan_rsp_fields)`

这样可以同时满足：

- 主广播可被正常发出。
- 手机侧仍可通过 scan response 看到完整设备名。
- 不需要改 GATT UUID，也不需要缩短对外展示名。

## 实机验证结果

- 擦除 `nvs` 分区后，设备以“无凭据”路径启动。
- 修复前日志为：
  - `ble_prov: BLE host task started`
  - `ble_prov: set adv fields failed, rc=4`
- 修复后日志为：
  - `ble_prov: BLE host task started`
  - `ble_prov: BLE provisioning advertising: ESP32S3-723C`

## 对后续 agent 的建议

- 如果后续继续往 advertising data 中追加字段，先重新估算 `31` 字节上限。
- 若需要继续扩展广播内容，优先把非必要展示字段放到 scan response，而不是继续堆进主 advertising data。
- 若再次出现 `set adv fields failed, rc=4`，优先排查 advertising payload 长度，而不是先怀疑 BLE 控制器初始化。



## ble-provisioning-miniapp-write-fragmentation


## 结论

- 微信小程序官方文档对 `wx.writeBLECharacteristicValue()` 明确给出兼容建议：单次写入尽量不要超过 `20` 字节。
- 当前仓库的 BLE 配网协议使用 UTF-8 JSON：
  - `hello`
  - `status`
  - `set_wifi`
  - `start_ap_fallback`
- 其中 `set_wifi` 和 `start_ap_fallback` 在真实场景下很容易超过 `20` 字节，因此不能把“小程序单次整包写入 JSON”当成稳定方案。

## 当前仓库中的处理方式

- 小程序侧：
  - 所有命令统一编码为 `JSON + '\\n'`
  - 按 `20` 字节分片顺序写入 RX characteristic
  - 相邻分片之间保留短暂间隔，避免并发写入
- 固件侧：
  - `ble_provision_transport.c` 新增 RX 分片缓存
  - 兼容两类输入：
    - 旧客户端：单次完整 JSON，且首字节是 `{`、末字节是 `}`
    - 新客户端：按 `\\n` 结尾的分片流，累计到换行后再一次性交给上层协议解析

## 为什么不用只依赖 MTU

- 微信小程序的 `wx.setBLEMTU()` 只在安卓 `5.1+` 有效。
- iOS 侧受系统限制，不适合作为当前最小闭环的唯一保障。
- 因此本仓库选择“应用层分片 + 固件端重组”，优先保证跨平台可用性。

## 官方 protocomm BLE 路线的差异

- 官方 `wifi_prov_mgr` / `protocomm_ble` 的 protobuf endpoint 不能沿用旧 JSON 的“20 字节普通写入分片”：
  - 普通 GATT write 每片会被固件当作一次独立 protobuf 请求处理。
  - 若需要超过单包 MTU 的请求，必须依赖 GATT prepare write/execute write；微信小程序通用 API 不直接暴露这层能力。
- 当前微信小程序官方配网客户端采用“先 `wx.setBLEMTU()`，再单包写入 protobuf”的策略：
  - Android 真机优先请求 `512` MTU。
  - 若 MTU 协商不可用或失败，保守按 `20` 字节 payload 上限校验。
  - 若 `prov-config` 请求超过当前 payload 上限，小程序应直接报错，避免把半包写入固件导致连接被关闭。
- 官方 `prov-scan` 的结果读取也需要控制单次响应大小；当前小程序按小批次拉取扫描结果，避免附近热点过多时一次 read 回包超过 MTU。
- 因此，旧 JSON 协议可应用层分片；官方 protocomm 协议默认不做应用层分片，优先通过 MTU 承接常见 SSID/密码长度。

## 对后续 agent 的建议

- 如果继续沿用当前 JSON 协议，默认继续保留 `JSON + '\\n' + 20 字节分片` 这套写法。
- 如果继续完善官方 BLE provisioning 小程序端，优先验证 `wx.setBLEMTU()` 真机日志和 `prov-config` payload 长度；不要把 protobuf 请求直接拆成多次普通 write。
- 如果后续协议字段继续变长，不需要优先改 MTU；当前方案已经能承接更长的请求。
- 若以后要切换到二进制帧协议，记得同时更新：
  - 小程序侧分片编码
  - 固件侧 RX 重组与帧边界判定



## ble-provisioning-nimble-uuid-byte-order


## 结论

- 当前仓库自定义 BLE GATT 使用 `BLE_UUID128_INIT(...)` 定义 128-bit UUID 时，必须按 `little-endian` 字节序填写。
- 若直接按人类阅读的 canonical UUID 文本顺序拆字节填入，微信小程序虽然仍可能通过设备名连接成功，但在 `getBLEDeviceServices` 后会报：
  - `未找到目标服务 1C5ADFB4-6B3F-BFF4-EA4A-820304901A02`
- 典型板端现象是：
  - 已开始广播
  - `BLE client connected`
  - 约 1-2 秒后立刻 `BLE client disconnected`
  - 没有后续 `BLE notify=1` 或配网命令日志

## 当前协议对应的正确写法

- 小程序约定的 canonical UUID：
  - Service: `1C5ADFB4-6B3F-BFF4-EA4A-820304901A02`
  - RX: `1C5ADFB5-6B3F-BFF4-EA4A-820304901A02`
  - TX: `1C5ADFB6-6B3F-BFF4-EA4A-820304901A02`
- NimBLE `BLE_UUID128_INIT(...)` 里应写为：
  - Service: `02 1A 90 04 03 82 4A EA F4 BF 3F 6B B4 DF 5A 1C`
  - RX: `02 1A 90 04 03 82 4A EA F4 BF 3F 6B B5 DF 5A 1C`
  - TX: `02 1A 90 04 03 82 4A EA F4 BF 3F 6B B6 DF 5A 1C`

## 证据

- 当前仓库实机联调中，板端日志表现为：
  - 广播正常
  - 手机/小程序可以连上
  - 小程序端提示“未找到目标服务”
  - 板端很快断开连接并重新广播
- ESP-IDF 自带 NimBLE GATT 示例也按 little-endian 顺序传入 `BLE_UUID128_INIT(...)`，例如文档中的：
  - canonical characteristic UUID `00001525-1212-EFDE-1523-785FEABCD123`
  - 宏实参写成 `0x23, 0xd1, ... , 0x00, 0x00`

## 对后续 agent 的建议

- 若再次出现“连接成功但找不到 service/characteristic”，先不要急着怀疑微信小程序 API。
- 第一检查项应是：
  - `ble_provision_transport.c` 中 `BLE_UUID128_INIT(...)` 的字节顺序
- 修改自定义 UUID 后，必须至少重新做：
  - 源码级 UUID 顺序检查
  - `idf.py build`
  - 重新 flash 板子



## ble-provisioning-notify-framing


## 结论

- 当前仓库 BLE 配网的上行回包不能只发送裸 JSON。
- 在微信小程序连续接收 `hello`、`status`、`wifi_scan started/batch/done` 多条 notify 时，若每条消息之间没有分隔符，小程序可能把多条 JSON 粘在一起，最终表现为：
  - 板端日志已经出现多次 `notify`
  - 小程序仍提示“设备暂未返回 wifi_scan 回包”
- 当前修复策略是：
  - 设备端 notify 时为每条 JSON 追加 `\n`
  - 使用 `ble_gatts_notify_custom()` 直接发送带分隔符的 mbuf
  - 再按 `20` 字节安全分片发送每条上行消息，避免微信侧未协商更大 MTU 时收不到完整 JSON

## 适用现象

- 串口能看到：
  - `BLE notify=1`
  - `收到 BLE Wi-Fi 扫描请求`
  - `BLE Wi-Fi 扫描完成`
  - 一串 `GATT procedure initiated: notify`
- 但小程序端没有进入：
  - `收到 Wi-Fi 列表 batch`
  - 或 `Wi-Fi 扫描完成`

## 原因

- 小程序侧 notify 解析器允许两种输入：
  - 单条完整 JSON
  - 多条以 `\n` 分隔的 JSON
- 设备端此前直接用 `ble_gatts_notify()` 从 characteristic 当前值发裸 JSON，没有显式分隔符。
- 当多个 notify 在短时间内送达时，小程序可能收到拼接后的文本流，导致 JSON 解析失败。
- 即使已经补了 `\n`，若单条 notify 长度仍超过微信侧当前可稳定接收的 payload，大概率仍只会得到残片，导致小程序一直等不到完整回包。

## 对后续 agent 的建议

- 若 BLE 上行继续扩展更多事件，默认保持：
  - 一条事件 = 一条以 `\n` 结尾的 JSON
  - 设备端按 `20` 字节安全分片发送
- 若后续再出现“串口明确 notify，但小程序提示未返回回包”，优先检查：
  - 设备端 notify 是否仍保留 `\n`
  - 设备端上行是否仍按 `20` 字节分片
  - 小程序是否仍在按换行拆帧



## ble-provisioning-ui-toggle-behavior


- 当前仓库把“BLE 总开关”和“BLE 配网会话”拆成两个动作。
- `screen_main_Bluetooth` 当前可见，语义类似手机蓝牙开关：表达 BLE enabled 偏好，并启动普通 BLE 可发现广播，不自动启动小程序配网。
- Wi-Fi 配网页面里的 `BLE Provision` 才是官方 BLE provisioning 广播入口。

## 控制分层

- UI 不直接操作底层 provisioning adapter。
- 主界面蓝牙开关统一通过 `network_manager_set_ble_enabled(bool enabled)` 改 BLE enabled 偏好并同步 `ble_presence` 普通广播。
- Wi-Fi 配网页面通过 `network_manager_start_ble_provisioning()` 显式启动小程序配网。
- AP 网页兜底通过 `network_manager_start_softap_provisioning()` 显式启动。
- `components/ble_presence` 是普通 BLE 可发现广播 owner；`components/network_provisioning_adapter` 是官方 BLE/SoftAP provisioning owner。

## NVS 偏好

- BLE 开关偏好存放在：
  - namespace: `network_svc`
  - key: `ble_enabled`
- 默认值为开启，目的是兼容当前仓库“无凭据默认进入 BLE 配网”的既有行为。

## 当前 UI 位置

- 主界面默认入口：
  - `screen_main_Wifi`
  - 展示真实 Wi-Fi 连接状态，并进入 Wi-Fi 管理页
- 主界面蓝牙入口：
  - `screen_main_Bluetooth`
  - 控制 BLE enabled 偏好和普通 BLE 可发现广播
  - 关闭时如果 BLE provisioning 正在运行，会停止该会话
  - 开启时会广播普通 BLE presence 名称 `ESP32S3-723C`，但不会自动广播 provisioning 服务
- BLE 配网入口：
  - `main/ui/custom/wifi_management_controller.c`
  - 用户进入 Wi-Fi 管理页后点击 `BLE Provision`
- 自动路径：
  - 开机无 recent Wi-Fi 或 latest 连接失败时，不再自动启动 BLE provisioning
  - 设备停在空闲态，等待用户明确选择 `BLE Provision` 或 `AP Web Fallback`

## 状态机语义

- `network_service_state_t` 已新增：
  - `NETWORK_SERVICE_STATE_BLE_DISABLED`
- 其含义是：
  - 当前没有 Wi-Fi 凭据
  - 用户主动关闭了 BLE enabled 偏好
  - 服务任务因此不再自动重拉 BLE
- 这用于区分：
  - “尚未启动/离线”
  - “BLE 正在配网”
  - “用户主动关闭 BLE”

## 当前残余风险

- 普通 BLE presence 当前采用 non-connectable advertising，目的是让小程序/扫描工具能发现设备，同时避免手机系统误连占用唯一 BLE connection；部分手机系统蓝牙设置页可能仍不会像经典蓝牙设备一样展示它。
- 若 BLE 配网启动失败，Wi-Fi 管理页只显示简短错误，详细原因仍主要依赖串口日志定位。
- 该文档只覆盖 BLE enabled 与 provisioning 入口的分离边界，不代表项目已经具备完整通用蓝牙业务协议。



## wechat-miniapp-ble-notify-byte-buffer


## 结论

- 当前小程序侧不能再把 BLE notify 当作“每次一定是一条完整字符串”来处理。
- 当 ESP32 端把 `hello`、`status`、`wifi_scan started/batch/done` 按 `20` 字节安全分片上行时，小程序需要：
  - 先按 `Uint8Array` 追加到字节缓冲
  - 以 `\n` 作为消息边界拆帧
  - 只在拿到完整帧后再做 `UTF-8` 解码和 `JSON.parse`

## 适用现象

- 板端串口已经能看到：
  - `BLE notify=1`
  - `收到 BLE Wi-Fi 扫描请求`
  - `BLE Wi-Fi 扫描完成`
  - 多次 `GATT procedure initiated: notify`
- 小程序却仍提示：
  - `设备暂未返回 hello/status/wifi_scan 回包`
  - 或日志里没有任何 `收到设备消息: ...`

## 原因

- 旧实现先把每次 notify 直接解码成字符串，再做文本拼接。
- 当上行消息在 BLE 层被拆成多个片段时，旧实现可能把半条 UTF-8 或半条 JSON 当作完整文本处理，导致：
  - 一直拼不出完整 JSON
  - 旧会话残片污染下一次连接

## 当前仓库的修复要点

- 小程序页 `index.js` 中引入 `notifyByteBuffer: Uint8Array`
- `characteristicValueHandler` 改为把原始 `ArrayBuffer` 交给 `consumeNotifyValue()`
- `consumeNotifyValue()` 逻辑固定为：
  - 字节缓冲追加
  - 找 `0x0A` 换行
  - 提取完整帧
  - 完整帧再 `decodeUtf8()` 和 `applyIncomingMessage()`
- 连接开始前、连接重置时、页面卸载时都必须同时清空：
  - `notifyByteBuffer`
  - `notifyTextBuffer`

## 对后续 agent 的建议

- 若后续仍扩展 BLE 上行事件，优先保持：
  - 设备端一条 JSON + `\n`
  - 小程序端字节缓冲拆帧
- 若再次遇到“板端 notify 明明已发，小程序却仍超时”，先查：
  - `notifyByteBuffer` 是否还存在
  - 连接/断开时是否清空了旧缓冲
  - 小程序日志里是否已经出现 `收到设备消息: ...`



## wechat-miniapp-ble-provisioning-handoff-config


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



## wechat-miniapp-official-ble-provisioning


## 结论

- 当前小程序端应优先实现“官方 protocomm BLE client”。
- 小程序协议层参考 SoftAP 配网页的官方 `proto-ver / prov-session / prov-scan / prov-config` 流程。
- 历史 `hello / status / scan_wifi / set_wifi` JSON GATT 协议只能作为旧镜像兜底，不再作为新架构主线。

## 端点与 UUID

- 官方 BLE provisioning 默认服务 UUID：
  - `2D9BED07-060F-877C-9B43-436B4D247517`
- ESP-IDF 5.5.3 当前端点 UUID 映射：
  - `prov-scan`：`0xFF50`
  - `prov-session`：`0xFF51`
  - `prov-config`：`0xFF52`
  - `proto-ver`：`0xFF53`
- GATT characteristic UUID 由 service UUID 作为 base，并替换底层 UUID byte `12/13` 为端点 16-bit UUID。
- 官方文档也允许通过 characteristic user description descriptor `0x2901` 读取端点名；但微信小程序端为了降低 descriptor API 兼容风险，可以优先使用固定端点 UUID 推导。

## 小程序实现边界

- 小程序先扫描官方设备名 `NET_PROV*` 或官方服务 UUID。
- 连接后发现服务和特征，建立端点名到 characteristic 的映射。
- BLE request 模式是：
  - 向对应 endpoint characteristic 写入一个完整 protobuf 请求
  - 再读取同一个 characteristic 等待响应
- 不再使用历史自定义 RX/TX notify 串口模型来承载官方协议。

## 风险

- 微信小程序 BLE 长包能力仍需真机验证。
- 官方 protocomm BLE 通常把一次 GATT write 视作一个完整请求，不等价于历史 JSON 协议里的 20 字节分片重组。
- `proto-ver / prov-session / prov-scan` 包体较小，优先用于最小闭环。
- `prov-config` 携带 SSID/密码，可能超过 20 字节；若真机失败，需要评估 `wx.setBLEMTU()` 或固件侧兼容层。



## ble-stop-delay-stack-overflow


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


