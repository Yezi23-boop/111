---
id: ui-refresh-policy-idle-dim
tags: project, lvgl, ui, power, brightness, touch
summary: 记录当前仓库 UI 运行态 STANDBY、渐进变暗、刷新降频与强制活跃开关的最小实现策略。
last_reviewed: 2026-06-01
memory_type: semantic
scope: repo
owners: main/ui/ui_refresh_policy.c, components/co5300_panel, main/ui/lvgl_task.c
triggers: ui, refresh, policy, idle, dim, standby
evidence_level: observed
---

# UI 运行态 STANDBY 与刷新策略

## 适用范围

- 当前仓库 `main/ui/lvgl_task.c`
- `components/lvgl_port/lv_port_input.c`
- `components/co5300_panel`

## 当前策略

- 最近 `30s` 内有触摸或高优先级活动通知：按 `ACTIVE` 处理
- 活跃态：`delay_ms = MIN(lv_timer_handler() 返回值, 16ms)`
- `STANDBY`：`delay_ms = MAX(lv_timer_handler() 返回值, 500ms)`
- `STANDBY` 亮度：从当前用户亮度开始，约 `5s` 渐进降到 `0%`
- 强制活跃开关打开时：始终保持活跃刷新，且不自动进入 `STANDBY`
- 第一版 `STANDBY` 不调用 CO5300 sleep-in、不反初始化 panel、不关闭显示供电轨

## 接缝位置

- `main/ui/ui_refresh_policy.[ch]`
  - 维护最近活跃时间、强制活跃状态、用户亮度
  - 发布 `ui_refresh_policy_get_activity_snapshot()` 只读快照，供 `power_policy` 读取 UI 活跃度事实
  - `ui_refresh_policy_notify_activity()` 可供 P0 提醒、低电量提示等非触摸路径唤醒 UI
- `main/ui/lvgl_task.c`
  - 每轮 `lv_timer_handler()` 后调用 `ui_refresh_policy_poll()`
  - 再通过 `ui_refresh_policy_adjust_delay()` 决定 `vTaskDelay`
- `components/lvgl_port/lv_port_input.c`
  - 读到有效触点后调用 `ui_refresh_policy_notify_touch()`
- `main/services/power/power_policy.c`
  - 只读消费 activity snapshot，把 `STANDBY` 转换成整机预算
  - 不反向调用 UI poll、亮度、LVGL 或面板接口
- `main/ui/generated/events_init.c`
  - 亮度滑条改为同时更新 `ui_refresh_policy_set_user_brightness_percent()`
- `components/co5300_panel`
  - 新增亮度命令封装，使用 `0x51` 写面板亮度

## 实现注意点

- `main/ui/lvgl_task.c` 中 `freertos/FreeRTOS.h` 和 `freertos/task.h` 需要放在会间接包含 `lvgl.h` 的 GUI 头之前。
- 若让 `events_init.h`、`gui_guider.h` 或 `lvgl.h` 先于 FreeRTOS 头进入编译单元，当前仓库在 ESP-IDF 5.5.3 下会触发 `FreeRTOS.h: Missing definition: portYIELD_CORE()` 编译错误。
- 当前这块 QSPI AMOLED 的亮度虽然也是走面板 `0x51`，但不能直接对 `panel_io` 裸发 `esp_lcd_panel_io_tx_param(..., 0x51, ...)`。
- `espressif__esp_lcd_co5300` 在 QSPI 模式下会先把命令编码成 `LCD_OPCODE_WRITE_CMD << 24 | (cmd & 0xff) << 8` 再发，因此当前仓库应复用 `esp_lcd_panel_co5300_set_brightness()`，否则手动亮度和 idle dim 都可能“看起来调用成功但屏幕不变”。

## 设计边界

- 当前策略不修改 `lv_tick_inc()` 周期，避免直接改变 LVGL timer 语义
- 当前策略只控制“调度频率 + 亮度”，不进入 `light sleep/deep sleep`
- 触摸仍是轮询读，不是 `GPIO38` 中断唤醒，所以该策略优先降低 CPU/刷新活动，不代表最终极限功耗
- `activity_snapshot` 只读：不调用 `ui_refresh_policy_poll()`，不写面板亮度，不触发 LVGL 操作，也不依赖 `power_policy`
- `power_policy` 只能读取该快照做整机状态判断，不应反向接管 `ui_refresh_policy` 的主循环延时和亮度写入
- 不再把 `IDLE_DIM` 作为产品级状态；短暂不动变暗已被 30 秒 `STANDBY` 取代

## 后续扩展建议

- 若后续有危险检测、波形、频谱、动画页等需要持续高刷的场景，优先复用 `ui_refresh_policy_set_force_active()` 或升级为令牌式高刷租约
- 若要继续压低待机功耗，下一步优先把触摸从轮询改为中断唤醒，再考虑 CO5300 sleep-in 或真正 ESP sleep
