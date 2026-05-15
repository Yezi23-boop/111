---
id: attempt-2026-05-13-safety-monitor-microphone-arbitration
tags: watch, resource-management, audio, safety-monitor, danger-detection
summary: safety-monitor-microphone-arbitration；结果：partial。
last_reviewed: 2026-05-13
memory_type: episodic
scope: task
status: active
result: partial
owners: main/services/background_service_manager.[ch], main/features/audio/audio_app.c, components/audio_codec, main/ui/custom/danger_detection_controller.c, tests/test_audio_codec_port_source.py, tests/test_safety_monitor_session_source.py, tests/test_danger_detection_controller_source.py, docs/context/plans/completed/2026-05-12-watch-resource-framework-plan.md
triggers: foreground_audio_active audio_recorder Safety Monitor resource_blocked 麦克风 仲裁
evidence_level: verified
record_reasons: owner-architecture, framework-constraint, evidence
force_reason:
---

# Attempt Log: safety-monitor-microphone-arbitration

## 背景

- 本次要验证什么：按手表整体资源框架继续执行危险识别第一阶段 item 4，让 P1 前台录音占用麦克风时，P2 Safety Monitor 暂停或进入可解释阻塞状态。
- 对应任务或计划：watch-resource-framework-plan-20260512
- 结果状态：partial
- 长期记录理由：owner-architecture, framework-constraint, evidence

## 操作

- 修改过的文件或 owner：
- `main/services/background_service_manager.[ch]`
- `main/features/audio/audio_app.c`
- `components/audio_codec/include/audio_codec.h`
- `components/audio_codec/audio_codec.c`
- `main/ui/custom/danger_detection_controller.c`
- `tests/test_audio_codec_port_source.py`
- `tests/test_safety_monitor_session_source.py`
- `tests/test_danger_detection_controller_source.py`
- `docs/context/plans/completed/2026-05-12-watch-resource-framework-plan.md`
- 执行的命令或动作：
- 复查框架边界：`audio_codec` 继续作为麦克风 input session owner；`background_service_manager` 只计算用户开关、policy 许可和前台音频阻塞；`safety_monitor_session` 继续负责危险识别 runtime 生命周期；UI 只读 manager/service 快照。
- 为 `background_service_manager` 增加 `foreground_audio_active` 状态和 `danger_blocked_by_foreground_audio` 快照字段；`should_run` 变为 `enabled_by_user && allowed_by_policy && !foreground_audio_active`。
- 为 `audio_codec` 增加 `AUDIO_CODEC_OWNER_AUDIO_RECORDER`，让本地录音以明确 owner 申请和释放 input session。
- 在 `audio_app` 录音任务中先声明前台录音活动，暂停 Safety Monitor 后再申请 input session；录音失败或结束都释放 input session 并清除前台活动状态。
- 危险识别页在用户已开启且前台录音占用麦克风时显示“资源占用，暂时等待”。
- 继续加深 owner 可观测性：`audio_codec` 增加只读 session snapshot 与 owner 文本接口，`background_service_manager` 同时读取显式前台音频标志和真实 input owner，并在阻塞状态变化时输出 `resource_blocked_change: resource=mic`。

## 观测

- 关键日志/证据：
- `uv run python -m pytest tests/test_audio_codec_port_source.py tests/test_safety_monitor_session_source.py tests/test_danger_detection_controller_source.py tests/test_power_integration_source.py`：17 passed。
- `idf.py build` 通过，`111.bin` 大小 `0x8d5050`，factory 分区剩余 `0x12afb0`（12%）。
- 加深 owner snapshot 后再次执行同一组 source tests：17 passed。
- 加深 owner snapshot 后再次执行 `idf.py build` 通过，`111.bin` 大小 `0x8d51f0`，factory 分区剩余 `0x12ae10`（12%）。
- 与预期不一致的点：
- 未记录。

## 结论

- 本次可以确认的事实：源码层已经按资源框架建立前台录音让路路径；危险识别页不直接抢麦克风，后台 manager 可解释地发布 foreground audio 阻塞状态，并能从 `audio_codec` 真实 input owner 快照读取麦克风占用者。
- 仍然不能确认的事实：
- 尚未上板手动验证“打开安全监听后开始本地录音”的完整日志顺序和录音结束后的 Safety Monitor 恢复。

## 未验证风险

- 下一轮仍需补证据的边界：
- 打开 `安全监听` 后启动本地录音，确认 `foreground_audio_active=1`、Safety Monitor stop/input session release、本地录音 acquire、录音结束 release、`foreground_audio_active=0`、Safety Monitor 恢复的板端日志顺序。
- 若后续语音助手也使用麦克风，需要同样声明 P1 foreground audio 活动，或进一步把阻塞判断收敛到 `audio_codec` 的当前 input owner 快照，避免每个前台音频功能重复接线。
