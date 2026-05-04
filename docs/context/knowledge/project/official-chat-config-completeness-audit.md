---
id: official-chat-config-completeness-audit
tags: [project, official-chat, sdkconfig, partition, esp32s3, review]
summary: 审查当前 111 仓库的 official_chat 迁移完整性，结论是最小可编译配置、model/assets 分区、AEC 和基础唤醒词开关已补齐，但完整运行时策略仍未完全对齐源仓库。
last_reviewed: 2026-03-31
memory_type: semantic
scope: repo
owners: main/services/official_chat_service.c, components/official_chat
triggers: official, chat, config, completeness, audit
evidence_level: inferred
---

# official_chat 配置完整性审查

## 结论

- 当前仓库已经补齐了 `official_chat` 的最小可编译/可启动 `sdkconfig` 基线。
- 当前仓库也已经补齐了 `model` / `assets` 运行时分区。
- 当前仓库还没有达到“和源仓库运行时完全对齐”的状态。
- 当前剩余的关键缺口主要在内存/TLS/PSRAM 策略差异，以及是否继续补更多源仓库语音/唤醒词组合项。

## 已补齐的最小配置

- 当前 `sdkconfig` 已具备这些最小关键项：
  - `CONFIG_USE_AUDIO_PROCESSOR=y`
  - `CONFIG_SEND_WAKE_WORD_DATA=y`
  - `CONFIG_OFFICIAL_CHAT_OTA_URL`
  - `CONFIG_OFFICIAL_CHAT_LANGUAGE_CODE`
  - `CONFIG_MODEL_IN_FLASH=y`
  - `CONFIG_LWIP_SO_RCVBUF=y`
  - `CONFIG_LWIP_UDP_RECVMBOX_SIZE=12`
  - `CONFIG_UDP_RECVMBOX_SIZE=12`
- 这些也是 `idf-EDGE_lmpulse` 的 direct-main 知识卡明确提到的已同步最小 `sdkconfig` 基线。

## 已补齐的运行时分区

- 当前 `partitions.csv` 已追加：
  - `model`, `data`, `spiffs`, `4M`
  - `assets`, `data`, `spiffs`, `8M`
- 构建时已能自动生成并准备刷写 `srmodels.bin` 到 `model` 分区。
- 这说明 `official_chat` 当前代码里对 `esp_srmodel_init("model")` 的基本前提已经满足。

## 已补齐的板级/语音配置

### 1. `CONFIG_USE_DEVICE_AEC`

- `board_metadata/esp32-s3-touch-amoled-2.06.json` 的 `sdkconfig_append` 指定了 `CONFIG_USE_DEVICE_AEC=y`。
- 当前 `sdkconfig` 已同步为 `CONFIG_USE_DEVICE_AEC=y`。
- 当前构建已通过，说明板级 AEC 偏好和实验链路不再冲突。

### 2. 基础唤醒词模型配置

- `idf-xiaozhi` 与 `idf-EDGE_lmpulse` 的 `sdkconfig` 都包含 `CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS=y`。
- 当前 `sdkconfig` 已同步 `CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS=y`。
- 构建日志里 `Move and Pack models...` 已开始生成 `srmodels.bin`，推荐模型分区大小约 `285K`，说明唤醒词模型已进入打包链路。

## 未完成的关键项

### 1. 内存/网络/TLS 策略已做低风险同步，但仍未完全对齐源仓库

- 当前 `sdkconfig` 已同步这些低风险项：
  - `CONFIG_SPIRAM_USE_MALLOC=y`
  - `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=65536`
  - `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`
  - `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=131072`
  - `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`
  - `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y`
- 当前 `sdkconfig` 仍刻意保持：
  - `# CONFIG_PM_ENABLE is not set`
  - `# CONFIG_SPIRAM_RODATA is not set`
- `idf-EDGE_lmpulse` 源仓库则打开了：
  - `CONFIG_PM_ENABLE=y`
  - `CONFIG_SPIRAM_RODATA=y`
  - `CONFIG_SPIRAM_USE_MALLOC=y`
  - `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`
  - `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`
  - `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y`
- 结果：
  - 当前已经把 AI 对话最容易受益的 TLS/PSRAM 压力转移能力同步进来。
  - 但仍不能把它视作“完全复制源仓库 AI 对话运行时策略”。
  - 当前继续保留 `PM_ENABLE` 和 `SPIRAM_RODATA` 不变，优先避免影响既有 UI/触摸/音频时序与稳定性。

## 审查结论

- 如果目标是“最小可编译、可切实验入口、可复用本地配网”，当前配置已经够用。
- 如果目标是“完整移植源仓库 AI 对话运行时”，当前配置仍未完全对齐。
- 下一阶段至少要补：
  1. 是否继续接受源仓库剩余的 `PM_ENABLE / SPIRAM_RODATA` 运行时策略
  2. 是否继续同步更多 `CONFIG_SR_WN_*` / 语音模型组合项

## 证据文件

- `D:\esp32S3\111\sdkconfig`
- `D:\esp32S3\111\partitions.csv`
- `D:\esp32S3\111\components\official_chat\application.cc`
- `D:\esp32S3\111\components\official_chat\assets_runtime.cc`
- `D:\esp32S3\111\components\official_chat\board_metadata\esp32-s3-touch-amoled-2.06.json`
- `C:\Users\ye\Desktop\idf-xiaozhi\sdkconfig`
- `C:\Users\ye\Desktop\idf-EDGE_lmpulse\sdkconfig`
- `C:\Users\ye\Desktop\idf-EDGE_lmpulse\docs\context\knowledge\project\official-chat-direct-main-boot-on-111-zh-cn.md`
