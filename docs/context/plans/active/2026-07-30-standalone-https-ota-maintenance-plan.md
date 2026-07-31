---
id: standalone-https-ota-maintenance-plan
tags: context, plans, ota, https, rollback, freertos, maintenance-session
summary: ESP32-S3 独立 HTTPS 双槽 OTA 维护会话执行计划，覆盖全屏进度、owner quiesce、manifest、A/B 切换、rollback 与真机故障注入。
last_reviewed: 2026-07-30
memory_type: task
scope: task
owners: partitions.csv, main/services/ota, main/services/runtime_gate, main/services/safety/background_service_manager.c, main/services/network/network_service.c, main/ui
triggers: OTA, HTTPS OTA, dual slot, rollback, otadata, maintenance session
evidence_level: design
status: active
---

# 独立 HTTPS 双槽 OTA Maintenance Session 计划

## 目标

为当前 ESP32-S3 固件建立独立 `ota_service`，不依赖 `official_chat` OTA。用户确认升级后进入全屏维护页面，非必要后台 owner 有序 quiesce，下载新镜像到备用 OTA 槽，首次启动确认后才将新镜像标为有效。

## 已确认约束

- Flash 为 32 MiB；无 `factory`，`ota_0` / `ota_1` 各 12 MiB，`otadata` 为 0x2000。
- 当前应用镜像为 0xAAF1B0，小于单槽 0xC00000；发布链路必须拒绝超过单槽的镜像。
- `assets` 为 2 MiB，`resources` 为 4 MiB，`model` 为 0x1E0000；OTA 只更新 app 槽，不更新这些数据分区、bootloader 或 partition table。
- 升级期间保留 LVGL、Wi-Fi STA/DNS/TLS 和电源基础服务；不强杀 task，不直接 suspend 其他 owner。
- Safety Monitor / ESP-DL、前台 AI 会话、普通音频、BLE provisioning/SoftAP 与周期网络任务只能由各自 owner 停止并发布 released ACK。
- UI 只提交 intent 和读取 snapshot；OTA task 不持有 LVGL 对象。

## 审查修正合同

- OTA owner 的固定顺序是：`try_acquire(OTA) -> request_quiesce(generation) -> wait current-generation ACK -> 下载 -> restart`。任何不重启出口必须按 `abort（如已开始）-> finish_quiesce(generation) -> release(OTA)` 清理，不能留下 maintenance gate 或 quiesce request。
- Gate 持有覆盖 `PREPARING` 至 `RESTARTING` 全程；maintenance active 时各 owner 必须拒绝或延后新的前台、音频、BLE/SoftAP、AI 会话和周期网络工作，不得在 ACK 后自行重新启动。
- OTA service 只编排 command、snapshot、gate、ACK 和重启；HTTPS client、`esp_https_ota_*`、镜像长度与错误码翻译位于窄 OTA transport adapter，避免 service 直接承载 SDK 生命周期。
- `PENDING_VERIFY` 由早期启动的本地 boot-check owner 处理，不等待 UI 首帧、OTA task、网络或云端。它在基础资源初始化完成后、后台服务启动前执行，并在有界时间内标记 valid 或 invalid/reboot。
- manifest 的 SHA-256 是镜像准入条件而非格式字段：OTA transport 必须确认 HTTP 长度、实际接收长度与 manifest `size` 一致，并在 `esp_https_ota_finish()` 前比较备用槽的计算 SHA-256 与 manifest；任何不一致均 abort，禁止切换 boot 槽。
- 恢复行为遵从每个 owner 的最新用户意图，不恢复过期页面 session；每个 owner 必须有明确 request、released snapshot/ACK、maintenance active 行为和恢复触发条件。

## 状态机

```text
IDLE
-> PREPARING
-> QUIESCING
-> READY
-> DOWNLOADING
-> VERIFYING
-> RESTARTING
-> (新镜像 PENDING_VERIFY)
-> VALID 或 ROLLED_BACK

任一步失败 -> FAILED -> 恢复已 quiesce owner -> IDLE
```

## 阶段与验收

### 阶段 0：Python 命令行本地 HTTPS 上位机

- [x] 在 `tools/ota_host/` 提供标准库 Python CLI：从 `.bin` 计算 SHA-256、生成 manifest，并以本地 HTTPS 静态服务发布 manifest 和镜像。
- [x] CLI 显式要求 TLS certificate/key；不生成或静默信任临时证书，设备端仅接受已配置的 CA。
- [x] 支持开发期可复现故障：错误 SHA-256、截断镜像和下载中断；每次请求输出方法、路径、状态码和发送字节数。
- [x] 为 manifest 生成与故障响应添加 host 侧单元测试，不需要连接设备。

验收：工具生成的 manifest 包含 version、HTTPS URL、size 与 SHA-256；用本机 HTTPS client 可下载完整镜像，三种故障模式可稳定复现。

### 阶段 1：无联网 OTA Service 骨架

- [ ] 新增 `main/services/ota/ota_service.c/.h`，创建 owner task、长度受限的 command queue 与只读 snapshot。
- [ ] UI 新增全屏 OTA 页面；只展示 `IDLE -> PREPARING -> QUIESCED -> READY`，不创建 HTTP client、不写 Flash、不改 `otadata`。
- [ ] OTA task 等 UI 首帧 ready 后运行；UI timer 只复制 snapshot。

验收：进入/退出 OTA 页面不阻塞 LVGL；退出后后台目标态恢复；COM7 `read_otadata` 前后完全一致。

### 阶段 2：Maintenance Quiesce 与 ACK

