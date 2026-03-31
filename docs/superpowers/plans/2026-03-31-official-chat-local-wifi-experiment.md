# Official Chat Local WiFi Experiment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不破坏当前 `111` 正式主流程的前提下，引入 `official_chat` 并通过单独实验入口打通最小可运行 AI 对话链路，Wi‑Fi 统一复用本地 `wifi_provision`。

**Architecture:** 当前 `wifi_provision + wifi_manager` 继续作为唯一 Wi‑Fi owner，只补 `official_chat` 需要的最小 runtime helper。`official_chat` 直接从 `hal_wifi` 切到 `wifi_provision`，并通过单独的 `main_ai_chat_experiment.c` 入口在编译期开关控制下运行，不影响现有 `main/111.c`。

**Tech Stack:** ESP-IDF 5.5.x, ESP32-S3, FreeRTOS, C/C++, `wifi_provision`, `official_chat`, `unittest`

---

### Task 1: 给当前本地 Wi‑Fi 栈补 `official_chat` 需要的最小 runtime helper

**Files:**
- Modify: `D:\esp32S3\111\components\wifi_provision\include\wifi_provision.h`
- Modify: `D:\esp32S3\111\components\wifi_provision\src\wifi_provision.c`
- Modify: `D:\esp32S3\111\components\wifi_provision\src\wifi_driver\wifi_manager.h`
- Modify: `D:\esp32S3\111\components\wifi_provision\src\wifi_driver\wifi_manager.c`
- Test: `D:\esp32S3\111\tests\test_wifi_runtime_helper_source.py`

- [ ] **Step 1: 先写失败中的源码回归测试**

新增 `D:\esp32S3\111\tests\test_wifi_runtime_helper_source.py`，断言：

- `wifi_provision.h` 暴露：
  - `wifi_provision_start_auto`
  - `wifi_provision_is_connected`
  - `wifi_provision_get_ip`
  - `wifi_provision_set_power_save`
  - `wifi_provision_set_credentials`
  - `wifi_provision_has_credentials`
- `wifi_manager.h` 暴露：
  - `wifi_manager_is_connected`
  - `wifi_manager_get_ip`
  - `wifi_manager_set_power_save`
  - `wifi_manager_set_credentials`
  - `wifi_manager_has_credentials`
  - `wifi_manager_connect_saved`
- `wifi_provision.c` 通过 helper 转发到 `wifi_manager`
- 不改变现有 AP 页面入口函数 `wifi_provision_start_apcfg`

- [ ] **Step 2: 运行测试确认当前先失败**

Run: `python -m unittest tests.test_wifi_runtime_helper_source -v`

Expected:
- 至少 1 个失败，证明 helper 还未补齐

- [ ] **Step 3: 实现最小 helper，不替换现有配网页版本**

要求：

- 保留当前 `192.168.100.1` 配网页行为
- 仅补 `official_chat` 需要的 runtime helper
- `wifi_provision_start_auto()` 策略定义为：
  - 有本地凭据时连接 STA
  - 无凭据时进入当前 AP 配网页

- [ ] **Step 4: 重新运行源码测试确认通过**

Run: `python -m unittest tests.test_wifi_runtime_helper_source -v`

Expected:
- 全部通过

### Task 2: 迁移 `official_chat` 并去掉 `hal_wifi` 依赖

**Files:**
- Create: `D:\esp32S3\111\components\official_chat\...`
- Test: `D:\esp32S3\111\tests\test_official_chat_source.py`

- [ ] **Step 1: 先写失败中的 `official_chat` 迁移测试**

新增 `D:\esp32S3\111\tests\test_official_chat_source.py`，断言：

- `components/official_chat/CMakeLists.txt` 依赖 `wifi_provision`，不依赖 `hal_wifi`
- `application.cc` 不再 include `hal_wifi.h`
- `application.cc` 使用 `wifi_provision_set_power_save(...)`
- `mcp_server.cc` 使用：
  - `wifi_provision_is_connected()`
  - `wifi_provision_get_ip(...)`
- `mcp_server.cc` 不再调用：
  - `hal_wifi_get_connect_state()`
  - `hal_wifi_get_ip()`

- [ ] **Step 2: 运行测试确认当前先失败**

Run: `python -m unittest tests.test_official_chat_source -v`

Expected:
- 因 `components/official_chat` 尚不存在或仍缺转换而失败

- [ ] **Step 3: 迁入 `official_chat` 组件并做最小 Wi‑Fi 改造**

来源优先级：

1. 组件骨架与主要源码来自 `C:\Users\ye\Desktop\idf-xiaozhi\components\official_chat`
2. `hal_wifi -> wifi_provision` 的改法优先参考 `C:\Users\ye\Desktop\idf-EDGE_lmpulse\components\official_chat`

