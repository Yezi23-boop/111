---
id: official-chat-ota-tls-time-bootstrap
tags: [project, official-chat, ota, tls, sntp, time]
summary: official_chat 主卡（2026-08-06 合并）：OTA/激活 HTTPS 先于时间同步的 TLS 首次授时约束 + 音频队列官方对齐 + 迁移前置条件 + 配置完整性审计 + RAM 对齐基线。首次 OTA HTTPS 前必须先确认系统时间有效并输出 TLS 诊断日志。
last_reviewed: 2026-08-06
memory_type: semantic
scope: repo
owners: main/services/official_chat_service.c, main/services/network/network_service.c, components/official_chat
triggers: official, chat, ota, tls, time, bootstrap, 时间, 授时, 迁移
evidence_level: observed
route_area: "Official Chat"
---

# official_chat OTA TLS 首次授时约束

## 结论

- 当前仓库的正式启动入口还没有把首轮时间同步重新并回主链路。
- `official_chat` 的 OTA 版本检查却会在激活阶段第一时间发起 HTTPS 请求。
- 如果此时系统时间仍停留在冷启动默认值，`mbedtls` 证书有效期校验会失败，表面症状通常是：
  - `mbedtls_ssl_handshake returned -0x2700`
  - `official_ota: version check request failed: ESP_ERR_HTTP_CONNECT`
- 因此 OTA 首次 HTTPS 请求前，必须先确认系统时间进入 TLS 可用区间；否则即使 Wi-Fi、DHCP、DNS 都正常，也会在证书校验阶段失败。

## 当前仓库中的真实触发条件

- `main/app/app_main.c`
  - 正式入口当前只启动：
    - `lvgl_task`
    - `network_service`
    - `official_chat_service`
  - `time_and_weather` 任务创建保持注释。
- `docs/context/knowledge/project/startup-init-and-blocking-chain.md`
  - 已明确记录“时间同步链路尚未重新并回正式入口”。
- `components/official_chat/ota.cc`
  - `CheckVersion()` 会先发 OTA 版本查询 HTTPS 请求。
  - 旧实现只有在请求成功拿到响应后，才通过 `server_time` 调 `settimeofday()`。

这意味着：

- 首次 HTTPS 如果依赖有效系统时间才能成功；
- 而有效系统时间又依赖这次 HTTPS 成功后的 `server_time`；
- 二者形成了冷启动死锁。

## 已落地修复

- 在 `components/official_chat/ota.cc` 中，为所有 HTTPS JSON 请求增加了“先校验系统时间”的前置步骤。
- 若时间无效：
  - 复用仓库既有 NTP 服务器集合启动或重启 `SNTP`
  - 在有限时间窗口内等待首次授时
  - 成功后再继续 HTTPS 请求
  - 超时则直接返回错误，并打印当前时间、SNTP 状态和 reachability
- 同时为 HTTP/TLS 失败补充了额外诊断：
  - `errno`
  - `esp_http_client_get_and_clear_last_tls_error()` 返回值
  - `mbedtls` 原始错误码
  - `TLS flags`
  - 当前 UTC 时间快照
- `server_time.timestamp` 是 Unix epoch 毫秒，属于 UTC 绝对时间：
  - `official_chat` 只负责解析并转交给 `system_time` owner。
  - `timezone_offset` 只能用于显示本地时间，不能加到 epoch 后再写回系统时间或 RTC。
  - 若东八区把 `timezone_offset` 再加进 epoch，会导致系统时间和 RTC 被写快 8 小时。

## 日志判读

- 若看到：
  - `system time invalid before HTTPS request`
  - `SNTP started for OTA TLS bootstrap`
  - `time snapshot stage=after_sntp_sync`
  - 随后 OTA 请求成功
  - 说明根因就是冷启动时间无效。
- 若看到：
  - `http tls diagnostics stage=open`
  - `tls_code=-9984`（即 `-0x2700`）
  - 说明 TLS 失败发生在握手阶段，优先检查系统时间与 CA 信任链。
- 若看到：
  - `system time still invalid after SNTP wait`
  - 说明当前板端即使联网成功，NTP 仍未在超时时间内完成首次授时，需要继续排查 SNTP 服务器可达性或系统网络策略。

## 验证方法

1. 冷启动设备，确保之前未保留可信 RTC 时间。
2. 完成配网并进入 `official_chat` 激活阶段。
3. 观察是否先出现时间快照与 SNTP bootstrap 日志，再继续 OTA HTTPS。
4. 若激活成功，应不再出现首轮 `mbedtls_ssl_handshake returned -0x2700`。

## 证据文件

- `D:\esp32S3\111\components\official_chat\ota.cc`
- `D:\esp32S3\111\main\app\app_main.c`
- `D:\esp32S3\111\main\features\weather\time_weather.c`
- `D:\esp32S3\111\components\system_time\system_time.c`
- `D:\esp32S3\111\main\services\system_time_service.c`
- `D:\esp32S3\111\docs\context\knowledge\project\startup-init-and-blocking-chain.md`

## official-chat-audio-queue-official-alignment


