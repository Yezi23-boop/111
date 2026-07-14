---
id: nonblocking-boot-network-service
tags: [project, boot, wifi, network-service, esp32s3, official-chat]
summary: 记录当前仓库把正式主流程从阻塞联网切换到后台联网后的当前启动底座，以及重整后的 main 目录位置。
last_reviewed: 2026-04-21
memory_type: semantic
scope: repo
owners: main/services/network/network_service.c, components/network_manager, components/wifi_control
triggers: nonblocking, boot, network, service
evidence_level: observed
---

# 非阻塞启动与后台联网服务

## 结论

- `D:\esp32S3\111\main\app\hardware_init.c` 已不再阻塞等待 Wi‑Fi 连接成功。
- `D:\esp32S3\111\main\app\app_main.c` 当前会在硬件初始化完成后立即启动：
  - `ui/lvgl_task.c` 中的 `lvgl_task`
  - `services/network_service.c`
  - `services/official_chat_service.c` 的初始化
- `D:\esp32S3\111\main\features\weather\time_weather.c` 任务实现仍在，但正式入口里的任务创建当前保持注释。
- `D:\esp32S3\111\main\services\network_service.c` 负责在后台继续执行：
  - `network_manager_start()`
  - 联网状态轮询
  - `api.tenclass.net` / `mqtt.xiaozhi.me` DNS 与服务就绪探测
- 当前 `network_service` 已不是旧 `wifi_provision` façade，而是：
  - `network_manager` 之上的兼容 shim
  - AI 服务就绪探测层

## 当前启动语义

### `hardware_init()`

现在只代表：

- 基础硬件初始化成功

不再代表：

- Wi‑Fi 已连接
- AI 服务已经可用

### `network_service`

当前仍是后台联网状态层，但正式联网 owner 已经切到 `network_manager`。当前至少区分：

- `OFFLINE`
- `BLE_PROVISIONING`
- `BLE_DISABLED`
- `CONNECTING`
- `WIFI_READY`
- `SERVICE_READY`
- `PORTAL_REQUIRED`
- `ERROR`

其中最关键的是把：

- “STA 已拿到 IP”
和
- “AI 服务可真正启动”

拆成两层状态，避免重蹈“刚拿到 IP 就做 OTA/MQTT，DNS 还没 ready”的问题。

## 当前边界

- 这一步只是在正式主流程里搭好后台联网底座。
- 当前正式 UI 已存在真实 Wi‑Fi / BLE 入口与 Wi‑Fi 管理页。
- `official_chat` 的正式入口生命周期已经并回 `main/app/app_main.c`，但是否前台激活仍由 AI 页面控制。
- 还没有处理 AI 与音乐播放器的音频 owner 协调。
- `network_service` 当前仍保留兼容 shim 语义，但新的网络策略和 UI 控制语义应以 `network_manager` 为准。

## 证据文件

- `D:\esp32S3\111\main\app\app_main.c`
- `D:\esp32S3\111\main\app\hardware_init.c`
- `D:\esp32S3\111\main\app\hardware_init.h`
- `D:\esp32S3\111\main\services\network_service.c`
- `D:\esp32S3\111\main\services\network_service.h`
- `D:\esp32S3\111\main\services\official_chat_service.c`
- `D:\esp32S3\111\components\network_manager`
- `D:\esp32S3\111\tests\test_nonblocking_boot_source.py`
