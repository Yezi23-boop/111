---
id: watch-code-directory-reorg-execution-plan
tags: context, plans, architecture, directory-layout, layering, owner, main, services, features, memory-watch, ui-preview
summary: ESP32-S3 手表固件工程代码目录整理执行计划：保留 ESP-IDF 根工程结构，按 owner 收敛 main/services、features、ui、components 边界，并将 Memory Watch 拆成 feature/service/ui 三层。
last_reviewed: 2026-07-07
memory_type: task
scope: task
owners: docs/context/plans/completed/2026-07-07-watch-code-directory-reorg-execution-plan.md, main/CMakeLists.txt, main/services, main/features, main/ui, tools, tests, scripts/context/check_layering.py
triggers: 工程代码目录整理, directory reorg, features services 边界, memory_watch, ui_preview, agent_preview, main/services, layering
evidence_level: design
status: archived
---

# Watch Code Directory Reorg 执行计划

## 目标与全局

- 任务目标：在不推倒 ESP-IDF 工程结构的前提下，整理当前工程代码目录，让 `features`、`services`、`ui`、`app`、`components` 的 owner 边界更清楚，避免后续功能继续堆进错误目录。
- 为什么现在做：当前 `main/services` 已经混入 Memory Watch、power、network、runtime gate、IMU、audio test 等多类 owner；`main/ui/agent_preview` 是 host 预览工具却位于板端 UI 树；`features/weather` 同时承担 feature、service 和 HTTP adapter；后续继续加 Hermes、ESP-DL、BLE、天气、低功耗会放大混层成本。
- 完成后用户会看到什么变化：工程目录更容易理解，新功能可以按 owner 找到落点；source tests 能阻止 UI/generated、service、driver adapter 的越层扩散；迁移过程中固件行为不应变化。

## 总体原则

- 保留根目录 `CMakeLists.txt`、`main/`、`components/`，不整体迁移到 `firmware/`。
- `main` 继续作为一个 ESP-IDF component，不在本计划中拆成多个 IDF component。
- 每一批目录迁移只做路径、include、CMake、tests 更新，不顺手改业务逻辑。
- 先补文档和检查规则，再移动低风险工具目录，再整理固件源码。
- 只提交本任务相关文件，不回滚或混入已有无关脏改动。
- 不修改 `official_chat` 主线。
- 不读取、打印、提交任何真实 key/token；`memory_watch_dev_endpoint_local.h` 只作为路径/ignore 风险处理，不读取内容。

## 层级边界

### 一句话判断

```text
用户为什么要用它？ -> features
系统怎么持续支撑它？ -> services
硬件/协议怎么实现它？ -> components
这块板怎么接线？ -> main/app/board_*
页面怎么显示和收集用户意图？ -> main/ui
```

### `features` 边界

`features` 放用户可见能力、产品语义和用户场景规则。

适合放入 `features`：

- 小游戏、提醒、危险识别产品状态、Memory Watch 产品语义。
- “用户看到/使用这个功能时，它应该表现成什么”的规则。
- feature 内部可以调用 service 的 command/snapshot，但不应直接拥有长期后台 task、HTTP client、TLS、BLE retry 或 raw driver。

当前建议保留或新增：

```text
main/features/alerts/
main/features/danger_detection/
main/features/mini_games/
main/features/memory_watch/
```

### `services` 边界

`services` 放长期运行 owner、后台状态推进、资源生命周期、队列、worker、重试、超时和 snapshot。

适合放入 `services`：

- 网络 ready/probe/legacy shim。
- power、sleep、runtime gate。
- Hermes / Memory Watch 后台通信、录音、上传、WebSocket、sync、inbox、pending reply。
- IMU 长期采样和传感器 snapshot。
- 天气周期 HTTP、缓存和 snapshot。

推荐使用窄目录，避免 `runtime/`、`system/` 变成新杂物桶：

