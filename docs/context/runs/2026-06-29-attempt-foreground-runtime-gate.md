---
id: attempt-foreground-runtime-gate
tags: context, runs, attempt-log, foreground-runtime-gate, resource-arbitration, freertos, ram, espdl, hermes, ble
summary: Watch runtime resource gate 阶段 1：新增强前台独占 gate 最小实现，只提供 owner/quiet window 状态事实，不接入业务 owner。
last_reviewed: 2026-06-29
memory_type: episodic
scope: task
result: success
owners: main/services/foreground_runtime_gate.c, main/services/foreground_runtime_gate.h, main/CMakeLists.txt, tests/test_foreground_runtime_gate_source.py
triggers: foreground_runtime_gate, runtime resource gate, Hermes, BLE, ESP-DL, strong foreground owner, quiet window
evidence_level: design
record_reasons: route-choice, evidence
---

# Attempt Log: Foreground Runtime Gate

## 背景

- 本次要验证什么：在不新增大而全 `ResourceManager` 的前提下，先建立一个很薄的强前台资源事实：当前是否已有 Hermes / BLE 配网 / OTA / 未来重交互页面占用强前台窗口，以及是否处于 quiet window。
- 对应计划：`docs/context/plans/active/2026-06-29-watch-runtime-resource-gate-plan.md` 阶段 1。
- 长期记录理由：这是后续 ESP-DL 让路、Hermes 前台接入、后台 HTTPS 错峰和 BLE quiet-window retry 的地基；需要防止后续 agent 把它扩成中心资源管理器。

## 操作

- 新增：
  - `main/services/foreground_runtime_gate.h`
  - `main/services/foreground_runtime_gate.c`
  - `tests/test_foreground_runtime_gate_source.py`
- 修改：
  - `main/CMakeLists.txt`：把 `foreground_runtime_gate.c` 加入 `service_srcs`。
  - `tests/main_paths.py`：加入 gate 源码/头文件路径。
  - `docs/context/plans/active/2026-06-29-watch-runtime-resource-gate-plan.md`：阶段 1 标记完成，下一步转向 ESP-DL 让路。

## 当前实现边界

- `foreground_runtime_gate` 只提供：
  - `acquire/release`
  - `is_active/current_owner`
  - `quiet_for/is_quiet`
  - owner 文本转换
- 内部只用 `portMUX_TYPE` 保护状态，无动态内存、无 task、无 queue、无 callback。
- 本阶段不接入 Hermes、BLE、ESP-DL 或 OTA 行为；因此不会改变当前运行体验。
- 非当前 owner 不能 release，避免其他模块假释放。
- quiet window 只拒绝新 acquire，不 stop 任何已有 owner。

## 验证

- Source test：

```powershell
uv run python -m unittest tests.test_foreground_runtime_gate_source
```

结果：`Ran 3 tests ... OK`。

- Context 校验：

```powershell
uv run python scripts/context/validate_context.py --level standard --q "watch runtime resource gate foreground runtime gate ESP-DL BLE Hermes" --brief
```

结果：`错误: 0，警告: 0`。

- 构建：

```powershell
& 'D:\esp-idf\v5.5.3\esp-idf\export.ps1'; idf.py build
```

结果：通过。`111.bin` 大小 `0xabc140`，最小 app 分区剩余 `0x343ec0`（23%）。

## 未验证风险

- quiet window 使用 `esp_timer_get_time()`；后续如在极早启动阶段调用，需要确认 esp_timer 已可用。
- 当前 `acquire(timeout_ms)` 不阻塞等待，`timeout_ms` 为后续兼容参数；如果后续真的需要等待，必须单独评估是否会卡 UI 或 owner task。

## 下一步

- 阶段 2：让 ESP-DL / Safety Monitor 在强前台 active 时不启动新推理窗口或跳过当前窗口。
- 阶段 2 仍应保持 owner 边界：gate 不直接 suspend/delete ESP-DL task，只发布事实，由 ESP-DL 或 background owner 自己让路。
