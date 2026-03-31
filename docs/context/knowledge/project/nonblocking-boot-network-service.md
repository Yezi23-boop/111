---
id: nonblocking-boot-network-service
tags: [project, boot, wifi, network-service, esp32s3, official-chat]
summary: 记录当前仓库把正式主流程从阻塞联网切换到后台联网的第一步实现，作为 AI 融入 UI 的启动底座。
last_reviewed: 2026-03-31
---

# 非阻塞启动与后台联网服务

## 结论

- `D:\esp32S3\111\main\hardware_init.c` 已不再阻塞等待 Wi‑Fi 连接成功。
- `D:\esp32S3\111\main\111.c` 现在会在硬件初始化完成后立即启动：
  - `lvgl_task`
  - `time_and_weather`
  - `network_service`
- `D:\esp32S3\111\main\network_service.c` 负责在后台继续执行：
  - `wifi_provision_start_auto()`
  - 联网状态轮询
  - `api.tenclass.net` / `mqtt.xiaozhi.me` DNS 与服务就绪探测
- 在临时“无 UI 干扰”的验证模式下，可以继续使用 `D:\esp32S3\111\main\111.c` 作为入口，但注释掉：
  - `lvgl_task`
  - `time_and_weather`
  - 与之相关的启动延时
- 这时需要额外增加一个很轻的后台任务，在 `network_service_is_service_ready()` 后调用 `official_chat_service_enter_foreground()`，否则系统只会停在“网络已就绪但 AI 无触发源”的空闲状态。

## 为什么要这么改

- 用户要求“没网的时候也能用手表”。
- 后续 AI 页面如果要在未联网时显示引导配网，正式 UI 就不能继续被 `hardware_init()` 卡住。
- AI 实验入口 `main_ai_chat_experiment.c` 里已经有一套可用的服务探测逻辑，本轮把这部分能力沉到正式主流程可复用的后台联网层。

## 当前启动语义

### `hardware_init()`

现在只代表：

- 基础硬件和本地联网组件初始化成功

不再代表：

- Wi‑Fi 已连接
- AI 服务已经可用

### `network_service`

新增后台联网状态层，当前至少区分：

- `OFFLINE`
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
- 还没有正式 AI 页面。
- 还没有把 `official_chat` 并回 `main/111.c`。
- 还没有处理 AI 与音乐播放器的音频 owner 协调。
- 临时“无 UI”验证模式不适合长期保留，只用于隔离显示/UI 干扰，单独观察 `official_chat`、音频链和网络链。

## 证据文件

- `D:\esp32S3\111\main\111.c`
- `D:\esp32S3\111\main\hardware_init.c`
- `D:\esp32S3\111\main\hardware_init.h`
- `D:\esp32S3\111\main\network_service.c`
- `D:\esp32S3\111\main\network_service.h`
- `D:\esp32S3\111\tests\test_nonblocking_boot_source.py`