```text
main/services/memory_watch/
main/services/power/
main/services/network/
main/services/sensors/
main/services/weather/
main/services/runtime_gate/
main/services/startup/
main/services/safety/
main/services/audio_diag/
main/services/time/
```

### `ui` 边界

`ui` 放板端 LVGL 页面、controller、view、generated 代码和 UI runtime。

必须保持：

- `main/ui/generated/` 只放 GUI Guider 生成对象、资源和事件桥接。
- `main/ui/custom/` 放手写 controller、view、字体桥接和用户交互。
- UI 只提交用户 intent 或读取 snapshot，不直接执行 HTTP、BLE retry、I2C、driver API 或 Hermes/MiMo/API Server 调用。
- `main/ui/agent_preview` 不是板端 UI，应迁出到 `tools/ui_preview/`。

### `app` / `components` 边界

- `main/app/board_*` 保持板级事实和板级 adapter：GPIO、I2C 地址、安装方向、电源/马达板级语义。
- `components/*` 保持 driver adapter、domain owner、vendor wrapper 或 ESP-IDF component。
- 不把 `board_*` 并入 device driver。
- 不把 `qmi8658c`、`axp2101`、`ds2413` 下沉/上移到 feature 或 UI。
- `components/imu_sensor` 当前是 `imu_service -> imu_sensor -> qmi8658c` 的窄适配层，不能按旧审查卡误迁。

## 目标目录结构

```text
main/
  app/
  ui/
    generated/
    custom/
  features/
    memory_watch/
    alerts/
    danger_detection/
    mini_games/
  services/
    memory_watch/
    power/
    network/
    sensors/
    weather/
    runtime_gate/
    startup/
    safety/
    audio_diag/
    time/
components/
managed_components/
server/
tests/
scripts/
tools/
  ui_preview/
docs/
artifacts/
outputs/
board_logs/
audio_data/
scratch/
tmp/
```

## Memory Watch 三层边界

Memory Watch 不是单纯前台页面，也不是纯后台 service；它必须拆成三层理解。

```text
main/features/memory_watch/
  Memory Watch 产品规则和前台/后台语义

main/services/memory_watch/
  Memory Watch runtime、通信、录音、同步、缓存

main/ui/custom/
  Memory Watch LVGL 页面、controller、view
```

### `features/memory_watch` 负责

- 用户按住说话属于一轮 Hermes 输入。
- 前台 Hermes 页面等待优先；用户离页后 detach 到后台 pending。
- 收到 Hermes reply 后是否生成气泡通知。
- conversation reply 与 Hermes 主动 inbox 的产品区分。
- V1/V2/V2.1 行为规则和用户体验边界。
- 只调用 `services/memory_watch` 的 command/snapshot，不直接拼 HTTP/WS frame。

### `services/memory_watch` 负责

- endpoint/NVS/Kconfig 配置读取。
- 录音、Ogg Opus mux、HTTP multipart、WebSocket、`/sync`、inbox poll、mark-read。
- worker、queue、timeout、retry、pending reply、本地短缓存。
- `watch_endpoint_service` 可以同域，但继续保持中性 facade，不能改成 Hermes-only。
- 不包含 LVGL 对象、UI 文案或页面状态。

### `ui/custom/memory_watch_*` 负责

- Hermes 聊天页。
- 按住说话按钮。
- 用户气泡、助手气泡、状态文本。
- inbox 入口、通知气泡、点开跳转。
- 不直接持有 device token，不直连 Hermes/MiMo，不拼 HTTP/WS。

### 本计划内文件归属

先迁入 `main/services/memory_watch/`：

```text
main/services/memory_watch_service.*
main/services/memory_watch_recorder.*
main/services/memory_watch_ogg_opus_muxer.*
main/services/memory_watch_voice_client.*
main/services/memory_watch_ws_client.*
main/services/watch_endpoint_service.*
main/services/memory_watch_dev_endpoint_local.h
```

后续新增薄 feature facade：

```text
main/features/memory_watch/memory_watch_feature.*
```

保留在 UI：

