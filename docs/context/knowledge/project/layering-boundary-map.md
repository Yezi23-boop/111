---
id: layering-boundary-map
tags: project, architecture, layering, boundary, owner, esp32-s3
summary: 记录当前仓库推荐采用的 App/UI、Service、Manager/Domain、Driver Adapter、Vendor/SDK 分层边界，以及 UI/Service/Manager/Adapter 的调用红线和越界检查重点。
last_reviewed: 2026-08-06
memory_type: semantic
scope: repo
owners: main/app, main/services, main/features, main/ui, components
triggers: layering, boundary, architecture, owner, app, service, manager, adapter, vendor, 分层, 架构, 越界
evidence_level: observed
---

# 当前项目分层边界地图

## 结论

当前仓库更适合采用“ESP-IDF component 化分层 + 有选择的平台抽象”，不建议为了套用课程模型而大规模改名为 `app/service/platform/implementation/vendor`。

推荐逻辑层：

```text
App/UI -> Service -> Manager/Domain -> Driver Adapter -> Vendor/SDK
```

分层的核心目的不是多建目录，而是隔离真实变化点：UI/业务不直接依赖芯片、协议、SDK 和外设细节；硬件或 SDK 变化时，优先收敛在 owner 模块内修改。

板级事实是单独的变化点：GPIO/中断线、I2C 地址、片选、传感器安装轴向、板级阈值和硬件变体配置默认收敛到 `main/app/board_*` 或现有 board owner。临时 diagnostic/probe 可以直接写已确认常量；一旦进入长期 service/session 或主启动链路，就必须先抽到 board 配置接口。

## 当前目录映射

- App/UI：`main/app`、`main/ui`、`main/features/*`，负责业务入口、页面交互和用户可见产品语义；host 预览工具位于 `tools/ui_preview`。
- Board Facts / Board Adapter：`main/app/board_*.c` 或现有 board owner，负责本板 GPIO/中断线、I2C 地址、片选、传感器安装轴向、板级阈值、硬件变体和上电基础设施事实。
- Service：`main/services/*`，按 `memory_watch/power/network/sensors/runtime_gate/startup/time/weather/safety/audio_diag` 等窄 owner 子目录组织，负责后台生命周期、状态推进、重试、超时、任务协作和跨模块编排。
- Manager/Domain：`components/network_manager`、`components/audio_codec`、`components/mp3_player`、`components/espdl_inference`、`main/app/board_power.c`，负责领域语义和资源 owner。
- Driver Adapter：`components/network_provisioning_adapter`、`components/ap_portal_adapter`、`components/wifi_control`、`components/co5300_panel`、`components/touch_ft5x06`、`components/axp2101`、`components/lvgl_port`，负责把项目语义适配到具体 SDK、协议或器件。
- Vendor/SDK：ESP-IDF、LVGL、ESP-DL、official_chat、芯片手册、I2C/SPI/I2S/Wi-Fi/provisioning/httpd 等原始 API。

## 红线

- `main/ui` 默认不直接 include 或调用 `esp_wifi_*`、`wifi_prov_mgr_*`、`httpd_*`、`esp_lcd_*`、`i2s_*`、`axp2101_*`、`co5300_panel_*`、`touch_ft5x06_*`。
- `main/ui/generated` 不直接 include `services/*`、`features/*` 或 raw driver；运行时数据刷新放入 `main/ui/custom`。
- UI 高频路径只读快照或调用 service/manager 的语义接口，不顺手推进联网、硬件、播放、低功耗状态机。
- Service 拥有后台状态推进和生命周期；UI 不抢 service 的重试、超时、stop/start owner。
- Service 不长期硬编码 GPIO/I2C 地址/片选/中断线/传感器安装轴向/板级阈值；需要这些事实时只读取 board 配置接口。
- Manager/Domain 输出语义能力，不新增“只转发一层”的空 wrapper。
- Driver Adapter 拥有 SDK、协议、寄存器、上电顺序、总线时序和错误码翻译。
- Driver Adapter 不拥有板上接线、安装方向或产品动作阈值；这些事实由 board 层或 service/domain 配置提供。
- 函数指针容器只在确实存在多实现替换、运行时注入或测试替身时使用；单实现路径优先用 C 头文件和 ESP-IDF component 边界。

## 典型链路

联网/配网链路：

```text
main/ui
  -> main/services/network/network_service.c
  -> components/network_manager
  -> components/network_provisioning_adapter / ap_portal_adapter / wifi_control
  -> ESP-IDF network_provisioning / esp_wifi / httpd
```

电源链路：

```text
main/ui/custom
  -> main/services/power/power_service.c
  -> main/app/board_power.c
  -> components/axp2101
  -> I2C / AXP2101 registers
```

音频链路：

```text
main/features/audio 或 main/services/official_chat_service.c
  -> audio owner/session
  -> components/audio_codec / components/mp3_player
  -> I2S / codec / storage
```

显示触摸链路：

```text
UI controller
  -> main/ui/lvgl_task.c / components/lvgl_port
  -> components/co5300_panel / components/touch_ft5x06
  -> esp_lcd / I2C / panel command
```

危险识别链路：

