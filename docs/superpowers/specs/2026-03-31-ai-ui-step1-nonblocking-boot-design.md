# AI UI Step 1 Nonblocking Boot Design

## 问题重述

当前仓库 `D:\esp32S3\111` 的正式主启动流是：

- `D:\esp32S3\111\main\111.c` 调用 `hardware_init()`
- `D:\esp32S3\111\main\hardware_init.c` 内部阻塞等待 Wi‑Fi 连接成功
- 只有在 `hardware_init()` 返回成功后，才会启动：
  - `lvgl_task`
  - `time_and_weather`

这条启动链在最初只服务“联网后再进入主功能”时还能接受，但现在用户明确提出两点：

1. 后续要把 AI 对话逐步融入 UI 功能
2. 没网的时候也要能正常使用手表

这意味着当前“硬件初始化阻塞到联网成功”的模型已经不再适用。  
如果不先拆掉这层阻塞，后面无论是 AI 页面还是独立音乐应用，都会被启动阶段的联网等待绑住。

## 目标

本轮目标是 AI 融入 UI 的第一步，只做“启动解耦与后台联网”：

1. `hardware_init()` 改为只完成硬件 bring-up，不再阻塞等待联网
2. `111.c` 在硬件初始化后立即启动 UI
3. 新增后台联网服务任务，在系统进入 UI 后继续联网和服务探测
4. 为后续 AI 页面提供“未联网 / 联网中 / 服务可用”的状态基础

验收目标：

- 没网时 UI 也能启动
- `hardware_init()` 返回语义变成“硬件 ready”，而不是“网络 ready”
- 后台联网仍复用当前 `wifi_provision`
- 当前 AI 实验入口逻辑中的 DNS/服务探测能力开始向统一后台联网服务收敛

## 非目标

本轮明确不做：

- 正式 AI 页面
- AI 页面 UI 入口
- `official_chat` 正式并回 `111.c`
- 音乐应用页面
- AI 与音乐播放器音频抢占策略
- 全局模式管理器
- `hardware_init.c` 内直接启动 `official_chat`

## 现状与证据

### 当前正式启动流

来自：

- `D:\esp32S3\111\main\111.c`
- `D:\esp32S3\111\main\hardware_init.c`

当前行为：

- `hardware_init()` 内部创建事件组
- 初始化 `wifi_provision`
- 调用 `xEventGroupWaitBits(..., portMAX_DELAY)` 永久等待联网成功
- 只有等到连接成功后，`111.c` 才会继续启动 LVGL 和时间天气任务

这会导致：

- 没网时手表主界面迟迟起不来
- UI 功能被 Wi‑Fi 阻塞
- AI 后续接入时只能继续依赖一条“联网优先”的启动链

### 当前 AI 实验入口

来自：

- `D:\esp32S3\111\main\main_ai_chat_experiment.c`

已知能力：

- `wifi_provision_start_auto()`
- 等待 `wifi_provision_is_connected()`
- `getaddrinfo()` 探测 `api.tenclass.net` 和 `mqtt.xiaozhi.me`

说明仓库里已经有：

- 本地联网自动启动能力
- AI 服务前的 DNS/服务就绪探测能力

只是它们还停留在实验入口里，没有沉到正式主流程可复用的后台联网层。

### 当前用户期望

用户明确要求：

- “先完成硬件初始化，联网在后台继续进行”
- “显示未联网状态并引导进入本地 AP 配网，我需要没网的时候也能用手表”

这决定了正式启动模型必须从“阻塞联网”切换到“非阻塞联网”。

## 方案比较

### 方案 A：只把 AI 实验入口的联网 helper 并进 `hardware_init.c`

优点：

- 表面改动少

缺点：

- `hardware_init.c` 仍然承担过多应用语义
- UI 启动仍可能继续受联网状态影响
- 没有真正形成统一后台联网层

结论：不采用

### 方案 B：把 `hardware_init()` 改成非阻塞硬件 bring-up，并新增后台联网服务

优点：