```text
main/ui/custom/memory_watch_controller.*
main/ui/custom/memory_watch_view.*
```

保留在 server：

```text
server/watch_voice_endpoint/
```

## 范围与非目标

本计划明确要做：

- 新增本执行计划并固定目录 owner 规则。
- 增强 source tests / `check_layering.py`，先让边界可见。
- 将 `main/ui/agent_preview` 迁到 `tools/ui_preview`。
- 将 `main/services` 内明显同域文件迁入窄子目录。
- 为 Memory Watch 建立 feature/service/ui 三层边界。
- 更新 `main/CMakeLists.txt`、tests、docs 和路径引用。

本计划明确不做：

- 不迁移整个 ESP-IDF 工程到 `firmware/`。
- 不拆 `main` 为多个 ESP-IDF component。
- 不修改 `official_chat` 主线。
- 不移动 `managed_components`。
- 不清理或删除 `build/`、`artifacts/`、`outputs/`、`board_logs/`、`audio_data/` 等产物目录；第一版只文档化。
- 不在目录迁移批次顺手重构业务逻辑。
- 不把 `server/watch_voice_endpoint` 放进固件树。
- 不把 `features` 或 `services` 变成大桶式目录。

## 分阶段执行

### 阶段 0：计划落地

要做：

- 新增本 active plan。
- 更新 `docs/context/CHANGELOG.md`。
- 不改工程代码。

验收：

```powershell
uv run python scripts/context/validate_context.py --level standard --q "watch code directory reorg features services memory_watch ui_preview" --brief
git diff --check
```

### 阶段 1：补目录边界检查

要做：

- 增强 `scripts/context/check_layering.py` 或新增 source tests。
- `main/ui/generated` 禁止 include `services/*`、`features/*`、driver adapter、HTTP、Wi-Fi、LCD/touch raw header。
- `main/ui/custom` 禁止直接 include raw driver；只允许通过 service/manager command/snapshot。
- `main/services` 非例外路径禁止 include `qmi8658c.h`、`axp2101.h`、`ds2413.h`、`co5300_panel.h`、`touch_ft5x06.h`、`i2c_manager.h`。
- `tools/ui_preview` 单独分类，不算板端 UI 越界。
- 新增 `i2c_manager_get_bus_handle()` allowlist，只允许 device adapter/component 使用。
- 新增 Memory Watch 目录规则：新增 `memory_watch_*` 必须明确归属 feature/service/ui。

验收：

```powershell
uv run python scripts/context/check_layering.py --verbose
uv run python -m pytest tests -q
git diff --check
```

### 阶段 2：迁移 UI Preview

要做：

```text
main/ui/agent_preview/ -> tools/ui_preview/
```

同步更新：

- `tools/ui_preview/README.md`。
- preview build/capture 脚本默认路径。
- host runner CMake repo root 推导。
- `tests/test_memory_watch_ui_source.py` 等硬编码路径。
- `docs/context/knowledge/project/gui-guider-lvgl-host-preview-workflow.md`。
- `docs/context/knowledge/project/co5300-screen-layout-safe-zone.md` 中 preview 路径说明。
- `AGENTS.md` 与 `docs/context/INDEX.agent.md` 中 preview 路由说明。

不做：

- 不改板端 `main/ui/custom` 和 `main/ui/generated` 逻辑。
- 不把 host mock 头文件放回 `main/ui`。

验收：

```powershell
uv run python scripts/context/check_layering.py --verbose
uv run python -m pytest tests/test_memory_watch_ui_source.py tests/test_ui_chinese_fonts_source.py -q
& "D:\esp32S3\111\tools\ui_preview\scripts\build_apple_watch_s5_preview.ps1"
& "D:\esp32S3\111\tools\ui_preview\scripts\capture_apple_watch_s5_preview.ps1" -OutputPath "D:\esp32S3\111\tools\ui_preview\artifacts\smoke.png"
```

### 阶段 3：建立 Memory Watch 目录边界

要做：

