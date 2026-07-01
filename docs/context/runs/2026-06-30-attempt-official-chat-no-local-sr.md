---
id: run-2026-06-30-official-chat-no-local-sr
tags: run, attempt, official_chat, esp-sr, sdkconfig, crash, memory
summary: 记录 official_chat 进入前台时 ESP-SR srmodel_load StoreProhibited 的诊断、证伪和最终处理：当前产品不需要本地唤醒词，official_chat 跳过本地 SR model loader。
created: 2026-06-30
last_reviewed: 2026-06-30
owners: components/official_chat, sdkconfig, tests
evidence_level: observed
---

# official_chat 去除本地 SR loader 尝试记录

## 问题签名

用户进入 `official_chat` 前台后，板端反复在 ESP-SR loader 内崩溃：

- `Guru Meditation Error: Core 0 panic'ed (StoreProhibited)`
- `srmodel_load at managed_components/espressif__esp-sr/src/model_path.c:140`
- 调用链：`official_chat::Application::InitializeAudioService()` -> `esp_srmodel_init("model")` -> `srmodel_mmap_init()` -> `srmodel_load()`

## 已证伪路线

1. 仅全量刷写旧的 4 字节 `srmodels.bin` 不能解决。
2. 打开 `CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS=y` 后，`build/srmodels/srmodels.bin` 从 4 字节变为约 291 KB，并全量刷入 `0x1010000`，但进入 `official_chat` 仍在同一处 `srmodel_load:140` 崩溃。
3. 解析 291 KB 模型包可见 wake word 模型表存在，因此崩溃不再是“模型没刷进去”的单一问题。

## 当前决策

当前产品入口由页面/按键显式触发，不需要本地 wake word。因此不再让 `official_chat` 启动路径调用 `esp_srmodel_init("model")`。

改动：

- `components/official_chat/application.cc`
  - `Application::InitializeAudioService()` 直接 `models_list_.reset()`。
  - `AudioService` 继续接收 `nullptr` 模型表；后续若误开唤醒词检测，已有路径会降级为日志告警。
- `sdkconfig`
  - 关闭 `CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS`。
  - 构建后 `build/srmodels/srmodels.bin` 回到 4 字节空表，但 official_chat 不再读取它。
- `tests/test_official_chat_source.py`
  - 增加 official_chat 跳过本地 SR loader 的源码测试。
- `tests/test_official_chat_dependency_source.py`
  - 把本地 wake word 模型期望改为关闭。

## 验证

- `uv run python -m unittest tests.test_official_chat_source tests.test_official_chat_service_source`
  - `22 passed`
- `uv run python -m unittest tests.test_official_chat_dependency_source.OfficialChatDependencySourceTests.test_sdkconfig_keeps_local_wake_word_model_disabled`
  - `1 passed`
- `idf.py fullclean; idf.py build`
  - 通过
  - `111.bin` size `0xabd480`
  - `build/srmodels/srmodels.bin` size `4`
- `idf.py -p COM3 flash`
  - 通过
  - flash 日志确认写入 `0x1010000 srmodels/srmodels.bin`，大小 4 字节
- `agent_serial_monitor.ps1` 90 秒启动监控
  - 日志：`board_logs/2026-06-30-00-57-35-official-chat-no-local-sr-repro.log`
  - 未见 `MODEL_LOADER`
  - 未见 `Guru` / `panic` / `StoreProhibited`

## 剩余边界

本轮 90 秒监控内未捕获到用户实际点击 `official_chat foreground requested`，因此“点击进入 official_chat 后完全无崩溃”仍需用户再点一次页面确认。

预期新日志：

- 不应再出现 `MODEL_LOADER: The storage free size...`
- 不应再出现 `esp_srmodel_init("model")` / `srmodel_load`
- 若后续某处误启用 wake word，应只出现 `wake word detection requested without SR models`，不应 panic。

## 后续建议

- 不要再通过打开 `CONFIG_SR_WN_*` 来解决当前产品的 `official_chat` 前台崩溃；当前产品不需要本地唤醒词。
- 若未来恢复本地 wake word，需要单独开任务评估 ESP-SR loader、模型打包格式、internal RAM 峰值和 `AudioService` 唤醒词链路，而不是混入 AI Memory Watch/Hermes 前台入口。
