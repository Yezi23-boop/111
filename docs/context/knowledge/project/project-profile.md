---
id: project-profile
tags: project, profile, entrypoint, architecture, network, ui
summary: 面向 agent 首读的仓库画像，压缩记录正式启动链路、当前真实 owner、已退场旧链路和最重要的上下文入口。
last_reviewed: 2026-05-05
memory_type: semantic
scope: repo
owners: AGENTS.md, docs/context/knowledge/project/project-profile.md
triggers: profile, entrypoint, repo-state, startup, owner, 当前正式启动链路, 当前真实 owner, 入口, 项目画像, 仓库画像
evidence_level: observed
---

# 项目画像

## 这个仓库现在是什么

- 这是一个 `ESP32-S3 + ESP-IDF` 固件仓库，主线能力覆盖 `LVGL UI`、显示/触摸、音频播放、`Wi-Fi/BLE` 配网、`official_chat`、电源观测与危险声音识别。
- 正式应用入口在 `main/app/app_main.c`。
- 当前协作风格是：先找真实 owner 和上下文证据，再做最小可验证改动。

## 当前正式启动链路

- `app_main()` 先执行 `main/app/hardware_init.c:hardware_init()`。
- 硬件 ready 后启动 `main/ui/lvgl_task.c` 的 UI 任务。
- UI 起来后再由后台服务继续推进：
  - `main/services/power_service.c`
  - `main/services/network_service.c`
  - `main/services/official_chat_service.c`
- 当前正式模型已经切到“先起 UI，联网后台继续”；不要再按“联网成功后再进 UI”的旧路径理解仓库。

## 当前真实 owner

- 显示/触摸：`components/lvgl_port`、`components/co5300_panel`、`components/touch_ft5x06`、`main/ui`
- 音频：`components/audio_codec`、`components/mp3_player`、`main/features/audio`
- 联网/配网：`components/network_manager`、`components/network_provisioning_adapter`、`components/ap_portal_adapter`、`components/wifi_control`、`main/services/network_service.c`
- 电源：`components/axp2101`、`main/app/board_power.c`、`main/services/power_service.c`
- 危险识别：`components/espdl_inference`、`main/features/danger_detection`

## 当前已退场或不要再当正式主线的旧链路

- 旧 `components/wifi_provision` 已退场；不要再把它当正式 owner。
- “联网成功后再进入 UI”的启动模型已退场。
- 历史实验入口和历史方案卡可能仍保留作背景资料，但默认不代表当前正式实现。

## 遇到问题时先读哪里

- 全局骨架按需读：仅当 query/brief pack 命中，或确实需要完整仓库骨架时，再打开 `docs/context/knowledge/project/repo-overview.md`
- 当前目录和 owner 映射先读：`docs/context/knowledge/project/main-directory-map.md`
- 联网/配网主线先读：`docs/context/knowledge/project/network-provisioning-custom-upper-architecture.md`
- UI/状态读取边界先读：`docs/context/knowledge/project/agent-operational-rules.md`
- 通用嵌入式 C/C++ 约束先读：`docs/context/knowledge/project/embedded-c-cpp-engineering-rules.md`

## 任务推进的默认姿势

- 普通任务默认先运行 `uv run python scripts/context/validate_context.py --level light --q "<任务关键词/文件/错误码>" --brief`
- `light` 会先查历史尝试，再查稳定知识，并生成 brief pack；不要为普通代码任务默认跑完整四件套。
- `query.py` / `pack_context.py` 只作为实现细节或高级调试路径，不作为普通任务的默认入口。
- 只改 context 文档时用 `--level standard`；改入口/检索基准时用 `--level routing`；改 `scripts/context` 或记忆机制时才用 `--level full`。
- 复杂任务先在 `docs/context/plans/active/` 落计划
- 有长期复用价值的联调、日志、板测证据，以及 agent 已经做过且不应重复的修改/尝试，按 `context-memory-policy.md` 写入 `docs/context/runs/`