- 新建 `main/features/memory_watch/`。
- 新建 `main/services/memory_watch/`。
- 将 Memory Watch runtime 文件迁到 `main/services/memory_watch/`。
- 可新增很薄的 `memory_watch_feature.*`，只表达产品语义和 UI/service 编排，不承接 HTTP/WS/录音细节。
- 更新 include、`main/CMakeLists.txt`、source tests。

重点 include 规则：

- 外部引用统一使用：

```c
#include "services/memory_watch/memory_watch_service.h"
#include "features/memory_watch/memory_watch_feature.h"
```

- 跨出 Memory Watch domain 的依赖使用稳定路径，例如：

```c
#include "services/network/network_service.h"
#include "services/power/power_policy.h"
```

- 域内文件可以同目录短 include，但不新增 `INCLUDE_DIRS` 来掩盖路径问题。

验收：

```powershell
uv run python -m unittest tests.test_memory_watch_service_source tests.test_memory_watch_voice_client_source tests.test_memory_watch_ws_client_source tests.test_memory_watch_recorder_source tests.test_memory_watch_ogg_opus_muxer_source tests.test_watch_endpoint_service_source tests.test_memory_watch_ui_source
uv run python -m pytest server/watch_voice_endpoint/tests -q
rg -n "memory_watch_dev_endpoint_local|HERMES_API_KEY|MIMO_ASR_API_KEY|API_SERVER_KEY" main tests
idf.py build
```

`rg` 命令只用于确认没有真实密钥扩散；输出不得包含真实 key/token。

### 阶段 4：迁移其他 service 子域

按批次移动，每批独立验证。

#### Power

```text
main/services/power_service.*          -> main/services/power/
main/services/power_policy.*           -> main/services/power/
main/services/sleep_coordinator.*      -> main/services/power/
main/services/wakeup_evidence_service.* -> main/services/power/
```

#### Network

```text
main/services/network_service.* -> main/services/network/
```

说明：`network_service` 继续是 service-ready/probe/legacy shim；新产品网络动作仍优先走 `components/network_manager`。

#### Sensors

```text
main/services/imu_service.* -> main/services/sensors/
```

说明：当前 IMU 链路以 `imu_service -> imu_sensor -> qmi8658c` 为准，不按旧 `imu_service -> board_imu -> qmi8658c` 文档误迁。`board_imu` 仍只保留板级事实。

#### Runtime Gate

```text
main/services/foreground_runtime_gate.*          -> main/services/runtime_gate/
main/services/background_https_gate.*            -> main/services/runtime_gate/
main/services/runtime_resource_gate_board_test.* -> main/services/runtime_gate/
```

说明：`runtime_gate` 是薄 gate，不是大 ResourceManager。

#### Startup / Time / Safety / Audio Diagnostic

```text
main/services/startup_readiness.*         -> main/services/startup/
main/services/system_time_service.*       -> main/services/time/
main/services/safety_monitor_session.*    -> main/services/safety/
main/services/background_service_manager.* -> main/services/safety/
main/services/audio_mic_test_service.*    -> main/services/audio_diag/
```

说明：如果某个子目录只有 1 个文件且移动会制造大量 churn，可以在该批次保持暂不移动，并在本计划记录原因。

每批验收：

```powershell
uv run python scripts/context/check_layering.py --verbose
uv run python -m pytest tests -q
git diff --check
idf.py build
```

### 阶段 5：天气模块收敛

当前问题：

- `main/features/weather/hptts.c` 同时持有 API/location/URL、`esp_http_client`、JSON 解析和任务逻辑。
- 天气展示是 feature，但天气获取、缓存和 snapshot 是 service。

目标结构：

```text
main/services/weather/weather_service.*
main/services/weather/weather_http_client.*
```

UI 和页面只读取 weather snapshot，不直接执行 HTTP。

不做：

- 不在本阶段引入复杂配置系统。
- 不把天气 HTTP client 做成全局网络 worker。
- 不把天气 API key 或 location 扩散到 UI。

