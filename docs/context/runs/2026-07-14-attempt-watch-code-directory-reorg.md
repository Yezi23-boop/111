---
id: attempt-watch-code-directory-reorg
tags: context, runs, directory-layout, layering, owner, memory-watch, weather, ui-preview, freertos
summary: 记录手表固件工程代码目录按 owner 整理、天气 service 拆分、generated UI 解耦及完整构建验证证据。
last_reviewed: 2026-07-14
memory_type: run
scope: project
owners: main/CMakeLists.txt, main/services, main/features, main/ui, tools/ui_preview, scripts/context/check_layering.py
triggers: watch directory reorg, services owner folders, memory watch directory, weather service, ui preview
evidence_level: observed
status: active
---

# Watch Code Directory Reorg 执行记录

## 目标与边界

- 保留 ESP-IDF 根工程和单一 `main` component，不迁移到 `firmware/`。
- 目录迁移默认只改路径、include、CMake、tests 和文档，不修改 `official_chat` 业务语义。
- `sdkconfig` 的既有未提交改动保持原样；敏感本地 endpoint 文件只移动路径，不读取或输出内容。

## 已完成

- `main/ui/agent_preview` 迁到 `tools/ui_preview`，host mock 不再污染板端 UI 分层检查。
- `main/services` 按 `memory_watch/power/network/sensors/runtime_gate/startup/time/weather/safety/audio_diag` 窄 owner 子目录整理。
- Memory Watch runtime 收敛到 `services/memory_watch`；`features/memory_watch` 只记录产品语义边界，未新增空转发 facade。
- 主屏电量和天气刷新迁到 `ui/custom/main_screen_runtime`；`ui/generated` 不再直接 include feature/service。
- 天气拆为 `weather_service` 和 `weather_http_client`。service 持有 FreeRTOS task、mutex、snapshot 和刷新策略；client 获取 HTTPS gate 并返回值类型 DTO，不反向写 service。
- `check_layering.py` 开始扫描 generated UI，增加 raw driver、I2C bus handle allowlist 和 Memory Watch 文件归属检查。

## 验证证据

- `uv run python scripts/context/check_layering.py --verbose`：`warning_count=0`，保留 2 个已记录例外。
- 目录迁移聚焦 source tests：`79 passed`。
- 全量 source tests：`423 passed, 7 failed`。失败来自本轮前已有的显示队列深度、危险页坐标、fall 模型契约、IMU 阈值、分区表与字体断言漂移；未在目录整理中修改。
- `server/watch_voice_endpoint/.venv/Scripts/python.exe -m pytest server/watch_voice_endpoint/tests -q`：`174 passed`。
- host preview build 和 410x502 截图通过。
- ESP-IDF 5.5.3 执行 `fullclean + build` 通过；subagent 复查修复后最终 `111.bin=0xac5f30`，最小 app partition 剩余 `0x33a0d0`（23%）。
- `git diff --check` 无 whitespace error。

## Subagent 复查收口

- 代码 reviewer 未发现 P0；指出 weather DTO 全局临时指针不可重入和 mutex 分配失败缺少保护，已分别改为 `esp_http_client.user_data` 和显式空指针检查。
- 边界 reviewer 发现本地 endpoint header 的 ignore 仍指向旧目录，已更新 `.gitignore`；该文件保持 ignored，不进入提交。
- reviewer 发现 3 个现行 context 路由仍指向旧 service 路径，已同步更新并通过 routing 校验。
- `sdkconfig` 是任务前已有无关改动，本轮不修改、不暂存、不提交。

## 失败路线与证伪

- 根 `uv` 环境直接运行 server tests 缺少 `fastapi/httpx`；改用 server 自带 `.venv` 后全部通过。这不是 server 代码错误。
- 全量 source tests 的 7 个失败不涉及本轮 moved path；聚焦路径测试、分层检查和完整固件 build 均通过，故不在本任务中修改无关实现或断言。

## 后续边界

- `official_chat_service` 与 `fall_detection_service` 暂保留在 services 根目录，后续只有出现明确 owner 收敛收益时再单独迁移。
- `wakeup_evidence_service` 直连 AXP2101 仍是显式 known exception；本轮只迁目录，不改变其 PMIC owner 合同。
- 目录整理不替代真机行为回归；本次无运行时语义变化，不执行 app-flash/monitor。