```text
UI/alerts
  -> main/features/danger_detection
  -> components/espdl_inference
  -> ESP-DL model/runtime
```

IMU / 抬腕链路：

```text
main/services/sensors/imu_service.c
  -> main/app/board_imu.c
  -> components/imu_sensor
  -> components/qmi8658c
  -> I2C / QMI8658C registers
```

天气链路：

```text
main/ui/custom
  -> main/services/weather/weather_service.c (snapshot)
  -> main/services/weather/weather_http_client.c
  -> ESP HTTP Client / cJSON
```

## 常见风险

- 过度抽象：只有一个实现却加 platform/implementation/function-pointer 多层跳转，增加 token 和调试成本。
- Manager 垃圾桶化：所有跨模块逻辑都塞进 manager，导致 owner 不清。
- Service 与 feature 混淆：页面功能直接启动后台任务，或后台 service 反向依赖具体页面对象。
- 原始 SDK 泄漏：UI/feature 直接依赖 ESP-IDF/LVGL/器件 API，后续替换 SDK 或器件时扩散修改。
- 生命周期不清：音频 session、LVGL 对象、Wi-Fi provisioning 会话、电源快照的 owner 不一致，容易产生悬空指针、重复 start/stop 或竞态。

## 落地规则

- 后续代码生成、评审和重构默认先按本卡判断层级与 owner，再决定文件落点和调用方向。
- 不为了分层而批量重命名目录；先尊重当前 `main/* + components/*` 结构。
- 新增抽象前先回答：是否有真实替换风险、是否有两个以上调用者、是否能减少上层变化。
- 新增跨层接口前先回答：owner 是谁、谁能调用、生命周期谁释放、错误如何返回、如何验证。
- Diagnostic/probe 升级为长期 service/session 前，必须把板级事实抽到 board 层，并补 source test 锁定 service/driver 不再各自硬编码同一事实。
- 简单 bugfix 不强制输出完整文件划分方案；新增模块、跨文件改动或明显重构时才需要先说明模块职责。
- 如果要临时越层调试，必须标注临时性，并在收尾时说明是否回退或固化到正确 owner。

## 检查方式

- 运行 `uv run python scripts/context/check_layering.py --verbose` 做只读提示。
- 普通检查只报告风险，不阻断；需要 CI 或强门禁时再加 `--strict`。
- 出现疑似越层时，先判断是否是 generated/debug/test/临时观测代码，再决定是否重构。
- 已知例外会单独列为 `known_exception_count`，用于保留历史债务可见性，但避免 agent 重复把已记录边界当成新问题。

## 接口/配置审查未闭环项（2026-07-07 审查蒸馏）

- 配置入口 owner 已明确：Wi-Fi 凭据 `network_credentials` NVS、BLE 偏好 `ble_control`、默认 transport `network_manager`、SoftAP 门户 `ap_portal_adapter`、Memory Watch endpoint NVS、weather/IMU 已收敛。未闭环风险：`network_manager_get_status()` 读路径会触发凭据保存/清理；BLE UI 点击路径执行 gate/quiet/retry；SoftAP 门户承接 Memory Watch 配置 schema（记录为过渡例外或拆 portal extension）。
- P0：tracked `sdkconfig` 不得保留真实 `CONFIG_MEMORY_WATCH_DEFAULT_DEVICE_TOKEN`（已进入的按泄露处理并轮换）；`server/watch_voice_endpoint/watch_contract.v1.json` 未覆盖 `/v1/watch/ws` 与 `/v1/watch/sync` 当前主路径，需扩展或新增 V2 contract 并用 source test 锁定 route/字段/枚举/超时。
- P1：`network_manager_get_status()` 非纯快照（会推进状态、保存/清理凭据）→ 新增纯 `network_manager_get_snapshot(out)`，状态推进只留 manager task、adapter event callback 和显式 command；主界面 BLE enable 的 gate/quiet/retry 应从 UI 路径移到 network owner task，UI 只提交 intent。
- P2：`network_service` 兼容面偏宽（冻结新增产品动作 API）；`memory_watch_service` public API 偏宽（冻结新增，内部拆文件，`copy_endpoint_config()` 视为敏感接口）；`ap_portal_adapter.h`/`co5300_panel.h`/`i2c_manager.h` 泄漏 SDK/raw handle（source test + allowlist）；`main` 大 component 编译期隔离不足（先补检查，中期 PRIV_REQUIRES/allowlist，不一次性大拆）；`lvgl_port -> ui_refresh_policy_notify_touch()` 反向通知（过渡例外，后续改 input activity callback 注册）。
- 检查工具补齐：`check_layering.py` 应新增 generated 禁 include service/feature/driver、UI 禁直调 driver adapter（allowlist）、service 禁直调设备层 raw API（diagnostic 例外登记）、components 禁反向引用 main、public header SDK 泄漏扫描、CMake REQUIRES 扫描、preview mock 单独分类。
- 整改顺序：先清 P0（token + contract）→ 收敛网络 getter 与 BLE UI 重路径 → 扩展检查工具 → 明确 SoftAP 配置写入边界 → 中期结构债（memory_watch 拆文件、public header 收窄）。
