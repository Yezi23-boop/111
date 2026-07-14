---
id: light-allowed-runtime-save-plan-20260707
tags: plan, active, watch, low-power, standby, light-allowed, runtime-save, lvgl, power-policy
summary: 将 LIGHT_ALLOWED 收敛为运行态深省电候选：30 秒 STANDBY 保持现有基础省电，5 分钟无交互后只新增 LVGL 进一步降频，不进入 ESP Light Sleep，不断 Wi-Fi 或 PMIC 主电源轨。
status: active
last_reviewed: 2026-07-07
memory_type: project_plan
scope: repo
owners: main/ui/ui_refresh_policy.c, main/services/power/power_policy.c, main/services/network/network_service.c, main/services/power/sleep_coordinator.c, main/services/safety/background_service_manager.c
triggers: LIGHT_ALLOWED, STANDBY, runtime save, low power experience, LVGL 2000ms, 省电体验, 运行态深省电
evidence_level: design
depends_on: docs/context/knowledge/project/low-power-framework-architecture.md, docs/context/knowledge/project/runtime-owner-contract.md
---

# LIGHT_ALLOWED 运行态深省电执行计划

## 目标与全局

- 任务目标：把 `LIGHT_ALLOWED` 定义为 `STANDBY` 之后的运行态深省电候选，而不是立即进入 ESP Light Sleep。
- 为什么现在做：当前 `STANDBY` 已能完成基础省电，但 `LIGHT_ALLOWED` 还只像一个 sleep readiness 字段；需要先把 5 分钟后的省电体验和 owner 边界固定下来，后续外设断电才有可靠入口。
- 完成后用户会看到什么变化：30 秒无交互后屏幕渐暗到 0；5 分钟无交互后界面仍保持黑屏/极暗，但系统内部进一步降低 LVGL 唤醒频率；触摸、按键或 P0 危险提醒仍能恢复。

## 产品语义

当前只保留两个用户可理解的产品态：

```text
ACTIVE
STANDBY
```

`LIGHT_ALLOWED` 不是第三个产品态，也不出现在 UI 文案中。它是 `STANDBY` 内部的预算许可：

```text
state = STANDBY
sleep_permission = LIGHT_ALLOWED
```

当前阶段 `LIGHT_ALLOWED` 的含义是：

- 已经长时间无屏幕/触摸交互。
- 当前没有阻塞 sleep / 深省电的 blocker。
- 可以让部分 owner 进入更安静的运行态省电策略。
- 只打印 dry-run 证据，不调用 `esp_light_sleep_start()`。

## 已定决策

- 30 秒无交互进入 `STANDBY`。
- 5 分钟无交互才允许发布 `LIGHT_ALLOWED`。
- `STANDBY` 第一层继续保持现有动作：屏幕渐暗到 0%、LVGL 降低刷新、Wi-Fi modem PS、暂停非关键网络同步。
- `LIGHT_ALLOWED` 第一版只新增一个真实动作：LVGL 主循环进一步降频到 `2000ms`。
- `power_service` 电源轮询第一版保持当前策略，不因为 `LIGHT_ALLOWED` 再降一档。
- `network_service` 第一版保持现有 `STANDBY` 行为：Wi-Fi power save、保连接、暂停非关键云端探测；不新增复杂保活策略。
- `sleep_coordinator` 继续只 dry-run，不进入真实 ESP sleep。
- 恢复路径不新增 `exit_light_allowed()` 或第二套状态机，复用现有 activity 恢复路径。
- P0 危险提醒优先级高于省电预算，必须唤醒 UI，并退出 `LIGHT_ALLOWED`。

## 范围与非目标

本轮明确要做：

- 让 `ui_refresh_policy` 在 `STANDBY + LIGHT_ALLOWED` 时把 LVGL 主循环延时从 `500ms` 提升到 `2000ms`。
- 保留 `power_policy` 的 5 分钟 `LIGHT_ALLOWED` 门槛。
- 补 source tests 锁定 30 秒/5 分钟/2000ms/不进 sleep 的边界。
- 补 context 文档和 changelog。

本轮明确不做：

- 不调用 `esp_light_sleep_start()` 或 `esp_deep_sleep_start()`。
- 不新增 `DEEP_STANDBY`、`LOW_POWER_MODE` 或大而全电源总管。
- 不关闭 Wi-Fi STA 连接。
- 不关闭 PMIC 主电源轨、Flash、PSRAM、RTC、触摸唤醒链路。
- 不真的断 SD 卡、音频 codec、I2S、麦克风、传感器或 PMIC rails。
- 不改变 P0 危险提醒的唤醒优先级。

## 进度

- `[x]` 已完成计划讨论：目标收敛为省电体验，不进入真实 ESP Light Sleep。
- `[x]` 已完成边界决策：`LIGHT_ALLOWED` 不作为 UI 产品态，只作为 `STANDBY` 内部深省电候选。
- `[x]` 已完成执行范围决策：第一版只新增 LVGL 主循环进一步降频到 `2000ms`。
- `[ ]` 待实现 Phase 1-4。
- `[ ]` 待上板验证 Phase 5。

## 当前行为基线

30 秒 `STANDBY` 当前应保持：

