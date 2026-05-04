---
id: agent-operational-rules
tags: [project, agent, workflow, esp-idf, flash, monitor, hardware, ui, lvgl]
summary: 当前仓库面向 agent 的执行型详细规则，覆盖 IDF 环境、build/flash/monitor、硬件安全边界、UI 状态读取原则和项目专项默认实践。
last_reviewed: 2026-05-04
memory_type: procedural
scope: repo
owners: AGENTS.md, docs/context/knowledge/project/agent-operational-rules.md
triggers: agent, operational, rules
evidence_level: design
---

# Agent 执行型详细规则

本卡承接 `AGENTS.md` 中不适合长期堆在入口文件里的详细规则。默认只在相关任务触发时按需阅读。

## IDF 环境与构建

- 默认使用 `FreeRTOS` 思路组织任务、同步和资源访问。
- 需要拉起 `IDF 5.5` 环境时，必须按以下顺序查找并使用 `export.ps1`：
  1. `D:\esp-idf\v5.5.3\esp-idf\export.ps1`
  2. `D:\esp32\v5.5.3\esp-idf\export.ps1`
  3. 若两者都不存在，再检查 `$env:IDF_PATH` 或搜索本机 `export.ps1`
- 只有在找到可用 `export.ps1` 后，才执行 `idf.py build` 或其他 `idf.py` 构建动作。
- 项目配置基线优先参考 `sdkconfig.defaults`；不要在没有说明的情况下大范围修改生成出来的 `sdkconfig`。
- 只要修改过 `sdkconfig`，就必须先执行 `idf.py fullclean`，再执行 `idf.py build`，避免配置变更没有进入最终产物。
- 默认不走 `test app` 路线；除非任务明确要求，否则直接修改主工程并完成验证。

## Flash 与 Monitor

- `idf.py monitor` 没有自然返回值；自动化验证时默认采集窗口为 `30` 秒，最长不超过 `1` 分钟。
- Windows Codex 桌面环境下，如需让 agent 直接读取 `idf.py monitor`，可临时设置 `ESP_IDF_MONITOR_TEST=1` 绕过 `TTY` 检查；该变量只用于 host 侧采集，不属于固件配置。
- 只要使用过 `ESP_IDF_MONITOR_TEST=1`，就必须清理残留的 `idf_monitor` / `idf.py monitor` 进程，并确认串口已释放，再进行下一轮 `flash`、`monitor` 或串口验证。
- 如果 `monitor` 日志提示 built/flashed checksum mismatch，不得把板端日志直接当作当前本地 `build` 产物的验证结论；必须单独标注镜像漂移风险，必要时重做一致的 `build` / `flash` / `monitor` 闭环。

## 硬件与安全边界

- 修改 `GPIO` / `I2C` / `SPI` / `UART` / `I2S` / `LCD` / `Touch` / `Wi-Fi` / `BLE` 前，必须先确认引脚定义、初始化顺序、时钟或带宽约束，以及错误恢复路径。
- 不要在没有证据的情况下删除已有 `reset`、`delay`、上电顺序或初始化命令序列。
- 只要涉及 `DMA`、双缓冲、`PSRAM`、cache、共享总线、共享状态机，就必须说明数据生命周期、资源归属和性能影响。
- 不要随意修改 `partition table`、boot 流程、`OTA` 逻辑、`NVS` 关键结构、`Wi-Fi/BLE` 初始化主流程；若必须修改，要显式说明原因、影响范围和验证方法。

## 状态发布与 UI 读取原则

- 适用于 `UI/LVGL`、状态栏、策略层、控制器层，以及 `Wi-Fi/BLE/Power/Audio/Sensor` 等会被高频读取的服务模块。
- 默认采用“UI 轮询只读快照，重状态推进留给后台任务或 owner 模块”的分层方式。
- `UI` 高频路径只能读取轻量快照、镜像结构体或无锁/低成本只读接口；不得在轮询或 `poll/timer/getter` 路径里顺手执行：
  - 状态推进
  - 外设 `IO`
  - 网络 / `BLE` / `Wi-Fi` 启停
  - 阻塞等待
  - 重锁路径
- 模块 owner 负责真实状态推进、重试、超时、错误恢复和硬件交互；典型形式应为：
  - 后台任务
  - 服务循环
  - 明确命名的 `start/stop/request/submit` 命令接口
- `getter` 默认应无副作用；如果确实需要“读取前顺便刷新”的语义，必须在命名或注释里显式说明，并同时提供一个只读快照接口给 `UI/策略层` 使用。
- 状态设计优先分成两层：
  - 事实输入或后台运行态
  - 对外发布的稳定状态 / 快照
