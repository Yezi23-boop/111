# Official Chat Local WiFi Experiment Design

## 问题重述

当前仓库 `D:\esp32S3\111` 已有一套稳定可用的本地 Wi‑Fi 管理路径：

- `main/hardware_init.c` 负责上电初始化
- `components/wifi_provision` 提供 AP 网页配网
- `components/wifi_provision/src/wifi_driver/wifi_manager.c` 持有实际 STA/AP 控制逻辑

后续需要接入 `C:\Users\ye\Desktop\idf-xiaozhi\components\official_chat` 的 AI 对话能力，但用户已明确要求：

- 以当前仓库本地 Wi‑Fi 管理为主
- 不保留 `hal_wifi` 作为长期兼容层
- `official_chat` 直接改成依赖本地 `wifi_provision` / `wifi_manager`
- AI 对话以单独实验入口接入，不影响当前正式主流程
- 复用现有本地配网，不接受实验阶段写死 Wi‑Fi 凭据

## 目标

第一阶段目标是把 AI 对话最小可运行链路接通，并且不破坏现有 `111` 固件路线。

验收目标：

1. 当前正式入口 `main/111.c` 保持现状
2. 新增单独实验入口，编译时可选择启用
3. 实验入口复用本地 `wifi_provision` 完成联网
4. `official_chat` 不再依赖 `hal_wifi`
5. `official_chat` 可以被创建、启动，并输出状态变化日志
6. 整体可以编译通过，并具备后续真机验证基础

## 非目标

本轮不追求：

- 把 AI 对话集成进现有 LVGL 主界面
- 把 `official_chat` 做成默认产品主流程
- 一次性完成所有 OTA、资产下载、模型切换与 UI 联动
- 在仓库中长期保留第二套公网 Wi‑Fi owner

## 现状与证据

### 当前仓库的 Wi‑Fi owner

当前仓库主启动链路来自：

- `D:\esp32S3\111\main\111.c`
- `D:\esp32S3\111\main\hardware_init.c`
- `D:\esp32S3\111\components\wifi_provision\src\wifi_provision.c`
- `D:\esp32S3\111\components\wifi_provision\src\wifi_driver\wifi_manager.c`

已知事实：

- `hardware_init()` 会阻塞等待联网成功后，才启动 UI 和后续任务
- AP 配网页当前可用，地址是 `http://192.168.100.1`
- 当前本地配网链路已经过真机验证

### `idf-xiaozhi` 的 `hal_wifi` 作用

`C:\Users\ye\Desktop\idf-xiaozhi\components\hal_wifi\include\hal_wifi.h` 暴露的主要是公网运行时 API：

- `hal_wifi_power_save(...)`
- `hal_wifi_get_connect_state()`
- `hal_wifi_get_ip()`
- `hal_wifi_set_credentials(...)`
- `hal_wifi_scan(...)`
- `hal_wifi_sta_do_connect()` / `hal_wifi_sta_do_disconnect()`

### `idf-xiaozhi` 的 `official_chat` 当前对 Wi‑Fi 的依赖

已确认：

- `application.cc` 使用 `hal_wifi_power_save(...)`
- `mcp_server.cc` 使用 `hal_wifi_get_connect_state()` 和 `hal_wifi_get_ip()`
- `official_chat` 当前仍把 `hal_wifi` 当成运行时网络状态来源

### 参考仓库的融合结论

`idf-EDGE_lmpulse` 已经验证过一条可行路线：

- 不再让 `hal_wifi` 做 owner
- `official_chat` 直接切到 `wifi_provision`
- Wi‑Fi 运行时 helper 由 `wifi_provision` / `wifi_manager` 提供

该方向与本次用户约束一致。

## 方案比较

### 方案 A：保留 `hal_wifi` 兼容层，底下转发到 `wifi_provision`

优点：

- `idf-xiaozhi` 原版 `official_chat` 改动更小

缺点：

- 重新引入第二套公网 Wi‑Fi API
- owner 边界变模糊，后续排查复杂
- 与用户“以本地 Wi‑Fi 管理为主、不保留 `hal_wifi` 兼容层”的目标冲突

结论：不采用

### 方案 B：直接把 `official_chat` 改成依赖本地 `wifi_provision` / `wifi_manager`

优点：

- 只有一套 Wi‑Fi owner
- 后续结构更清晰
- 与 `idf-EDGE_lmpulse` 已验证方向一致
- 回退简单，只需关掉实验入口

缺点：