```text
屏幕渐暗到 0%
LVGL 主循环延时约 500ms
Wi-Fi modem PS
非关键网络同步暂停
Safety Monitor 不被普通 STANDBY 关闭
不进入 ESP sleep
```

5 分钟 `LIGHT_ALLOWED` 目标行为：

```text
屏幕继续保持 0%
LVGL 主循环延时约 2000ms
power_service 轮询不变
network_service 维持 STANDBY 行为
sleep_coordinator 只 dry-run
不进入 esp_light_sleep_start()
```

## Owner 边界

### `ui_refresh_policy`

负责：

- 维护 UI 活跃度事实。
- 30 秒进入 `STANDBY`。
- 屏幕渐暗到 0%。
- 根据 `power_policy` 预算或等价 snapshot 判断是否使用 `2000ms` 深省电延时。

不负责：

- 不计算整机预算。
- 不控制 Wi-Fi、后台任务、PMIC、音频或 sleep API。

### `power_policy`

负责：

- 读取 `ui_refresh_policy` 只读快照。
- 30 秒后发布 `state=STANDBY`。
- 5 分钟后发布 `sleep_permission=LIGHT_ALLOWED` 和有效 `sleep_interval_hint_ms`。
- 聚合 blocker 和预算字段。

不负责：

- 不直接调用 LVGL、亮度、Wi-Fi、PMIC 或 sleep API。

### `network_service`

负责：

- 消费 `STANDBY` 预算。
- 开启 Wi-Fi power save。
- 暂停非关键云端探测。
- 保持 Wi-Fi 连接。

本轮不新增：

- 不新增 `LIGHT_ALLOWED` 专属网络策略。
- 不断网。
- 不做重连风暴控制重构。

### `background_service_manager`

负责：

- 继续按用户开关和预算管理 Safety Monitor。
- 保证普通 `STANDBY` / `LIGHT_ALLOWED` 不压制 P0 危险提醒。

本轮不新增：

- 不新增通用后台任务暂停总管。

### `sleep_coordinator`

负责：

- 读取 `sleep_permission`、`sleep_blockers`、`sleep_interval_hint_ms`。
- 打印 dry-run 证据。

不负责：

- 不进入 `esp_light_sleep_start()`。
- 不解释 owner 语义。
- 不反向修改 power budget。

## 外设断电候选清单

后续可以作为候选，但本轮不执行：

```text
SD 卡
音频 codec / I2S
麦克风链路
屏幕背光
非关键传感器
非关键后台任务
```

本轮明确不碰：

```text
PMIC 主电源轨
Flash / PSRAM
Wi-Fi STA 连接
RTC / PCF85063ATL
触摸唤醒链路
危险提醒必需链路
```

## 实现步骤

- `[ ]` Phase 1：复查 `power_policy` 当前 5 分钟门槛是否仍为 `k_light_allowed_idle_time_ms = 5LL * 60LL * 1000LL`。
- `[ ]` Phase 2：让 `ui_refresh_policy_adjust_delay()` 在 `LIGHT_ALLOWED` 时返回至少 `2000ms`。
- `[ ]` Phase 3：补 source tests，锁定：
  - `STANDBY` 仍是 30 秒。
  - 普通 `STANDBY` 仍是 `500ms`。
  - `LIGHT_ALLOWED` 后才是 `2000ms`。
  - `sleep_coordinator` 仍不调用 ESP sleep API。
  - P0 提醒仍调用 activity 唤醒路径。
- `[ ]` Phase 4：运行 context standard、相关 source tests、`idf.py build`。
- `[ ]` Phase 5：上板观察：
  - 30 秒进入 `STANDBY`。
  - 5 分钟后出现 `sleep=LIGHT_ALLOWED` dry-run。
  - 5 分钟后 UI 不 panic、不黑屏后卡死。
  - 触摸/按键可恢复。
  - 采集窗口内无 Guru、watchdog、NO_MEM。

## 验证与验收

计划运行的验证命令：

```powershell
python -m unittest tests.test_power_integration_source tests.test_ui_refresh_policy_source
uv run python scripts/context/validate_context.py --level standard --q "LIGHT_ALLOWED runtime save LVGL 2000ms STANDBY" --brief
cmd /c "call D:\esp-idf\v5.5.3\esp-idf\export.bat >nul && idf.py build"
```

期望看到的结果：

- source tests 通过。
- context check 错误 0、警告 0。
- `idf.py build` 通过。
- 上板日志中 `STANDBY` 与 `LIGHT_ALLOWED` 分层清楚。

## 幂等与恢复

- 如果中途中断，下次先从本文件 Phase 1 继续。
- 如果 `2000ms` 导致触摸恢复迟钝，优先把 `LIGHT_ALLOWED` LVGL 延时回退到 `1000ms` 或恢复 `500ms`，不回滚 5 分钟门槛。
- 如果上板发现 P0 提醒无法唤醒 UI，立即停止推进更深省电动作，先修复安全提醒优先级。
- 如果网络恢复异常，不新增 sleep 行为，先维持 `network_service` 当前 STANDBY 策略。

## 下一步

下一步最小动作：

```text
只实现 LVGL 在 LIGHT_ALLOWED 下进一步降频到 2000ms。
```

暂不实现外设断电；外设断电必须另开 owner-by-owner 计划，每个外设单独证明断电、恢复和失败回退路径。
