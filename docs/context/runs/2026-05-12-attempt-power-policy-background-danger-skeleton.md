---
id: attempt-2026-05-12-power-policy-background-danger-skeleton
tags: watch, power_policy, background_service_manager, danger-detection
summary: power-policy-background-danger-skeleton；结果：success。
last_reviewed: 2026-05-13
memory_type: episodic
scope: task
status: active
result: success
owners: main/services/power_policy.c, main/services/power_policy.h, main/services/background_service_manager.c, main/services/background_service_manager.h, main/services/safety_monitor_session.c, main/services/safety_monitor_session.h, main/app/app_main.c, main/ui/custom/danger_detection_controller.c, docs/context/plans/completed/2026-05-12-watch-resource-framework-plan.md
triggers: power_policy background_service_manager 危险识别 后台系统能力
evidence_level: observed
record_reasons: owner-architecture, framework-constraint, evidence
force_reason: 
---

# Attempt Log: power-policy-background-danger-skeleton

## 背景

- 本次要验证什么：落地 power_policy + background_service_manager 薄骨架，把危险识别从专页生命周期提升为后台系统能力
- 对应任务或计划：watch-resource-framework-plan-20260512
- 结果状态：success
- 长期记录理由：owner-architecture, framework-constraint, evidence

## 环境

- 分支/工作区状态：未记录
- 设备/串口/板型：未涉及或未记录
- 关键前置条件：未记录

## 操作

- 修改过的文件或 owner：
- main/services/power_policy.[ch]
- main/services/background_service_manager.[ch]
- main/services/safety_monitor_session.[ch]
- main/CMakeLists.txt
- main/app/app_main.c
- main/features/alerts/app_alert_manager.c
- main/ui/custom/danger_detection_controller.c
- main/ui/custom/danger_detection_view.[ch]
- main/ui/lvgl_task.c
- docs/context/plans/completed/2026-05-12-apple-watch-like-boot-flow-plan.md
- docs/context/plans/completed/2026-05-12-watch-resource-framework-plan.md
- 执行的命令或动作：
- 使用 grill-with-docs 对齐 owner：power_policy 只发布预算，background_service_manager 管用户开关与策略许可，danger_detection_service 保持状态机 owner
- 复查后修复 runtime FAILED 恢复路径：后台管理器先 stop 清理错误态，再重新 start，并以 start 后快照确认是否真的 running
- 复查后收窄页面入口：manager task 未启动时只记录用户开关，不拉起无人托管的后台危险识别
- 通过 D:\esp-idf\v5.5.3\esp-idf\export.ps1 后运行 idf.py build
- 烧录 COM3 并抓取首轮 60s 启动日志，确认后台危险识别自启动和首轮 UI 资源竞争
- 根据首轮日志将 background_service_manager 首轮策略应用延后 5s，避开 LVGL/CO5300 首帧 flush
- 重新 build、flash COM3 并抓取第二轮 60s 启动日志，确认延后后 UI 首帧 flush 与后台危险识别启动都正常
- 将 ESP-DL runtime 的普通 `non_danger` 推理心跳日志从逐窗打印改为 3s 节流；`danger` 窗口、状态变化和告警日志仍即时打印
- 复烧 3s 心跳版本并抓取 50s 启动日志，统计 `non_danger` 心跳间隔
- 按 Apple Watch 启动计划落地 `安全监听` UI 开关：`background_service_manager` 默认关闭；`danger_detection_ui_open()` 不再自动开启 session；页面 switch 通过 manager 设置用户开关并同步快照。
- 薄重构 `app_main.c` 为本地 stage 函数，保留 `hardware_init.c` 作为 Board Foundation owner；在 `app_main.c` / `lvgl_task.c` 增加 `boot_stage:*` 启动边界日志。
- 重新 `idf.py build`，随后烧录 COM3 并抓取 35s 冷启动日志，验证开机默认不启动危险识别 runtime。
- 新增 `safety_monitor_session.[ch]`，将危险识别 runtime 的启动、停止、错误恢复、运行确认和失败退避从 `background_service_manager` 中收敛出来；同步更新 source tests，避免继续锁 controller 直接启动 ESP-DL backend 的旧实现。
- 已尝试但不应直接重复的路径：
- 不要再让 danger_detection_controller 的返回事件直接 stop danger_detection_service
- 不要让 power_policy 直接操作 LVGL、PMIC 寄存器、模型推理或页面对象