- 需要修改 `official_chat` 的少量源码调用
- 需要给当前 `wifi_provision` 补一组最小运行时 helper

结论：采用

### 方案 C：整套搬入 `idf-EDGE_lmpulse` 的 `main + feature manager + official_chat`

优点：

- 更接近已有运行路径

缺点：

- 一次引入模块过多
- 对当前仓库侵入大
- 很难把故障隔离在 Wi‑Fi / 音频 / AI 对话某一层

结论：不采用

## 选定方案

采用方案 B，并拆成两条主线：

1. 保持当前正式固件路线不变
2. 新增单独 AI 实验入口，专门跑 `official_chat`

底层原则：

- `wifi_provision + wifi_manager` 继续作为唯一 Wi‑Fi owner
- 不引入长期存在的 `hal_wifi` 兼容层
- `official_chat` 直接切到 `wifi_provision_*`
- 实验入口通过编译开关挂载，不影响正式入口

## 架构设计

### 1. 正式入口保持不变

保留：

- `D:\esp32S3\111\main\111.c`
- `D:\esp32S3\111\main\hardware_init.c`
- 当前 UI / 时钟 / MP3 路线

要求：

- 不把 `official_chat` 设成默认入口
- 不让 AI 实验逻辑侵入当前正式主流程

### 2. 新增独立实验入口

新增一个单独实验入口文件，例如：

- `D:\esp32S3\111\main\main_ai_chat_experiment.c`

职责：

1. 初始化 NVS
2. 初始化并启动本地 `wifi_provision`
3. 等待联网成功
4. 初始化音频 codec
5. 创建 `official_chat`
6. 注册 `official_chat` 事件回调
7. 启动 `official_chat`
8. 保持最小轮询/任务运行

这个入口只在实验配置打开时编译/启用。

### 3. 扩展当前 `wifi_provision` 的最小运行时 helper

保留当前已经验证可用的行为：

- AP 地址 `192.168.100.1`
- 当前网页配网协议
- 当前按钮触发 AP 配网逻辑

在此基础上补充 `official_chat` 需要的最小运行时 helper：

- `bool wifi_provision_is_connected(void);`
- `esp_err_t wifi_provision_get_ip(char *ip_str, size_t ip_str_len);`
- `esp_err_t wifi_provision_set_power_save(bool enable);`
- `esp_err_t wifi_provision_set_credentials(const char *ssid, const char *password);`
- `bool wifi_provision_has_credentials(void);`
- `esp_err_t wifi_provision_start_auto(void);`

内部转发到 `wifi_manager`，但不改变现有 AP 页面和地址。

### 4. 直接修改 `official_chat` 的 Wi‑Fi 依赖

目标是把以下运行时调用替换掉：

- `hal_wifi_power_save(...)` -> `wifi_provision_set_power_save(...)`
- `hal_wifi_get_connect_state()` -> `wifi_provision_is_connected()`
- `hal_wifi_get_ip()` -> `wifi_provision_get_ip(...)`

同时把组件依赖从 `hal_wifi` 切到 `wifi_provision`。

### 5. 通过 Kconfig/构建开关挂载实验入口

在 `main/Kconfig.projbuild` 中增加实验入口开关，例如：

- `CONFIG_APP_AI_CHAT_EXPERIMENT`

在 `main/CMakeLists.txt` 中按开关决定是否编译 `main_ai_chat_experiment.c`。

要求：

- 默认关闭
- 关闭时仍然是当前正式固件行为
- 打开时只运行实验入口

## 最小可运行链路

实验入口的启动顺序定义为：

1. `nvs_flash_init()`
2. `wifi_provision_init(...)`
3. `wifi_provision_start_auto()`  
   行为要求：
   - 有本地凭据时直接 STA 连接
   - 没有凭据时进入当前 AP 配网页
4. 等待联网成功
5. `audio_codec_init()`
6. `official_chat_create(...)`
7. `official_chat_set_event_callback(...)`
8. `official_chat_start(...)`
9. 保持实验主循环/轮询

第一轮最小可运行验收只要求：

- 能编译通过
- 能复用本地配网联网
- 能创建并启动 `official_chat`
- 能输出状态变化日志
- 能看到至少一次连接服务端尝试

## 文件划分

### 计划新增/修改的关键文件

