---
id: official-chat-migration-prerequisites
tags: project, official-chat, sdkconfig, esp32s3, audio, speech
summary: 将 idf-xiaozhi 的 official_chat 迁移到当前仓库前，需要先补齐的关键 sdkconfig 开关、内存策略和组件依赖。
last_reviewed: 2026-03-31
---

# official_chat 迁移前置条件

- 当前仓库与 `idf-xiaozhi` 的 `sdkconfig` 差异里，和 `components/official_chat` 最直接相关的开关包括：
  - `CONFIG_OFFICIAL_CHAT_LANGUAGE_CODE`
  - `CONFIG_OFFICIAL_CHAT_OTA_URL`
  - `CONFIG_USE_AUDIO_PROCESSOR`
  - `CONFIG_USE_DEVICE_AEC`
  - `CONFIG_SEND_WAKE_WORD_DATA`
  - `CONFIG_WAKE_WORD_DETECTION_IN_LISTENING`
  - `CONFIG_LWIP_SO_RCVBUF`
  - `CONFIG_LWIP_UDP_RECVMBOX_SIZE`

- `idf-xiaozhi` 还额外打开了一整组语音与模型相关配置：
  - `CONFIG_MODEL_IN_FLASH`
  - 多个 `CONFIG_SR_*`
  - 多个 `CONFIG_AUDIO_DECODER_*`
  - 多个 `CONFIG_AUDIO_ENCODER_*`
  - 多个 `CONFIG_AUDIO_SIMPLE_DEC_*`

- 如果只复制 `official_chat` 源码而不补齐上面的配置，常见结果不是功能缺失，而是直接编译失败、Kconfig 选项缺失，或者运行时走到降级分支。

- `official_chat` 的 `CMakeLists.txt` 依赖当前仓库尚未具备的组件，至少包括：
  - `esp_audio_effects`
  - `esp-sr`
  - `espressif__esp_audio_codec`
  - `hal_wifi`

- 当前仓库虽然已有 `audio_codec`、`mqtt`、`json` 和 `esp_codec_dev`，但这并不能替代 `official_chat` 的全部依赖；迁移顺序应先补组件，再补 `sdkconfig`，最后再接入业务代码。

- 内存与网络配置也存在明显差异：
  - `idf-xiaozhi` 打开了 `CONFIG_PM_ENABLE`
  - `idf-xiaozhi` 更偏向 `SPIRAM_USE_MALLOC` / `SPIRAM_RODATA` / `SPIRAM_TRY_ALLOCATE_WIFI_LWIP`
  - `idf-xiaozhi` 打开了 `CONFIG_MBEDTLS_DYNAMIC_BUFFER` 与 `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`
  - `idf-xiaozhi` 打开了 `CONFIG_LWIP_SO_RCVBUF`，并把 `CONFIG_LWIP_UDP_RECVMBOX_SIZE` 调到 `12`

- 当前仓库保留了更多 IRAM-safe 中断与内部内存偏好配置，例如 `CONFIG_I2C_ISR_IRAM_SAFE`、`CONFIG_I2S_ISR_IRAM_SAFE`、`CONFIG_GDMA_ISR_IRAM_SAFE` 和 `CONFIG_SPIRAM_USE_CAPS_ALLOC`。如果后续照搬 `idf-xiaozhi` 的内存策略，需要重新评估显示、触摸和音频路径的实时性与内部 RAM 压力。

- 迁移 `official_chat` 时，建议分三步做最小可运行落地：
  1. 先补齐缺失组件并确认 `idf_component_register(... REQUIRES ...)` 可解析。
  2. 再最小化引入 `official_chat` 直接引用的 `CONFIG_*`，先以能编译通过为目标。
  3. 最后再补语音前端、AEC、唤醒词模型和 OTA 相关配置，逐步验证内存、网络和音频链路。
