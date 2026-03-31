---
id: official-chat-local-wifi-experiment
tags: [project, official-chat, wifi-provision, esp32s3, audio, experiment]
summary: 当前仓库已将 official_chat 接到本地 wifi_provision，并通过独立实验入口接通最小 AI 对话链路与简易独立实验页。
last_reviewed: 2026-03-31
---

# official_chat 本地 Wi-Fi 实验入口

## 结论

- 当前仓库已经引入本地组件 `components/official_chat` 和 `components/utils`。
- `official_chat` 不再依赖 `hal_wifi`，运行时统一调用本地 `wifi_provision_*` helper。
- 当前仓库的 `wifi_provision` 继续作为唯一 Wi-Fi owner，保留既有 AP 配网页行为和 `192.168.100.1` 地址。
- AI 对话实验入口独立放在 `main/main_ai_chat_experiment.c`，默认关闭，不影响正式入口 `main/111.c`。
- 当前实验入口已改成“单独的简易 AI 页面”，不再依赖正式主菜单或 `gui_guider` 页面结构。

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

当前 `main/main_ai_chat_experiment.c` 已改成复用共享服务，启动顺序是：

1. `nvs_flash_init`
2. `wifi_provision_init(NULL)`
3. `network_service_start()`
4. `audio_codec_init()`
5. `official_chat_service_init()`
6. `ai_experiment_ui_start()`

也就是说，实验入口和正式 UI 主流程现在都共享同一个 `official_chat_service`，但实验入口会由 `ai_experiment_ui` 页面在 `SERVICE_READY` 后自动调用 `official_chat_service_enter_foreground()`。

## 独立实验页

- 新增：
  - `main/ai_experiment_ui.h`
  - `main/ai_experiment_ui.c`
- 该页面不依赖主菜单和 `setup_ui()`。
- 页面职责只有 4 件事：
  - 显示网络状态
  - 显示 `official_chat_service` 状态
  - 提供“进入配网”按钮调用 `network_service_request_portal()`
  - 在 `NETWORK_SERVICE_STATE_SERVICE_READY` 后自动进入待唤醒
- 当前页面文案会映射：
  - `OFFICIAL_CHAT_SERVICE_STATE_IDLE` -> `待唤醒`
  - `OFFICIAL_CHAT_SERVICE_STATE_LISTENING` -> `聆听中`
  - `OFFICIAL_CHAT_SERVICE_STATE_SPEAKING` -> `回答中`

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
- 本轮保证最小可编译、可启动链路、本地配网复用和独立实验页显示；不要求先并回正式 UI。
- `official_chat` 运行时是否能在当前硬件上完整完成激活、协议连接、语音上下行，还需要真机烧录验证。

## 显示内存压力修正

- 真机验证独立实验页时，出现持续日志：
  - `spi_master: setup_dma_priv_buffer(): Failed to allocate priv TX buffer`
  - `lv_port: Display flush failed: ESP_ERR_NO_MEM`
- 该问题不是 `official_chat` 协议失败，而是 AI 音频链路启用后，LCD SPI 刷屏申请内部 DMA 私有 TX buffer 失败。
- 当前最小修正是在 `components/lvgl_port/lv_port_config.h` 中把：
  - `LV_PORT_FIXED_CHUNK_LINES`
  - 从 `30` 调整为 `10`
- 目的：把单次刷屏需要的内部 DMA 临时缓冲压低，优先保证独立 AI 实验页在 AFE/I2S 同时运行下还能刷新显示。