- [ ] OTA service 请求 maintenance 前台权，调用 `background_service_manager_request_foreground_quiesce()` 并等待对应 generation ACK。
- [ ] 固定 gate 生命周期：先 `try_acquire(OTA)`，所有失败/取消出口按 `finish_foreground_quiesce(generation)` 和 `release(OTA)` 反向清理。
- [ ] Safety Monitor / ESP-DL 确认 runtime、模型和 input session 已释放。
- [ ] 逐个补齐普通音频、Hermes、official_chat、BLE provisioning、SoftAP 与周期网络 owner 的 maintenance stop/released snapshot；OTA task 不直接操作这些资源。
- [ ] 每个 owner 在 maintenance active 期间拒绝或延后新的启动请求；退出维护时按最新用户意图重新 reconcile。
- [ ] ACK 超时或 cleanup 失败 fail closed：不调用 OTA 下载 API，恢复已经 quiesce 的 owner。

验收：日志证明所有必需 owner released 在 `READY` 之前；失败路径无强杀 task、audio/gate 泄漏或 Flash 写入。

### 阶段 3：自定义 Manifest 检查

- [ ] 独立 HTTPS endpoint 返回 manifest：version、URL、size、SHA-256。
- [ ] 校验版本递增、HTTPS/允许主机、有效系统时间、SHA-256 格式和 `size <= 0xC00000`。
- [ ] 检查阶段仅更新 snapshot；用户确认 `START` 后才允许下载。

验收：网络、TLS、JSON、版本或大小错误均不写 Flash，UI 显示可解释错误，owner 完整恢复。

### 阶段 4：HTTPS 下载与 A/B 切换

- [ ] 只在 OTA task 调用 `esp_https_ota_begin()`、循环 `esp_https_ota_perform()`、`esp_https_ota_is_complete_data_received()` 和 `esp_https_ota_finish()`。
- [ ] OTA transport adapter 持有 `esp_https_ota_*`；下载进度写入 snapshot，`perform()` 每次返回后检查取消命令并在 deadline 内进入 cleanup。
- [ ] `finish()` 前验证 HTTP Content-Length、实际接收长度、manifest size 和备用槽 SHA-256 全部一致；失败/取消调用 `esp_https_ota_abort()`，旧槽继续可启动。
- [ ] `finish()` 成功后进入 `RESTARTING` 并调用 `esp_restart()`。

验收：从 `ota_0` 升级时仅写 `ota_1`；下载失败后仍从旧槽启动；成功后 `otatool.py read_otadata` 与启动日志均指向新槽。

### 阶段 5：首次启动确认与 Rollback

- [ ] 在早期本地 boot-check 查询 `ESP_OTA_IMG_PENDING_VERIFY`，不依赖 UI 或 OTA task。
- [ ] 最小自检只覆盖基础资源文件系统和关键启动链路；不等待云端、天气、UI 或用户交互。
- [ ] 成功调用 `esp_ota_mark_app_valid_cancel_rollback()`；失败调用 `esp_ota_mark_app_invalid_rollback_and_reboot()`。

验收：确认后可连续重启；专用测试镜像不确认或显式失败时自动回到先前有效槽。

### 阶段 6：真机故障注入

- [ ] 下载 20%/50%/90% 主动 abort。
- [ ] 错误镜像、错误 manifest、TLS 失败。
- [ ] 首次启动未确认重启与下载中断电。
- [ ] `esp_https_ota_finish()` 更新启动选择后、`esp_restart()` 前复位/掉电。
- [ ] 新镜像 `PENDING_VERIFY` 到 valid/invalid 标记之间复位/掉电。
- [ ] 每次记录 running/boot slot、`otadata`、snapshot、ACK、panic/Guru/WDT 和资源恢复。

验收：所有失败均保留可启动旧镜像；无 Flash 越界、gate 泄漏或后台资源悬挂。

## 非目标

- 不 OTA 更新 bootloader、partition table、NVS、`assets`、`resources` 或 `model`。
- 不复用或修改 `official_chat` OTA 业务协议。
- 不新增全局 ResourceManager、后台 HTTPS gate 或通用 session router。
- 不让 UI 直接执行下载、Flash 写入、owner stop 或等待 ACK。

## 验证

- 每个代码阶段：聚焦 source test、`idf.py build`、`git diff --check`。
- 分区/OTA 状态：`otatool.py read_otadata` 与启动日志。
- 真机：`scripts/board/agent_serial_monitor.ps1` 限时采集；故障注入只使用专用测试镜像。

## 进度

- [x] 分区基础：32 MiB 双 12 MiB OTA 槽、`otadata` 0x2000 已在 COM7 烧录并启动验证。
- [x] 设计教学：A/B、`otadata`、manifest、HTTPS OTA、rollback、owner quiesce 与故障注入路径已确认。
- [x] 审查修正：已补齐 gate/quiesce generation cleanup、维护窗口阻断、OTA transport adapter、早期 boot-check、镜像 SHA-256 准入与 commit/首启故障窗口。
- [x] 阶段 0：Python 命令行本地 HTTPS 上位机。host 单测通过；本机 HTTPS client 已完整下载 `build/111.bin`，长度与 SHA-256 均匹配 manifest。
- [ ] 阶段 1：无联网 OTA Service 骨架。
- [ ] 阶段 2：Maintenance quiesce 与 ACK。
- [ ] 阶段 3：自定义 manifest。
- [ ] 阶段 4：HTTPS 下载与 A/B 切换。
- [ ] 阶段 5：首次启动确认与 rollback。
- [ ] 阶段 6：真机故障注入。

## 下一步

从阶段 1 开始：先建立不联网、不写 Flash 的 `ota_service`、全屏页面和 snapshot，再以 COM7 的 `otadata` 只读前后对比作为首个板端验收。
