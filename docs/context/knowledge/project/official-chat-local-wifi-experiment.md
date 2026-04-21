---
id: official-chat-local-wifi-experiment
tags: [project, official-chat, wifi-provision, esp32s3, audio, experiment]
summary: 历史实验卡：记录仓库早期通过独立实验入口验证 official_chat 接入本地 wifi_provision 的最小 AI 对话链路；该入口和旧网络 owner 均已退场。
last_reviewed: 2026-04-21
---

# official_chat 本地 Wi-Fi 实验入口（历史记录）

## 使用边界

- 本文只保留“独立实验入口时期”的迁移经验与排障线索。
- 文中提到的 `main_ai_chat_experiment.c`、`ai_experiment_ui.c`、`111.c` 切换逻辑都属于历史实现，当前仓库**不应**再把它们当作真实入口或现行结构来理解。
- 当前正式链路应以：
  - `main/app/app_main.c`
  - `main/services/official_chat_service.c`
  - `main/ui/custom/ai_ui_controller.c`
  为准。

## 结论

- 当前仓库已经引入本地组件 `components/official_chat` 和 `components/utils`。
- 在这段历史实验时期，`official_chat` 曾通过本地 `wifi_provision_*` helper 接入 Wi-Fi。
- 但当前仓库已经不再保留这条运行时路径；今天的正式网络 owner 已切到：
  - `wifi_control`
  - `network_manager`
  - `network_provisioning_adapter`
  - `ap_portal_adapter`
- 当时的 AI 对话实验入口独立放在 `main/main_ai_chat_experiment.c`，用于隔离验证，不影响正式入口。
- 截至 `2026-04-08`，该实验入口链路已从仓库删除，正式主流程收敛到 `main/app/app_main.c + official_chat_service`。

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
  - 当时保留 `wifi_provision` 适配后的调用路径
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

- 本文主要保留“独立实验入口”阶段的迁移经验，不再描述当前默认构建入口。
- 当前默认固件入口已是 `main/app/app_main.c`，不会再编译或启用 `main_ai_chat_experiment.c`。
- 当前仓库也已经不再保留 `wifi_provision` 组件本身；文中所有 `wifi_provision_*` 接口都属于历史实验阶段信息。
- `official_chat` 运行时是否能在当前硬件上完整完成激活、协议连接、语音上下行，仍需以正式主流程真机验证为准。

## 显示内存压力修正

- 真机验证独立实验页时，出现持续日志：
  - `spi_master: setup_dma_priv_buffer(): Failed to allocate priv TX buffer`
  - `lv_port: Display flush failed: ESP_ERR_NO_MEM`
- 该问题不是 `official_chat` 协议失败，而是 AI 音频链路启用后，LCD SPI 刷屏申请内部 DMA 私有 TX buffer 失败。
- 当前最小修正是在 `components/lvgl_port/lv_port_config.h` 中把：
  - `LV_PORT_FIXED_CHUNK_LINES`
  - 从 `30` 调整为 `10`
- 目的：把单次刷屏需要的内部 DMA 临时缓冲压低，优先保证独立 AI 实验页在 AFE/I2S 同时运行下还能刷新显示。