验收：

```powershell
uv run python -m pytest tests/test_time_weather_source.py -q
uv run python scripts/context/check_layering.py --verbose
idf.py build
```

### 阶段 6：文档地图同步与产物目录治理

要做：

- 更新 `docs/context/knowledge/project/main-directory-map.md`。
- 更新 `docs/context/knowledge/project/layering-boundary-map.md` 中实际目录映射。
- 更新 `docs/context/knowledge/project/watch-interface-config-layering-review.md` 中已完成项或 superseded 说明。
- 明确以下目录第一版只文档化，不移动：

```text
build/
artifacts/
outputs/
board_logs/
audio_data/
sdcard/
scratch/
tmp/
components/esp_lcd_co5300/build/
components/esp_lcd_co5300/.vscode/
```

验收：

```powershell
uv run python scripts/context/validate_context.py --level standard --q "watch code directory reorg main services features tools ui_preview" --brief
git diff --check
```

## 进度

- `[x]` 阶段 0：计划落地。
- `[x]` 阶段 1：补目录边界检查。
- `[x]` 阶段 2：迁移 UI Preview。
- `[x]` 阶段 3：建立 Memory Watch 目录边界。
- `[x]` 阶段 4：迁移其他 service 子域。
- `[x]` 阶段 5：天气模块收敛。
- `[x]` 阶段 6：文档地图同步与产物目录治理。

## 决策记录

- 2026-07-07：保留 ESP-IDF 根工程结构，不迁移到 `firmware/`。
- 2026-07-07：`main` 继续作为单 ESP-IDF component，目录整理优先通过 source tests 和 CMake 路径收敛边界。
- 2026-07-07：`features` 定义为用户可见能力/产品语义，`services` 定义为后台生命周期和运行时 owner。
- 2026-07-07：Memory Watch 拆成 feature/service/ui 三层，而不是整体放入 `services`。
- 2026-07-07：`main/ui/agent_preview` 迁到 `tools/ui_preview`，host preview 不再作为板端 UI 子目录。
- 2026-07-07：不使用过宽 `main/services/runtime/`、`main/services/system/` 大桶；优先窄 owner 目录。
- 2026-07-07：设备/板层边界保持：`main/app/board_*` 是板级事实，`components/*` 是 driver adapter/domain owner/vendor wrapper。
- 2026-07-14：不新增只转发一层的 `memory_watch_feature`；先用 `features/memory_watch/README.md` 固定产品语义边界。
- 2026-07-14：天气 HTTP client 改为返回值类型 DTO，由 weather service 独占 mutex 和 snapshot，保持任务栈、优先级、刷新周期和启动顺序不变。

## 子代理审查结论

- Framework review：整体策略成立，但 `runtime` / `system` 这类目录名太宽，容易变成新杂物桶；建议使用 `runtime_gate`、`startup` 等窄 owner 名。
- UI/LVGL review：`main/ui/agent_preview -> tools/ui_preview` 是低风险高收益，当前 `check_layering.py` 的 warning 主要来自 host preview mock。
- Hermes/Memory Watch review：同域迁入 `main/services/memory_watch/` 合理，但它只是 runtime 组织收敛；Memory Watch 产品语义应由 `features/memory_watch` 承接，server 侧仍留顶层 `server/watch_voice_endpoint`。
- Device/board review：当前 IMU 实现已是 `imu_service -> imu_sensor -> qmi8658c`，不要按旧审查卡误迁；`main/app/board_*`、`components/qmi8658c`、`components/axp2101`、`components/ds2413`、`i2c_manager` raw handle 都要通过 source tests 管住扩散。

## 意外与发现