- 最符合“手表离线也可用”的目标
- 后续 AI 页面、音乐应用、天气服务都能复用
- `hardware_init` 边界更清晰

缺点：

- 需要新引入一个后台网络服务层
- 要调整 `111.c` 的启动顺序

结论：采用

### 方案 C：直接把 `official_chat` 并回 `111.c`，同时重写启动流

优点：

- 一步到位

缺点：

- 范围过大
- 无法隔离是启动链问题还是 AI 整合问题

结论：不采用

## 选定方案

采用方案 B：

- `hardware_init` 只做硬件初始化
- `111.c` 在硬件 ready 后立即启动 UI
- 新增后台联网服务负责：
  - 自动联网
  - 状态维护
  - DNS / AI 服务探测

这一步是“AI 融入 UI”的底座，不是 AI 功能本身。

## 设计

### 1. `hardware_init` 新职责

`D:\esp32S3\111\main\hardware_init.c` 修改后只负责：

- NVS 初始化
- Audio SPIFFS 初始化
- SD 卡初始化
- `audio_codec` 初始化
- 按键初始化
- `wifi_provision_init(...)`
- 注册 Wi‑Fi 状态回调

它不再负责：

- 阻塞等待 Wi‑Fi 连接成功
- 决定 AI 服务是否可以启动
- 决定 UI 是否可以进入

新的返回语义是：

- `ESP_OK` = 基础硬件和本地联网组件初始化成功
- 不是“网络已经可用”

### 2. 正式主入口先起 UI，再起后台联网

`D:\esp32S3\111\main\111.c` 修改后：

- 在 `hardware_init() == ESP_OK` 后立即创建：
  - `lvgl_task`
  - `time_and_weather`
  - 后台联网任务（新服务）

不再要求：

- UI 必须等 Wi‑Fi 成功后才能起

这样可以满足：

- 没网时也能进入表盘/主界面
- 后台继续处理联网和配网相关状态

### 3. 新增后台联网服务层

建议新增：

- `D:\esp32S3\111\main\network_service.h`
- `D:\esp32S3\111\main\network_service.c`

职责：

1. 启动 `wifi_provision_start_auto()`
2. 维护联网状态
3. 在拿到 STA IP 后继续做 DNS/服务探测
4. 对 UI / AI 服务提供统一状态读取接口

建议暴露的最小状态：

- `NETWORK_SERVICE_STATE_OFFLINE`
- `NETWORK_SERVICE_STATE_CONNECTING`
- `NETWORK_SERVICE_STATE_WIFI_READY`
- `NETWORK_SERVICE_STATE_SERVICE_READY`
- `NETWORK_SERVICE_STATE_PORTAL_REQUIRED`
- `NETWORK_SERVICE_STATE_ERROR`

### 4. 分离“Wi‑Fi ready”和“AI service ready”

这是本轮最关键的设计点。

不能只判断：

- `wifi_provision_is_connected()`

因为之前真机已经证明：

- 刚拿到 STA IP 时，`api.tenclass.net` / `mqtt.xiaozhi.me` 可能还没 ready

所以后台联网服务要分两级：

1. `wifi_ready`
   - 已连接 AP
   - 已拿到 STA IP

2. `service_ready`
   - DNS 探测通过
   - AI 服务可以真正启动

后续 AI 页面才能根据这两个状态给出正确提示。

### 5. 未联网时 AI 页面预期行为

虽然本轮不做 AI 页面，但状态设计必须先服务这个目标。

后续进入 AI 页面时：

- 如果 `offline` 或 `portal_required`
  - 显示未联网状态
  - 引导进入本地 AP 配网
- 如果 `connecting`
  - 显示正在联网
- 如果 `wifi_ready`
  - 显示“网络可用，服务准备中”
- 如果 `service_ready`
  - 才允许真正启动 `official_chat`

这正好满足用户“没网也能用手表，但 AI 页面要能引导配网”的目标。

### 6. 复用现有本地配网，不引入第二套 owner

后台联网服务依然必须基于：

- `D:\esp32S3\111\components\wifi_provision`
- `D:\esp32S3\111\components\wifi_provision\src\wifi_driver\wifi_manager.c`

