---
id: layering-boundary-map
tags: project, architecture, layering, boundary, owner, esp32-s3
summary: 记录当前仓库推荐采用的 App/UI、Service、Manager/Domain、Driver Adapter、Vendor/SDK 分层边界，以及 UI/Service/Manager/Adapter 的调用红线和越界检查重点。
last_reviewed: 2026-05-04
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

## 当前目录映射

- App/UI：`main/app`、`main/ui`、`main/features/*`，负责业务入口、页面交互和用户可见功能。
- Service：`main/services/*`，负责后台生命周期、状态推进、重试、超时、任务协作和跨模块编排。
- Manager/Domain：`components/network_manager`、`components/audio_codec`、`components/mp3_player`、`components/espdl_inference`、`main/app/board_power.c`，负责领域语义和资源 owner。
- Driver Adapter：`components/network_provisioning_adapter`、`components/ap_portal_adapter`、`components/wifi_control`、`components/co5300_panel`、`components/touch_ft5x06`、`components/axp2101`、`components/lvgl_port`，负责把项目语义适配到具体 SDK、协议或器件。
- Vendor/SDK：ESP-IDF、LVGL、ESP-DL、official_chat、芯片手册、I2C/SPI/I2S/Wi-Fi/provisioning/httpd 等原始 API。

## 红线

- `main/ui` 默认不直接 include 或调用 `esp_wifi_*`、`wifi_prov_mgr_*`、`httpd_*`、`esp_lcd_*`、`i2s_*`、`axp2101_*`、`co5300_panel_*`、`touch_ft5x06_*`。
- UI 高频路径只读快照或调用 service/manager 的语义接口，不顺手推进联网、硬件、播放、低功耗状态机。
- Service 拥有后台状态推进和生命周期；UI 不抢 service 的重试、超时、stop/start owner。
- Manager/Domain 输出语义能力，不新增“只转发一层”的空 wrapper。
- Driver Adapter 拥有 SDK、协议、寄存器、上电顺序、总线时序和错误码翻译。
- 函数指针容器只在确实存在多实现替换、运行时注入或测试替身时使用；单实现路径优先用 C 头文件和 ESP-IDF component 边界。

## 典型链路

联网/配网链路：

```text
main/ui
  -> main/services/network_service.c
  -> components/network_manager
  -> components/network_provisioning_adapter / ap_portal_adapter / wifi_control
  -> ESP-IDF network_provisioning / esp_wifi / httpd
```

电源链路：

```text
main/ui
  -> main/services/power_service.c
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

## 常见风险

- 过度抽象：只有一个实现却加 platform/implementation/function-pointer 多层跳转，增加 token 和调试成本。
- Manager 垃圾桶化：所有跨模块逻辑都塞进 manager，导致 owner 不清。
- Service 与 feature 混淆：页面功能直接启动后台任务，或后台 service 反向依赖具体页面对象。
- 原始 SDK 泄漏：UI/feature 直接依赖 ESP-IDF/LVGL/器件 API，后续替换 SDK 或器件时扩散修改。
- 生命周期不清：音频 session、LVGL 对象、Wi-Fi provisioning 会话、电源快照的 owner 不一致，容易产生悬空指针、重复 start/stop 或竞态。

## 落地规则

- 不为了分层而批量重命名目录；先尊重当前 `main/* + components/*` 结构。
- 新增抽象前先回答：是否有真实替换风险、是否有两个以上调用者、是否能减少上层变化。
- 新增跨层接口前先回答：owner 是谁、谁能调用、生命周期谁释放、错误如何返回、如何验证。
- 简单 bugfix 不强制输出完整文件划分方案；新增模块、跨文件改动或明显重构时才需要先说明模块职责。
- 如果要临时越层调试，必须标注临时性，并在收尾时说明是否回退或固化到正确 owner。

## 检查方式

- 运行 `uv run python scripts/context/check_layering.py --verbose` 做只读提示。
- 普通检查只报告风险，不阻断；需要 CI 或强门禁时再加 `--strict`。
- 出现疑似越层时，先判断是否是 generated/debug/test/临时观测代码，再决定是否重构。
- 已知例外会单独列为 `known_exception_count`，用于保留历史债务可见性，但避免 agent 重复把已记录边界当成新问题。
