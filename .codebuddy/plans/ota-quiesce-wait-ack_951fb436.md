---
name: ota-quiesce-wait-ack
overview: 按方案 A 补齐 ota_service 的 QUIESCING wait-ACK：在 notify 后补 request → wait_foreground_quiesced(gen, 超时) → finish 三件套，并在统一清理出口中反向 finish，使实现符合活跃计划已确认的"审查修正合同"。
todos:
  - id: add-quiesce-ack
    content: 修改 ota_service.c：新增 quiesce_generation 字段与 kQuiesceWaitTimeoutMs 常量，prepare() 补 request/wait/finish 三件套，cleanup() 兜底 finish 并清零
    status: completed
  - id: update-source-test
    content: 在 tests/test_ota_service_source.py 补充 wait-ACK 三件套源码断言，固化审查合同
    status: completed
    dependencies:
      - add-quiesce-ack
  - id: verify-build-docs
    content: 运行聚焦 source 测试与 idf.py build 验证，并更新 completed 计划文档进度追加方案 A 落地记录
    status: completed
    dependencies:
      - add-quiesce-ack
      - update-source-test
---

## 用户需求

用户已确认 OTA 代码审查 #2 问题按"方案 A"实现：为 `ota_service.c` 的 QUIESCING 阶段补齐 `request_foreground_quiesce -> wait_foreground_quiesced -> finish_foreground_quiesce` 的同步 wait-ACK 机制，使 OTA 在确认 Safety Monitor/可抢占后台任务让路之后才继续下载，并确保任何非重启失败/取消出口都能反向清理 quiesce 请求。

## 核心功能

- `ota_service_prepare()`：发布 QUIESCING 后，在现有 `notify_foreground_runtime_changed()` 基础上补 request/wait/finish 三件套；wait 成功才发布 QUIESCED/READY，失败走 cleanup 并发布 FAILED。
- `ota_service_cleanup_maintenance()`：作为统一非重启出口，末尾兜底结束未完成的 quiesce 请求并清零代次。
- 成功路径（STAGED -> ACTIVATE -> restart）不经过 cleanup，无需 finish（重启后状态重置）。
- 不修改 `ota_service.h` 公开接口，不修改 `background_service_manager` 本身。

## 技术栈

- 沿用现有 ESP-IDF 5.5 + FreeRTOS 架构，仅修改 `main/services/ota/ota_service.c` 单文件。
- 复用已核实的现成 API（`main/services/safety/background_service_manager.h`）：
- `background_service_manager_request_foreground_quiesce(uint32_t *out_generation)`
- `background_service_manager_wait_foreground_quiesced(uint32_t generation, uint32_t timeout_ms)`
- `background_service_manager_finish_foreground_quiesce(uint32_t generation)`

## 实现思路

### 修改点 1：新增字段与常量（`ota_service.c`）

- `ota_service_context_t` 新增 `uint32_t quiesce_generation;` 字段，记录当前未结束的 quiesce 代次，初始 0 表示无进行中请求。
- 顶部常量区新增 `kQuiesceWaitTimeoutMs = 3000U`（注释说明：Safety Monitor 停止确认等待上限，与 official_chat 的 2500ms 同量级）。

### 修改点 2：`ota_service_prepare()` 补齐 wait-ACK

流程变为：PREPARING -> try_acquire(gate) -> power window -> 发布 QUIESCING -> notify（owner 立即让路）-> `request_foreground_quiesce(&gen)` -> `wait_foreground_quiesced(gen, kQuiesceWaitTimeoutMs)` -> 成功则 `finish_foreground_quiesce(gen)` 并清零代次 -> 发布 QUIESCED -> 发布 READY。

- request/wait 任一步失败：调用 `ota_service_cleanup_maintenance()`（内部兜底 finish）并发布 FAILED 返回。
- wait 成功后立即 finish（对齐 official_chat 用法：等待窗口结束即结束请求），后续下载失败走 cleanup 时不会重复 finish。

### 修改点 3：`ota_service_cleanup_maintenance()` 兜底清理

在现有 abort staged -> 关 power window -> release gate -> notify 之后，追加：若 `s_ota.quiesce_generation != 0`，调用 `finish_foreground_quiesce(gen)`（失败仅 WARN 日志）并清零，保证"任何不重启出口按 abort -> finish_quiesce -> release 反向清理"的合同成立。

## 执行细节

- **爆炸半径控制**：只改 `ota_service.c` 一个文件；保留现有 `notify_foreground_runtime_changed()`（与 request/wait 互补，owner 观测 gate 立即让路）。不新增配置项、不重构无关逻辑。
- **并发与日志**：wait 由 OTA task 阻塞执行（8KB internal 栈内，最多阻塞 3s），符合现有 task 模型；失败路径日志复用现有 `ESP_LOGW/ESP_LOGI`，不引入新日志噪音。
- **验证**：

1. `uv run python -m pytest tests/test_ota_service_source.py` —— 在测试中补充断言（`request_foreground_quiesce`、`wait_foreground_quiesced`、`finish_foreground_quiesce` 出现在 `ota_service.c` 源码），固化 wait-ACK 合同。
2. `idf.py build` 通过（构建产物 `111.bin`）。
3. 更新 `docs/context/plans/completed/2026-07-30-standalone-https-ota-maintenance-plan.md` 进度，追加方案 A 落地记录。

## 目录结构

```
project-root/
├── main/
│   └── services/
│       └── ota/
│           └── ota_service.c        # [MODIFY] 新增 quiesce_generation 字段、kQuiesceWaitTimeoutMs 常量；prepare() 补 request/wait/finish；cleanup() 兜底 finish
└── tests/
    └── test_ota_service_source.py   # [MODIFY] 补充 wait-ACK 三件套源码断言
```

## 关键代码结构（接口契约）

- 新增状态字段：`uint32_t quiesce_generation;`（0 = 无进行中 quiesce 请求）。
- 新增常量：`static const uint32_t kQuiesceWaitTimeoutMs = 3000U;`
- prepare() 中调用顺序：`request_foreground_quiesce(&gen)` -> `wait_foreground_quiesced(gen, kQuiesceWaitTimeoutMs)` -> 成功后 `finish_foreground_quiesce(gen)`；失败统一 `ota_service_cleanup_maintenance()` + 发布 FAILED。
- cleanup() 末尾：`if (s_ota.quiesce_generation != 0) { finish(gen); 清零; }`