要求：

- 不引入 `hal_wifi`
- 不新建第二套 Wi‑Fi owner
- AP 配网仍然走现有页面和地址逻辑

## 文件划分

### 需要修改

- `D:\esp32S3\111\main\111.c`
- `D:\esp32S3\111\main\hardware_init.c`
- `D:\esp32S3\111\main\hardware_init.h`（如需暴露新语义或状态辅助接口）

### 需要新增

- `D:\esp32S3\111\main\network_service.c`
- `D:\esp32S3\111\main\network_service.h`
- 对应测试文件（源码级测试）

### 暂不修改

- `D:\esp32S3\111\components\official_chat\...`
- `D:\esp32S3\111\components\mp3_player\...`
- `D:\esp32S3\111\main\ui\generated\...`
- `D:\esp32S3\111\main\ui\custom\...`
- `D:\esp32S3\111\main\time_weather.c`（除非为适应新的启动顺序需要极小改动）

## 风险分析

### 1. 启动时序变化风险

风险：

- UI 先起后，可能暴露出此前被联网阻塞掩盖的问题

规避：

- 第一阶段只做启动解耦
- 不同时并入 AI 页面和音乐应用

### 2. `time_and_weather` 对联网时机的隐式依赖

风险：

- `time_and_weather()` 当前会先 `esp_wait_sntp_sync()`
- 如果 UI 很早启动，而 SNTP 还没 ready，可能影响该任务的启动节奏

规避：

- 本轮重点只先改正式启动链
- 必要时后续再单独评估 `time_and_weather` 是否也要后台等待网络

### 3. 状态来源混乱

风险：

- 如果 UI、AI 服务、`wifi_provision` 各自维护一套联网判断，会越来越乱

规避：

- 本轮就建立统一的 `network_service` 状态层

### 4. AI 页面需求被过早带入

风险：

- 容易一边做启动解耦，一边顺手做 AI 页面入口，导致范围失控

规避：

- 明确本轮不做 AI 页面
- 只搭底座

## 验证计划

### 源码级验证

新增或扩展测试，验证：

1. `hardware_init.c` 不再出现 `xEventGroupWaitBits(... portMAX_DELAY ...)`
2. `111.c` 在 `hardware_init()` 后仍会创建 `lvgl_task`
3. `111.c` 新增后台联网任务创建逻辑
4. `network_service.c` 包含：
   - `wifi_provision_start_auto()`
   - `wifi_provision_is_connected()`
   - `getaddrinfo()` 探测
   - `api.tenclass.net`
   - `mqtt.xiaozhi.me`

### 构建验证

- `. "$env:IDF_PATH\export.ps1"; idf.py build`

目标：

- 启动链重构后主工程仍可构建

### 运行时验证

第一轮真机重点看：

1. 没网时是否仍能进入 UI
2. 后台是否会自动进入联网/配网流程
3. 日志中是否能看到后台联网状态变化

本轮不要求：

- AI 页面已经可用
- AI 对话已经正式并回 UI

## 回滚策略

如果本轮引入问题，可按最小方式回退：

1. 恢复 `hardware_init()` 中的阻塞等待联网逻辑
2. 停用 `network_service` 任务创建
3. 保持 `111.c` 回到原始启动顺序

因为本轮不改 `official_chat` 和 UI 页面本体，回滚边界比较清晰。

## 后续顺序

完成本轮后，推荐后续顺序：

1. 在 UI 中接入 AI 页面入口
2. AI 页面读取 `network_service` 状态显示“未联网 / 联网中 / 服务可用”
3. 再把 `official_chat_service` 并到 UI 路径

也就是说：

- 先解耦启动
- 再接状态页面
- 最后接 AI 会话本体

## 适用边界

本设计只适用于：

- AI 融入 UI 的第一步
- 正式启动流从“阻塞联网”切到“后台联网”

不适用于：

- 直接实现 AI 页面
- 直接把 `official_chat` 并进 `hardware_init.c`
- 直接处理 AI / 音乐 / 其他功能的统一模式管理