要求：

- 不保留 `hal_wifi` 为活动依赖
- Wi‑Fi 状态/省电/取 IP 一律改走 `wifi_provision`
- 只做最小可运行链路需要的改造，不提前做 UI 深度集成

- [ ] **Step 4: 重新运行 `official_chat` 源码测试确认通过**

Run: `python -m unittest tests.test_official_chat_source -v`

Expected:
- 全部通过

### Task 3: 新增独立 AI 实验入口并挂到编译开关

**Files:**
- Modify: `D:\esp32S3\111\main\CMakeLists.txt`
- Create: `D:\esp32S3\111\main\Kconfig.projbuild`
- Create: `D:\esp32S3\111\main\main_ai_chat_experiment.c`
- Test: `D:\esp32S3\111\tests\test_official_chat_experiment_source.py`

- [ ] **Step 1: 先写失败中的实验入口测试**

新增 `D:\esp32S3\111\tests\test_official_chat_experiment_source.py`，断言：

- `main_ai_chat_experiment.c` 存在
- 实验入口执行顺序包含：
  - `nvs_flash_init`
  - `wifi_provision_init`
  - `wifi_provision_start_auto`
  - 等待联网成功
  - `audio_codec_init`
  - `official_chat_create`
  - `official_chat_start`
- `main/CMakeLists.txt` 在开关打开时编译实验入口
- `main/Kconfig.projbuild` 暴露类似 `CONFIG_APP_AI_CHAT_EXPERIMENT` 的实验开关
- 当前 `main/111.c` 仍保留为正式入口

- [ ] **Step 2: 运行测试确认当前先失败**

Run: `python -m unittest tests.test_official_chat_experiment_source -v`

Expected:
- 因实验入口和开关尚不存在而失败

- [ ] **Step 3: 实现实验入口与构建挂载**

要求：

- 默认关闭实验入口
- 关闭时当前 `111.c` 行为不变
- 打开时只编译/运行实验入口，不强行把正式 UI 主流程和 AI 对话绑在一起
- 实验入口复用本地配网，不写死 Wi‑Fi 凭据

- [ ] **Step 4: 重新运行实验入口测试确认通过**

Run: `python -m unittest tests.test_official_chat_experiment_source -v`

Expected:
- 全部通过

### Task 4: 补依赖、最小 `sdkconfig` 闭环、完整验证与上下文更新

**Files:**
- Modify: `D:\esp32S3\111\sdkconfig`
- Modify/Create: 依赖组件目录与 `managed_components` 所需配置文件
- Modify: `D:\esp32S3\111\docs\context\CHANGELOG.md`
- Create: `D:\esp32S3\111\docs\context\knowledge\project\official-chat-local-wifi-experiment.md`

- [ ] **Step 1: 写失败中的依赖/配置回归测试**

新增或扩展测试，断言最小可运行链路必需项：

- `sdkconfig` 包含最小 `official_chat` 关键项：
  - `CONFIG_OFFICIAL_CHAT_LANGUAGE_CODE`
  - `CONFIG_OFFICIAL_CHAT_OTA_URL`
- 活动组件图包含：
  - `official_chat`
  - `wifi_provision`
- 不要求第一轮一次性补全全部 `SR_*` 语音模型项，但最小编译依赖必须能解析

- [ ] **Step 2: 运行测试确认当前先失败**

Run: `python -m unittest discover -s tests -p 'test_official_chat*source.py' -v`

Expected:
- 因组件或配置尚未齐全而失败

- [ ] **Step 3: 补齐最小可编译/可启动依赖**

要求：

- 优先补足 `official_chat` 真正编译所需的最小依赖
- 不一次性照搬整份 `idf-xiaozhi` `sdkconfig`
- 仅补第一轮最小可运行需要的 `CONFIG_*`

- [ ] **Step 4: 运行源码回归全集**

Run: `python -m unittest tests.test_wifi_runtime_helper_source tests.test_official_chat_source tests.test_official_chat_experiment_source tests.test_touch_ft5x06_i2c_mode_source tests.test_audio_codec_port_source tests.test_i2c_master_bus_sdkconfig -v`

Expected:
- 全部通过

- [ ] **Step 5: 完整构建验证**

Run: `. "$env:IDF_PATH\export.ps1"; idf.py reconfigure build`

Expected:
- `exit 0`

- [ ] **Step 6: 更新长期上下文并重建索引**

记录：

- `official_chat` 已切到本地 `wifi_provision`
- AI 对话实验入口是独立入口，不影响当前主流程
- 本地配网仍保持 `192.168.100.1`
- 本轮最小可运行链路的适用边界与未验证风险

Run:
- `uv run python scripts/context/build_index.py`
- `uv run python scripts/context/check.py`

Expected:
- `错误: 0，警告: 0`