## 观测

- 关键日志/证据：
- idf.py build 通过，生成 build/111.bin，factory 分区剩余约 16%
- validate_context.py --level standard 通过：check.py 错误 0 警告 0
- 首轮板端日志：`power_policy` 进入 `CHARGING`，`background_mgr` 启动危险识别，`Model::test()` 通过并持续输出 non_danger 推理；同时在 UI 首帧附近出现 `Display flush failed: ESP_ERR_NO_MEM`
- 第二轮板端日志：`power_policy` 约 2.3s 进入 `CHARGING`，UI/LVGL/CO5300/touch 约 2.9s 完成首轮初始化；`background_mgr` 约 7.5s 启动危险识别，`Model::test()` 通过并持续输出 non_danger 推理；60s 日志未检出 `ESP_ERR`、`NO_MEM`、`Display flush failed`、panic 或 Guru Meditation
- 日志文件：`board_logs/2026-05-12-power-policy-background.log`、`board_logs/2026-05-12-power-policy-background-deferred.log`
- 日志节流口径：推理仍按 300ms 滑窗运行；普通 `non_danger` 心跳日志每 3s 打一次；`danger` 推理窗口逐窗打，便于观察连续确认过程
- 3s 心跳板测：`board_logs/2026-05-12-espdl-3s-heartbeat.log` 中共 13 条 `INFERENCE` 心跳，平均间隔约 3240ms，最小 3230ms，最大 3250ms；该偏差来自日志只能在推理窗口完成后打印
- 安全监听默认关闭版本：`idf.py build` 通过，`111.bin` 大小 `0x8d4620`，factory 分区剩余 `0x12b9e0`（12%）。
- COM3 冷启动日志：`board_logs/2026-05-12-safety-switch-cold-boot.log` 中可见 `boot_stage: app_start / board_foundation_done / ui_task_created / policy_ready / managers_ready / network_service_ready / official_chat_ready / startup_sequence_done / display_foundation_done / ui_first_frame_ready`；35s 内未出现自动 `background danger detection started`、`INFERENCE`、`Model::test()`、`Display flush failed`、`ESP_ERR_NO_MEM`、panic 或 Guru。
- Safety Monitor session 加深后验证：`uv run python -m unittest tests.test_danger_detection_controller_source tests.test_danger_detection_service_source tests.test_power_integration_source tests.test_safety_monitor_session_source` 通过 11 项；`validate_context.py --level standard` 错误 0 警告 0；`idf.py build` 通过，`111.bin` 大小 `0x8d4760`，factory 分区剩余 `0x12b8a0`（12%）。
- 用户 2026-05-13 上板确认：打开 `安全监听` 后退出危险识别页面，普通 3s 推理日志继续输出；可确认页面退出不再拥有危险识别 stop 生命周期。
- 与预期不一致的点：
- 未记录

## 结论

- 本次可以确认的事实：`power_policy` 与 `background_service_manager` 的第一阶段薄骨架已可编译；危险识别的长期 start/stop owner 已从专页返回事件迁到后台服务管理器；`安全监听` UI 开关已落地，冷启动默认不再启动危险识别 runtime；启动阶段日志已可观测；`safety_monitor_session` 已作为会话生命周期 Module 承接危险识别 runtime 的 start/stop、错误恢复、运行确认和失败退避；用户已上板确认“打开安全监听后退出页面，3s 推理日志继续”。
- 仍然不能确认的事实：
- 未记录

## 未验证风险

- 下一轮仍需补证据的边界：
- 手动关闭 `安全监听` 开关后的 stop 日志、麦克风被前台占用时的 resource_blocked/失败日志
