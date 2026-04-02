---
id: official-chat-ram-alignment-to-xiaozhi-reference
tags: [project, official-chat, ram, esp32s3, audio, lwip]
summary: 记录当前仓库按 D:\xiaozhiai\xiaozhi-esp32 例程对齐的 AI 对话内部 RAM 配置，仅收敛可一一对应的栈与 lwIP mailbox 参数。
last_reviewed: 2026-04-02
---

# 背景

当目标是缩小 `official_chat` AI 对话路径的**内部 RAM**占用、且用户明确要求“跟 `D:\xiaozhiai\xiaozhi-esp32` 例程一样”时，只对齐那些能够在两边代码里直接一一对应的参数，不顺手改当前仓库自己扩出来的任务或 UI 结构。

# 可直接对齐的项

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

# 明确不在这次“例程对齐”里改的项

- `components/official_chat/application.cc` 的 `kApplicationWorkerTaskStackBytes`
  - 原因：当前仓库存在 `official_chat_service` 和额外 shutdown fence，和例程不是同构结构，不能机械照搬。
- `sdkconfig` 里的 `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN / OUT_CONTENT_LEN`
  - 原因：当前仓库与例程本来已经一致，不属于需要继续压缩的差异项。
- PSRAM 渲染缓冲
  - 原因：本轮目标是内部 RAM，不是 PSRAM。

# 验证闭环

源码级回归：

- `tests/test_official_chat_ram_alignment_source.py`
- `tests/test_official_chat_dependency_source.py`

构建验证：

- 修改 `sdkconfig` 后必须先 `idf.py fullclean`
- 再执行 `idf.py build`

# 适用边界

- 适用于“想先跟官方/参考例程收敛内部 RAM”这一类保守调参。
- 不等价于“当前仓库内部 RAM 已经最优”，只是把**可直接证明的差异**先收平。
- 若后续继续压缩，优先基于真机 `uxTaskGetStackHighWaterMark()`、heap watermark 和音频稳定性日志再做下一轮，而不是继续机械抄例程。
