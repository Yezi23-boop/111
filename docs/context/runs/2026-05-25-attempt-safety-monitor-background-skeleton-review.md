---
id: attempt-2026-05-25-safety-monitor-background-skeleton-review
tags: watch, safety-monitor, danger-detection, background-service, framework-review, subagent
summary: 复查 Safety Monitor 后台服务骨架，确认当前 owner 链路成立，并把后续 agent 写代码必须沿用的骨架回写到 runtime owner contract。
last_reviewed: 2026-05-25
memory_type: episodic
scope: task
status: active
result: success
owners: docs/context/knowledge/project/runtime-owner-contract.md, docs/context/plans/completed/2026-05-25-safety-monitor-background-completion-plan.md, docs/context/knowledge/project/hearing-assist-danger-alert-firmware-mapping.md, main/services/background_service_manager.c, main/services/safety_monitor_session.c, main/features/danger_detection/danger_detection_service.c, main/features/alerts/app_alert_manager.c, main/ui/custom/danger_detection_controller.c, components/espdl_inference, components/audio_codec
triggers: Safety Monitor 后台骨架 background_service_manager safety_monitor_session danger_detection_service 后续 agent subagent 复查
evidence_level: reviewed
record_reasons: owner-architecture, framework-constraint, subagent-review
force_reason:
---

# Attempt Log: safety-monitor-background-skeleton-review

## 背景

- 本次要验证什么：当前危险识别后台服务骨架是否已经搭好，是否可以作为后续 agent 写代码的固定合同。
- 对应任务或计划：`docs/context/plans/completed/2026-05-25-safety-monitor-background-completion-plan.md`
- 结果状态：success
- 长期记录理由：owner-architecture, framework-constraint, subagent-review

## 环境

- 分支/工作区状态：工作区已有未提交改动，本次记录只写 context 文档。
- 设备/串口/板型：本轮为源码和上下文复查，没有新增板端日志。
- 关键前置条件：Safety Monitor 已由页面功能提升为后台服务链路，且 AI 前台音频与麦克风 session 仲裁已接入。

## 操作

- 修改过的文件或 owner：
- `docs/context/knowledge/project/runtime-owner-contract.md`
- `docs/context/plans/completed/2026-05-25-safety-monitor-background-completion-plan.md`
- `docs/context/knowledge/project/hearing-assist-danger-alert-firmware-mapping.md`
- `docs/context/CHANGELOG.md`
- `main/services/background_service_manager.[ch]`
- `main/ui/custom/danger_detection_controller.c`
- `tests/test_safety_monitor_session_source.py`
- `tests/test_danger_detection_controller_source.py`
- `components/espdl_inference/espdl_audio_runtime.cpp`
- `main/features/danger_detection/danger_detection_service.c`
- `tests/test_espdl_single_model_runtime_source.py`
- `tests/test_danger_detection_service_source.py`
- `main/features/alerts/app_alert_manager.c`
- `main/features/alerts/audio_alert_player.c`
- `tests/test_audio_codec_port_source.py`
- 执行的命令或动作：
- 运行 light context 检索：`uv run python scripts/context/validate_context.py --level light --q "Safety Monitor 后台 骨架 background_service_manager safety_monitor_session 后续 agent" --brief`
- 收到 `framework_reviewer` 复查结论：当前调用方向符合 `UI -> background_service_manager -> safety_monitor_session -> danger_detection_service -> espdl_inference/app_alert_manager`，未发现总管家化或 owner 偏离。
- 收到 `embedded_safety_reviewer` 复查结论：未发现已坐实 P0；存在 ESP-DL stop 超时、alert manager 共享状态、runtime 高频动态分配等 P1/P2 风险，属于后续质量 gate，不阻断当前骨架确认。
- 将 Safety Monitor 当前正式骨架回写到 `runtime-owner-contract.md`，明确后续 agent 不得把生命周期塞回 UI、不得新增大而全 manager。
- 追加实现 Gate 2：`background_service_manager_snapshot_t` 发布 `danger_should_run` 与 `danger_block_reason`，并在目标态或阻塞原因变化时打印 `background_target_change`。
- 危险识别页状态文案改为优先消费 `danger_block_reason`，不再重复组合 `danger_enabled_by_user / danger_allowed_by_policy / danger_blocked_by_foreground_audio`。
- 修复嵌入式复查指出的 P1：ESP-DL stop timeout 后，runtime 支持稍后补清 codec/model 资源；`danger_detection_service_stop()` 不再在底层 stop 失败时清掉 runtime/callback owner 状态；`safety_monitor_session` stop 失败时不发布“已停止”。
- 修复提醒层共享状态：`app_alert_manager` 和 `audio_alert_player` 对跨任务共享字段加临界区，显示和音频调用保持在锁外；`clear()` hide 成功后提交 inactive，并用 generation 避免过期 raise/show 覆盖新状态。
- 修复 stale callback 告警竞态：ESP-DL result callback 在真正 `app_alert_manager_raise/clear` 前二次检查 service 状态，stop 成功后再次兜底 clear，避免 STOPPING 后补出过期提醒。
- 收敛 ESP-DL 推理热路径分配：`pcm_float` 不再每个推理窗口构造，而是在 runtime 任务启动时一次性分配并复用。