- 设备/板层审查（已蒸馏进 `hardware-capability-gap-map.md`）的 IMU 描述曾与当前 active plan/代码出现漂移：当前代码方向为 `imu_service -> imu_sensor -> qmi8658c`，目录整理必须以当前 active plan 和 source tests 为准。
- `main/ui/agent_preview` 的 mock 导致 layering warning 噪声；迁出后可让 UI/generated 检查更严格。
- `memory_watch_dev_endpoint_local.h` 是敏感本地开发入口；迁移时不得读取或输出内容。
- `watch_endpoint_service` 虽与 Memory Watch endpoint 配置同域，但语义应保持中性，避免 danger alert 等未来非 Hermes 能力被迫依赖 Hermes 命名。

## 验证与验收

阶段 2（2026-07-14）：

- `uv run python scripts/context/check_layering.py --verbose`：退出码 0，`tools/ui_preview` mock 无 UI 越界 warning；剩余 2 条 warning 来自既有 `main/ui/generated/widgets_init.c`。
- `uv run python -m pytest tests/test_memory_watch_ui_source.py tests/test_ui_chinese_fonts_source.py -q`：`19 passed`。
- `tools/ui_preview/scripts/build_apple_watch_s5_preview.ps1`：host runner 配置、编译和链接通过。
- `tools/ui_preview/scripts/capture_apple_watch_s5_preview.ps1 -OutputPath tools/ui_preview/artifacts/smoke.png`：截图生成成功，尺寸 410×502。

阶段 1、3-6（2026-07-14）：

- `uv run python scripts/context/check_layering.py --verbose`：`warning_count=0`，保留 2 个显式 known exception。
- 目录迁移聚焦 source tests：`79 passed`；server watch endpoint：`174 passed`。
- 全量 source tests：`423 passed, 7 failed`；7 个失败均为任务前已有的非目录整理断言漂移，详见本次 run。
- ESP-IDF 5.5.3 `fullclean + build`：通过，subagent 修复后最终 `111.bin=0xac5f30`，app 分区剩余 23%。
- host preview 重新构建及 `directory-reorg-smoke.png` 截图通过。
- 两名独立 subagent 复查无 P0；weather DTO reentrancy、mutex 分配失败保护、本地敏感 header ignore 和现行 context 旧路径均已修复。

每个代码迁移批次的基础验证：

```powershell
uv run python scripts/context/check_layering.py --verbose
uv run python -m pytest tests -q
git diff --check
. D:\esp-idf\v5.5.3\esp-idf\export.ps1
idf.py build
```

只改 context 文档时：

```powershell
uv run python scripts/context/validate_context.py --level standard --q "watch code directory reorg features services memory_watch ui_preview" --brief
git diff --check
```

路径迁移批次的最低验收：

- `main/CMakeLists.txt` 中所有 moved source 路径正确。
- `tests/main_paths.py` 或相关 source tests 路径已同步。
- `rg -n "main/ui/agent_preview|services/memory_watch_service|services/memory_watch_voice_client"` 不再返回应迁移的新引用；历史 runs 可保留旧路径证据。
- `check_layering.py --verbose` 不因 `tools/ui_preview` mock 报 UI 越界。
- `idf.py build` 通过。

## 幂等与恢复

- 每个阶段单独提交，失败时只回退该阶段移动。
- 如果 UI preview 迁移失败，恢复 `main/ui/agent_preview` 并先只更新 `check_layering.py` 跳过 preview mock。
- 如果 Memory Watch 迁移导致 include 大面积失败，先只迁 `.c/.cc`，保留 public `.h` 兼容路径，再分批更新 include。
- 如果 service 子域迁移导致 tests 过多失败，优先更新 `tests/main_paths.py` 中心路径，不逐个散改。
- 如果 weather 收敛风险过高，先保留 `main/features/weather`，只写文档标记为后续待迁。
- 如果构建失败，优先检查 `main/CMakeLists.txt`、include path 和 ESP-IDF `INCLUDE_DIRS`，不要通过新增过宽 include 目录掩盖越界。

## 下一步

- 目录整理阶段已全部完成；提交时只纳入本任务相关路径，继续排除用户既有 `sdkconfig` 改动。
- 后续单独处理全量 source tests 的 7 个既有漂移，不在本目录整理任务中顺手修改。