## 结论

当前仓库 `components/official_chat/audio/audio_service.*` 的主音频队列应按 `D:\xiaozhi-esp32\main\audio\audio_service.*` 官方实现保持：

- FreeRTOS task 承载输入、输出和 Opus 编解码任务；
- FreeRTOS event group 控制 wake word、audio processor 和 audio testing 运行状态；
- `std::mutex + std::condition_variable` 负责多队列条件等待；
- `std::deque<std::unique_ptr<AudioTask>> audio_encode_queue_` 存编码任务；
- `std::deque<std::unique_ptr<AudioStreamPacket>>` 存 decode/send/testing 队列；
- PCM 数据继续由 `std::vector<int16_t>` 持有。

## 依据

- 官方 `D:\xiaozhi-esp32\main\audio\audio_service.h` 明确写有两条数据流：
  - MIC -> Processors -> Encode Queue -> Opus Encoder -> Send Queue -> Server
  - Server -> Decode Queue -> Opus Decoder -> Playback Queue -> Speaker
- 官方 `OpusCodecTask()` 使用同一个 `audio_queue_cv_.wait()` 同时等待：
  - encode queue 非空且 send queue 有空间；
  - decode queue 非空且 playback queue 有空间；
  - service stopped。
- 若只把其中一个队列换成 FreeRTOS queue，会变成两套等待机制并存，反而增加停机唤醒和多条件等待复杂度。

## 当前边界

- 不采用 `FixedAudioTaskQueue` 固定环形队列作为默认实现。
- 不把 audio queue 拆成 FreeRTOS queue，除非后续明确要做一版完整音频队列架构重构。
- 可以保留不改变队列模型的编译清理，例如补齐 `esp_audio_dec_out_frame_t` / `esp_audio_enc_out_frame_t` 字段初始化，降低 warning 干扰。

## 后续建议

若后续再次讨论“固定块池 / FreeRTOS queue / ringbuffer”，应先输出完整方案，覆盖：

- Opus task 如何等待 encode/decode/playback 多个条件；
- stop/shutdown 如何唤醒所有阻塞点；
- PCM buffer 谁借出、谁归还；
- decode/send/playback/testing 队列是否一起迁移；
- 与官方实现偏离后的验证方法。



## official-chat-config-completeness-audit


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



## official-chat-migration-prerequisites


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



## official-chat-ram-alignment-to-xiaozhi-reference


当目标是缩小 `official_chat` AI 对话路径的**内部 RAM**占用、且用户明确要求“跟 `D:\xiaozhiai\xiaozhi-esp32` 例程一样”时，只对齐那些能够在两边代码里直接一一对应的参数，不顺手改当前仓库自己扩出来的任务或 UI 结构。


对齐来源：

- `D:\xiaozhiai\xiaozhi-esp32\main\audio\audio_service.cc`
- `D:\xiaozhiai\xiaozhi-esp32\main\audio\processors\afe_audio_processor.cc`
- `D:\xiaozhiai\xiaozhi-esp32\main\audio\wake_words\afe_wake_word.cc`
- `D:\xiaozhiai\xiaozhi-esp32\sdkconfig`

当前仓库对应调整为：

- `components/official_chat/audio/audio_service.h`
  - `kAudioInputTaskStackBytes = 6144`
  - `kAudioOutputTaskStackBytes = 4096`
  - `kAudioOpusTaskStackBytes = 24576`
- `components/official_chat/audio/processors/afe_audio_processor.cc`
  - `afe_proc` 任务栈 `4096`
- `components/official_chat/audio/wake_words/afe_wake_word.cc`
  - `afe_wake` 检测任务栈 `4096`
  - `kWakeWordEncodeTaskStackBytes = 4096 * 6`
- `sdkconfig`
  - `CONFIG_LWIP_TCPIP_RECVMBOX_SIZE=16`
  - `CONFIG_LWIP_UDP_RECVMBOX_SIZE=6`


- `components/official_chat/application.cc` 的 `kApplicationWorkerTaskStackBytes`
  - 原因：当前仓库存在 `official_chat_service` 和额外 shutdown fence，和例程不是同构结构，不能机械照搬。
- `sdkconfig` 里的 `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN / OUT_CONTENT_LEN`
  - 原因：当前仓库与例程本来已经一致，不属于需要继续压缩的差异项。
- PSRAM 渲染缓冲
  - 原因：本轮目标是内部 RAM，不是 PSRAM。


源码级回归：

- `tests/test_official_chat_ram_alignment_source.py`
- `tests/test_official_chat_dependency_source.py`

构建验证：

- 修改 `sdkconfig` 后必须先 `idf.py fullclean`
- 再执行 `idf.py build`


- 适用于“想先跟官方/参考例程收敛内部 RAM”这一类保守调参。
- 不等价于“当前仓库内部 RAM 已经最优”，只是把**可直接证明的差异**先收平。
- 若后续继续压缩，优先基于真机 `uxTaskGetStackHighWaterMark()`、heap watermark 和音频稳定性日志再做下一轮，而不是继续机械抄例程。


