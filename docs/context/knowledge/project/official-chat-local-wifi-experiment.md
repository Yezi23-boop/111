---
id: official-chat-local-wifi-experiment
tags: [project, official-chat, wifi-provision, esp32s3, audio, experiment]
summary: 当前仓库已将 official_chat 接到本地 wifi_provision，并通过独立实验入口接通最小 AI 对话链路。
last_reviewed: 2026-03-31
---

# official_chat 本地 Wi-Fi 实验入口

## 结论

- 当前仓库已经引入本地组件 `components/official_chat` 和 `components/utils`。
- `official_chat` 不再依赖 `hal_wifi`，运行时统一调用本地 `wifi_provision_*` helper。
- 当前仓库的 `wifi_provision` 继续作为唯一 Wi-Fi owner，保留既有 AP 配网页行为和 `192.168.100.1` 地址。
- AI 对话实验入口独立放在 `main/main_ai_chat_experiment.c`，默认关闭，不影响正式入口 `main/111.c`。

## 本轮关键实现

- `components/wifi_provision/include/wifi_provision.h`
  - 增加 `wifi_provision_start_auto`
  - 增加 `wifi_provision_is_connected`
  - 增加 `wifi_provision_get_ip`
  - 增加 `wifi_provision_set_power_save`
  - 增加 `wifi_provision_set_credentials`
  - 增加 `wifi_provision_has_credentials`
- `components/wifi_provision/src/wifi_driver/wifi_manager.c`
  - 新增 NVS 凭据保存/加载
  - `STA_START` 不再隐式自动连接，改为等待显式连接请求
  - 提供 `wifi_manager_connect_saved`、连接状态、IP、power save helper
  - 保留 `wifi_manager_private.h` 中 `.ap_ip = "192.168.100.1"`
- `components/official_chat`
  - 采用 `idf-EDGE_lmpulse` 版本作为迁移基线
  - 保留 `wifi_provision` 适配后的调用路径
  - 在 `ota.cc` 中去掉 `try/catch + std::stoi`，改成 `strtol` 解析版本号，避免启用全局 C++ exceptions
- `main/CMakeLists.txt`
  - 改为根据 `CONFIG_APP_AI_CHAT_EXPERIMENT` 在 `111.c` 和 `main_ai_chat_experiment.c` 之间切换入口
  - `main` 组件增加 `official_chat` 依赖
- `main/Kconfig.projbuild`
  - 增加 `official_chat` 所需最小 Kconfig
  - 增加 `APP_AI_CHAT_EXPERIMENT`
- `main/idf_component.yml`
  - 新增 managed components:
    - `espressif/esp_audio_codec`
    - `espressif/esp_audio_effects`
    - `espressif/esp-sr`

## 实验入口链路

`main/main_ai_chat_experiment.c` 的启动顺序是：

1. `nvs_flash_init`
2. `wifi_provision_init(NULL)`
3. `wifi_provision_start_auto()`
4. 轮询 `wifi_provision_is_connected()`
5. `audio_codec_init()`
6. `official_chat_create()`
7. `official_chat_set_event_callback()`
8. `official_chat_start()`

## 配置闭环

- `sdkconfig` 已补最小 `official_chat` 键：
  - `CONFIG_USE_AUDIO_PROCESSOR=y`
  - `CONFIG_SEND_WAKE_WORD_DATA=y`
  - `CONFIG_OFFICIAL_CHAT_OTA_URL="https://api.tenclass.net/xiaozhi/ota/"`
  - `CONFIG_OFFICIAL_CHAT_LANGUAGE_CODE="zh-CN"`
  - `CONFIG_MODEL_IN_FLASH=y`
- `lwip` 为 `official_chat` 的 UDP 下行路径补了：
  - `CONFIG_LWIP_SO_RCVBUF=y`
  - `CONFIG_LWIP_UDP_RECVMBOX_SIZE=12`
  - `CONFIG_UDP_RECVMBOX_SIZE=12`

## 验证结果

- `python -m unittest tests.test_wifi_runtime_helper_source tests.test_official_chat_source tests.test_official_chat_experiment_source tests.test_official_chat_dependency_source tests.test_touch_ft5x06_i2c_mode_source tests.test_audio_codec_port_source tests.test_i2c_master_bus_sdkconfig -v`
  - 全部通过
- `. "$env:IDF_PATH\export.ps1"; idf.py reconfigure build`
  - 通过
- `. "$env:IDF_PATH\export.ps1"; idf.py build`
  - 通过

## 后续修正

- `main/main_ai_chat_experiment.c` 增加了启动前网络服务探测：
  - 在拿到 STA IP 后，额外用 `getaddrinfo()` 轮询探测 `api.tenclass.net` 和 `mqtt.xiaozhi.me`
  - 目标是降低“刚拿到 IP 就立刻做 OTA / MQTT 访问”时的 DNS 竞态
  - 如果超时仍未就绪，只记录告警并继续启动，避免把实验入口卡死
- `components/wifi_provision/src/wifi_driver/wifi_manager.c` 调整了 AP IP 配置顺序：
  - 先把 `192.168.100.1` 写入 `ap_netif`
  - 再启用 `APSTA`
  - 门户日志改为打印运行时实际 AP IP，而不是仅打印配置常量

## 当前边界

- 默认固件入口仍然是 `main/111.c`，不会自动启用 AI 对话实验。
- 本轮只保证最小可编译、可启动链路和本地配网复用，不包含正式 UI 集成。
- `official_chat` 运行时是否能在当前硬件上完整完成激活、协议连接、语音上下行，还需要真机烧录验证。