- 修改 `D:\esp32S3\111\components\wifi_provision\include\wifi_provision.h`
- 修改 `D:\esp32S3\111\components\wifi_provision\src\wifi_provision.c`
- 修改 `D:\esp32S3\111\components\wifi_provision\src\wifi_driver\wifi_manager.h`
- 修改 `D:\esp32S3\111\components\wifi_provision\src\wifi_driver\wifi_manager.c`
- 新增 `D:\esp32S3\111\components\official_chat\...`
- 修改 `D:\esp32S3\111\main\CMakeLists.txt`
- 新增 `D:\esp32S3\111\main\main_ai_chat_experiment.c`
- 修改 `D:\esp32S3\111\main\Kconfig.projbuild`
- 新增测试文件，用于约束 `official_chat` 不再依赖 `hal_wifi`

### 文件职责

- `wifi_provision.h/.c`：对外提供唯一公网 Wi‑Fi helper
- `wifi_manager.h/.c`：持有实际 STA/AP/scan/IP/省电/凭据逻辑
- `main_ai_chat_experiment.c`：实验入口编排
- `official_chat/*`：AI 对话运行时与协议栈
- `main/Kconfig.projbuild`：实验入口选择与配置菜单

## 风险分析

### 1. Wi‑Fi owner 冲突风险

如果同时保留 `hal_wifi` 生命周期和本地 `wifi_provision` 生命周期，容易出现：

- 重复 `esp_wifi_init/start`
- 模式切换互相覆盖
- 连接状态来源不一致

规避方式：

- 明确只保留 `wifi_provision + wifi_manager` 为 owner
- `official_chat` 只读 Wi‑Fi 运行时状态，不直接接管底层

### 2. 配网页回归风险

当前 `192.168.100.1` 已经真机验证可用。若粗暴替换 `wifi_provision` 为 EDGE 版本，可能再次破坏页面进入路径。

规避方式：

- 不再整体替换 `wifi_provision`
- 只在当前版本上补 helper

### 3. 音频链路冲突风险

`official_chat` 会引入采集/播放链路，可能与当前 `audio_codec`、`mp3_player` 存在状态竞争。

规避方式：

- 第一轮实验入口不并行跑 UI/MP3 正式路线
- 先追求最小可运行，不做多功能并发

### 4. 组件依赖缺失风险

当前仓库仍缺：

- `utils`
- `esp-sr`
- `esp_audio_effects`
- `espressif__esp_audio_codec`

规避方式：

- 迁移计划先补依赖，再接 `official_chat`

### 5. `sdkconfig` 差异风险

`official_chat` 还依赖若干 `CONFIG_*`：

- `CONFIG_OFFICIAL_CHAT_LANGUAGE_CODE`
- `CONFIG_OFFICIAL_CHAT_OTA_URL`
- `CONFIG_USE_AUDIO_PROCESSOR`
- `CONFIG_USE_DEVICE_AEC`
- `CONFIG_SEND_WAKE_WORD_DATA`
- `CONFIG_WAKE_WORD_DETECTION_IN_LISTENING`

规避方式：

- 第一轮只补最小可编译/可启动集合
- 不一次性照搬整份 `sdkconfig`

## 验证计划

### 源码级验证

- 断言 `official_chat` 不再 include `hal_wifi.h`
- 断言 `official_chat` 改为依赖 `wifi_provision`
- 断言实验入口复用 `wifi_provision_start_auto()`
- 断言当前正式入口仍保留 `111.c`

### 构建验证

- `idf.py build`
- 必要时 `idf.py reconfigure build`

### 真机验证

实验入口打开后验证：

1. 有保存凭据时可直接联网
2. 无凭据时可进入当前 AP 配网页
3. 联网成功后 `official_chat` 能创建并启动
4. 可观察到 `official_chat` 状态日志
5. 不影响关闭实验入口后的正式固件构建

## 回滚策略

- `CONFIG_APP_AI_CHAT_EXPERIMENT` 默认关闭
- 关闭实验入口开关即可恢复当前正式固件行为
- 不修改 `main/111.c` 的正式主流程
- 不保留长期 `hal_wifi` 兼容层

## 实施顺序

建议实施顺序：

1. 先补 `wifi_provision` 最小 runtime helper
2. 再引入 `official_chat` 所需最小组件依赖
3. 再把 `official_chat` 的 `hal_wifi_*` 调用切到 `wifi_provision_*`
4. 最后新增实验入口与 Kconfig 开关

## 适用边界

本设计只覆盖：

- 本地 Wi‑Fi owner 统一
- `official_chat` 最小可运行接入
- 实验入口与正式入口分离

本设计不覆盖：

- 正式产品化 UI 集成
- AI 对话成为默认主入口
- 多模式并发运行时调度优化