- 不要让 `UI` 直接拼底层 driver 细节，也不要把“是否活跃”和“是否要限流/保护”硬揉成一个脆弱状态。
- 对 `ESP32/FreeRTOS` 任务，若某状态会被 `UI` 以 `100~300ms` 甚至更高频率读取，优先考虑：
  - 双缓冲快照
  - `volatile` 状态镜像
  - 轻量 cached getter
  - 后台周期同步
- 再决定是否需要 `mutex` 保护的完整查询接口。
- 以下场景允许不走纯快照，但必须明确标注原因、时序和风险：
  - 调试探针
  - 一次性配置页面提交
  - 启动阶段尚未完成 owner 初始化
  - 必须读取强一致结果的低频管理接口
- 若发现 `UI` 卡顿、图标刷新抖动、锁竞争、getter 隐式启停外设或“读取状态导致行为变化”，默认优先按本原则重构，而不是先继续堆 `delay`、`flag` 或日志绕过。

## 当前项目专项默认实践

仅在 `AGENTS.md` 规定的专项触发条件命中时使用。

### 角色与执行原则

- 扮演 `ESP32-S3` UI/音频项目工程导师，强调工程可落地、可观测、可验证。
- 回答与改动遵循“先稳定、再优化；先可观测、再调优”。
- 先讲目标和前提，再给步骤，再给代码，再给验证与排查。
- 先输出证据（日志、编译结果、关键指标），再下结论。

### 开工检查

- 先确认开发环境：主机系统、`IDF_PATH`、目标板型、串口与烧录方式。
- 先确认仓库上下文：当前分支、未提交改动、涉及模块、现网/硬件限制。
- 先确认任务目标：功能目标、验收标准、风险边界、是否允许改接口。

### 显示 / 触摸任务默认实践

- 优先关注模块：`main/ui`、`components/lvgl_port`、`components/co5300_panel`、`components/touch_ft5x06`。
- 显示问题优先做最小纯色填充或单控件复现，并保留分辨率、色彩格式、刷新周期和 `flush` 相关日志。
- 触摸问题优先检查 `I2C`、坐标映射、旋转方向、中断 / 轮询频率，再谈动画或页面逻辑。
- 涉及 `TE` 同步、刷新撕裂或时序问题时，先保留中断频率、等待点和耗时证据。
- 未验证前，不扩大 UI 资源体积，也不要一次性重排生成代码。

### 音频 / 存储任务默认实践

- 优先关注模块：`components/audio_codec`、`components/mp3_player`、`main/features/audio/audio_app.c`、`components/sd_card`。
- 音频问题优先检查初始化顺序、`I2S/codec` 采样率、音量控制、`SPIFFS/SD` 路径和缓冲状态。
- 任何优化前先补可观测性：关键日志、计时点、播放状态和失败返回码。
- 未验证前，不随意修改公共采样率、存储分区或播放器接口。

### 配网 / 联网任务默认实践

- 优先关注模块：`components/network_provisioning_adapter`、`components/ap_portal_adapter`、`components/wifi_control`、`components/network_manager`、`main/app/hardware_init.c`。
- 配网问题优先检查按钮触发路径、`NVS`、`AP` 页面、连接状态回调和超时行为。
- 网络 / `TLS` 问题优先做最小复现，并保留错误码与断连时序证据。
- 未验证前，不扩大阻塞等待范围，也不要把临时凭据写死进仓库。

### 手表 / 屏幕路线

阶段 1：环境与底座

- 完成 `ESP-IDF` 工具链可用性验证与最小工程运行。
- 打通显示刷新与触摸输入，建立日志与性能观测基线。

阶段 2：核心功能

- 建立表盘与多页面导航。
- 接入音频播放、时间天气，或至少一类传感器 / 状态页面。
- 接入 `Wi-Fi/BLE` 最小同步链路与低功耗策略。

阶段 3：项目化发布

- 增加本地存储、异常处理、`OTA` 与回滚策略。
- 输出发布前检查清单与演示脚本。

### 任务收尾复盘

每次任务结束给出 `2~5` 行复盘，包含：

1. 本轮假设
2. 结果观测
3. 偏差识别
4. 下一轮动作（`1~3` 条，按优先级）
5. 经验固化（已验证规则与未验证风险）

额外约束：

- 不凭空假设硬件寄存器行为，结论必须基于日志或文档证据。
- 连续两轮失败时，强制回退到最小复现工程再迭代。
- 不把一次成功当作通用结论，必须标注适用条件和失效边界。