## 观测

- 关键日志/证据：
- `background_service_manager`：保存用户开关，读取 power budget 与 audio session snapshot，合成 Safety Monitor `should_run`。
- `safety_monitor_session`：负责 `danger_detection_service_start/stop`、ERROR 恢复、5s 退避和运行确认。
- `danger_detection_service`：负责 `MONITORING / SUSPICIOUS / ALERTING / COOLDOWN` 风险状态机。
- `danger_detection_controller`：只读快照、写后台开关，页面进入不自动 start，退出不 stop。
- `uv run python -m pytest tests/test_safety_monitor_session_source.py tests/test_danger_detection_controller_source.py`：5 passed。
- `uv run python -m pytest tests/test_safety_monitor_session_source.py tests/test_danger_detection_controller_source.py tests/test_audio_codec_port_source.py tests/test_power_integration_source.py tests/test_danger_detection_service_source.py`：24 passed。
- `uv run python scripts/context/validate_context.py --level standard --q "Safety Monitor 后台服务 danger_should_run danger_block_reason background_service_manager" --brief`：错误 0，警告 0。
- `idf.py build`：通过，`111.bin` 大小 `0x8d7280`，factory 剩余 `0x128d80`（12%）。
- `uv run python -m pytest tests/test_espdl_single_model_runtime_source.py tests/test_danger_detection_service_source.py tests/test_safety_monitor_session_source.py tests/test_danger_detection_controller_source.py tests/test_audio_codec_port_source.py tests/test_power_integration_source.py`：29 passed。
- `uv run python scripts/context/validate_context.py --level standard --q "Safety Monitor espdl_audio_runtime_stop timeout danger_detection_service_stop" --brief`：错误 0，警告 0。
- P1 修复后 `idf.py build`：通过，`111.bin` 大小 `0x8d7390`，factory 剩余 `0x128c70`（12%）。
- `uv run python -m pytest tests/test_espdl_single_model_runtime_source.py tests/test_audio_codec_port_source.py tests/test_danger_detection_service_source.py tests/test_safety_monitor_session_source.py`：20 passed。
- 最终本地验证：`uv run python -m pytest tests/test_espdl_single_model_runtime_source.py tests/test_danger_detection_service_source.py tests/test_safety_monitor_session_source.py tests/test_danger_detection_controller_source.py tests/test_audio_codec_port_source.py tests/test_power_integration_source.py`：30 passed。
- 最终 context standard：`uv run python scripts/context/validate_context.py --level standard --q "Safety Monitor P1 app_alert_manager audio_alert_player pcm_float stop timeout" --brief`：错误 0，警告 0。
- 复查 P1 事务一致性修复后重新验证：30 项 source tests 通过，context standard 错误 0 / 警告 0。
- 最终 `idf.py build`：通过，`111.bin` 大小 `0x8d7650`，factory 剩余 `0x1289b0`（12%）。
- stale callback 修复后重新验证：31 项 source tests 通过；context standard 错误 0 / 警告 0。
- stale callback 修复后最终 `idf.py build`：通过，`111.bin` 大小 `0x8d76c0`，factory 剩余 `0x128940`（12%）。
- 最终本地验证：`uv run python -m pytest tests/test_audio_codec_port_source.py tests/test_espdl_single_model_runtime_source.py tests/test_danger_detection_service_source.py tests/test_safety_monitor_session_source.py tests/test_danger_detection_controller_source.py tests/test_power_integration_source.py tests/test_official_chat_service_source.py`：38 passed。
- 最终 context standard：`uv run python scripts/context/validate_context.py --level standard --q "Safety Monitor final background service runtime owner contract danger_detection" --brief`：错误 0，警告 0。
- `git diff --check`：无 whitespace error；仅输出 Windows CRLF 转换提示。
- 最终 `idf.py build`：通过，`111.bin` 大小 `0x8d76c0`，factory 剩余 `0x128940`（12%）。
- 最终 framework subagent 复查：无 P0/P1，结论为“框架可验收”；P2 是 runtime owner contract、completion plan 和 attempt log 仍未纳入版本控制，若要约束 clean checkout 需要提交。
- 板端验收准备：`Get-CimInstance Win32_SerialPort` 当前只发现 `COM1`，未发现 ESP32-S3 开发板串口，因此本轮不能补真实串口日志。
- 新增板端验收脚本：`scripts/board/validate_safety_monitor_board.ps1` 支持 `-ListPorts`、自动选择非 `COM1` 单一候选、可选 `-NoFlash`、限时 capture `idf.py -p <port> flash monitor` 日志，并扫描 Safety Monitor 启动、3s 推理、AI 前台暂停/恢复、mic blocked 和 `ALERTING` 关键模式。
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\board\validate_safety_monitor_board.ps1 -ListPorts`：通过，当前仅列出 `COM1`。
- `uv run python -m pytest tests/test_safety_monitor_board_validation_script.py tests/test_audio_codec_port_source.py tests/test_espdl_single_model_runtime_source.py tests/test_danger_detection_service_source.py tests/test_safety_monitor_session_source.py tests/test_danger_detection_controller_source.py tests/test_power_integration_source.py tests/test_official_chat_service_source.py`：40 passed。
- 与预期不一致的点：
- 无框架阻断问题；源码、上下文和构建验证均通过。当前剩余缺口是设备未连接导致的板端长期运行日志。

## 结论

- 本次可以确认的事实：Safety Monitor 后台服务骨架已经搭好，可以作为后续 agent 写代码的正式框架边界。
- 后续 agent 应沿用六段 owner：`danger_detection_controller -> background_service_manager -> safety_monitor_session -> danger_detection_service -> espdl_inference -> app_alert_manager`。
- 当前不需要新增 `ResourceManager`、`session_router`、默认 `ui_manager` 或其他总管家。
- 后台服务可观测性第一层已补齐：manager snapshot 能直接说明 Safety Monitor 目标态和主阻塞原因。
- ESP-DL stop timeout 的服务状态一致性已补第一层，避免暂停/恢复链路误判 Safety Monitor 已释放资源。
- 嵌入式复查指出的源码级 P1 已处理到当前最小可验证层：stop timeout 状态一致性、提醒层共享状态保护、ESP-DL 推理热路径反复分配。
- 最终复查指出的 stale ESP-DL callback 重新触发提醒风险已做最小修复。
- 最终 framework subagent 复查确认框架可验收，无 P0/P1 owner 偏离。
- 板端验收脚本已补齐，后续设备一旦枚举为非 `COM1` 串口即可直接抓取验收日志。

## 未验证风险

- 下一轮仍需补证据的边界：
- 仍需要板端长时间运行证据确认 internal/PSRAM free、minimum free、task stack high water、多次 stop/start 后 session snapshot，以及 P0 提醒的真实可感知效果。
- 当前机器未枚举到 ESP32-S3 串口，无法执行本轮真实烧录/monitor 验收。
