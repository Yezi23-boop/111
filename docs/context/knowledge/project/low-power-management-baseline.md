---
id: project-low-power-management-baseline
tags: project, power, low-power, axp2101, lvgl, wifi
summary: 当前仓库已落地的运行态 STANDBY、电源观测与时间唤醒证据能力，以及仍未进入 ESP sleep 的缺口。
last_reviewed: 2026-06-01
memory_type: semantic
scope: repo
owners: components/axp2101, main/app/board_power.c, main/services/power/power_service.c, main/ui/ui_refresh_policy.c
triggers: low, power, management, baseline
evidence_level: observed
---

# 当前项目低功耗管理基线

## 一句话结论

当前项目已经落地了“电源观测 + 运行态 `STANDBY` + 局部场景节能”三层能力，但还没有进入 `Light Sleep / Deep Sleep` 的系统级睡眠编排。

## 已落地能力

### 1. 电源观测层已经接入主流程，但仍停留在只读阶段

- `components/axp2101/axp2101.c` 已能探测 `AXP2101` 并读取 `VBUS / battery / vsys / charging` 等快照。
- `main/app/board_power.c` 把 PMIC 原始快照转换成板级统一语义，并维护最近一次成功采样缓存。
- `main/services/power/power_service.c` 以后台任务方式发布稳定电源状态：
  - 正常路径默认 `1s` 轮询；
  - 连续失败时退避到 `2s / 5s`；
  - 对 `battery_mv / system_mv` 使用 `20mV` 抖动阈值；
  - 通过双缓冲发布只读快照，避免上层读到半更新状态。
- `main/app/hardware_init.c` 会在启动阶段调用 `board_power_init()`，随后立即抓取并打印一帧 `Board power boot snapshot`。

结论：

- 当前项目已经具备“看见电源状态变化”的能力。
- 当前项目还没有开放 PMIC 电源轨控制、充电参数调节、睡眠/唤醒寄存器编排。

### 2. UI 运行态 STANDBY 已经实际落地

- `main/ui/ui_refresh_policy.c` 已实现最小运行态低功耗策略：
  - 最近一次交互后的活跃窗口为 `30s`；
  - 活跃态把 UI 主循环最大延时压到 `16ms`；
  - `STANDBY` 把 UI 主循环最小延时抬到 `500ms`；
  - `STANDBY` 下屏幕按约 `5s` 渐进降到 `0%` 亮度；
  - 第一版不调用 CO5300 sleep-in、不反初始化 panel、不关闭显示供电轨；
  - 仍保留 `force_active` 逃生口，供动画、提示页或特殊页面强制常亮。
- `main/ui/lvgl_task.c` 在每次 `lv_timer_handler()` 后调用 `ui_refresh_policy_poll()`，再通过 `ui_refresh_policy_adjust_delay()` 调整下一轮 `vTaskDelay()`。
- `components/lvgl_port/lv_port_input.c` 在触摸命中时调用 `ui_refresh_policy_notify_touch()`，触摸会把 UI 从 `STANDBY` 拉回 `ACTIVE`。
- `main/ui/generated/events_init.c` 中的亮度滑条会调用 `ui_refresh_policy_set_user_brightness_percent()`，说明用户亮度设置已经纳入策略层。
- `components/co5300_panel/co5300_panel.c` 已提供 `co5300_panel_set_brightness_percent()`，`ui_refresh_policy` 通过它实际下发屏幕亮度。
- `main/services/power/power_policy.c` 只读消费 UI activity snapshot，把 `STANDBY` 转译成整机预算，不直接控制 LVGL、亮度或面板。
- `main/services/network/network_service.c` 消费预算后只切换 `wifi_control_set_power_save()` 并暂停非关键云端探测，不断开 AP 或销毁 IP 状态。

结论：

- 当前项目已经不是“全程满亮 + 固定高频刷新”。
- 当前 `STANDBY` 仍是运行态省电，不等于 ESP light sleep、deep sleep 或 PMIC power off。

### 3. Wi-Fi 已有局部场景化省电切换

- `components/wifi_control/src/wifi_control.c` 已提供 `wifi_control_set_power_save(bool enable)`，底层调用 `esp_wifi_set_ps(enable ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE)`。
- `components/official_chat/application.cc` 已按聊天状态切换 Wi-Fi Power Save：
  - `Connecting / Listening / Speaking` 时关闭省电；
  - `Activating / Upgrading / Idle` 时打开省电。

结论：

- 当前仓库已经存在“按场景切换无线功耗”的真实先例。
- 旧 `wifi_provision_set_power_save()` 已经退场；当前这套能力由 `wifi_control` 承接，但仍只在 `official_chat` 场景里使用，尚未上升为整机统一策略。

### 4. 局部组件已经开始考虑低功耗友好配置

- `main/app/hardware_init.c` 中 `button_gpio_config_t.enable_power_save = true`，说明按键驱动已允许进入其组件侧低功耗路径。

这类设置的价值不在于单点节电有多大，而在于说明当前工程已经接受“组件本身要对低功耗友好”这件事。

## 当前明确未落地的部分

### 1. 系统级睡眠入口已有默认关闭测试门控

当前 `main/services/power/sleep_coordinator.c` 只保留 sleep 预算 dry-run，不再保留手动 Light-sleep 测试入口：

- 不调用 `esp_light_sleep_start`
- 不调用 `esp_sleep_enable_timer_wakeup`
- 不读取 `esp_sleep_get_wakeup_cause`

这意味着普通固件仍不进入系统级 sleep；当前只能说明 STANDBY 预算和 dry-run 观测成立，不能说明板端 Light Sleep 唤醒恢复已经闭环。

### 2. `power_policy` 已有第一版，但还不是睡眠编排器

当前代码里已经能看到：

- 只读电源事实源：`components/axp2101`、`main/app/board_power.c`
- 电源状态发布层：`main/services/power/power_service.c`
- UI 运行态省电：`main/ui/ui_refresh_policy.c`
- 整机预算发布层：`main/services/power/power_policy.c`
- 网络预算消费者：`main/services/network/network_service.c` + `components/wifi_control`

但当前第一版仍主要覆盖运行态预算和默认关闭的 sleep 测试门控，还没有编排：

- 何时进入 CO5300 硬件 sleep-in 或显示 rail 关闭；
- 何时暂停音频/传感器
- 显式 `Light Sleep` 测试前是否要暂停哪些可暂停后台任务
- 唤醒后先恢复哪些模块

### 3. RTC/PMIC 唤醒链路进入证据闭环阶段

当前已新增：

- `components/pcf85063atl`
- `main/services/power/wakeup_evidence_service.[ch]`

当前边界是：

- `RTC_INT(GPIO39)` 先通过 PCF85063ATL countdown timer 做运行态证据日志；
- AXP2101 IRQ bank 先通过轮询状态寄存器做事件证据；
- `AXP_IRQ` 仍只确认到 `EXIO5`，没有确认最终 MCU GPIO；
- `PWRON/PWROK` 仍不作为应用层 GPIO 控制入口。
- `Light Sleep` 测试代码必须默认关闭，不能跟随开机自动执行；主 USB 串口/JTAG 不足以作为唯一观测口。

这意味着本仓库已经开始验证外部唤醒增强，但仍不能把它描述成“已经具备 RTC/PMIC 外部睡眠唤醒能力”。当前不保留 timer-based Light Sleep / Deep Sleep 显式实验代码；`RTC_INT(GPIO39)`、`AXP_IRQ/EXIO5` 与 PCF85063ATL/PMIC IRQ 证据闭环只作为后续外部唤醒增强前置。

### 4. 高耗电外设还没纳入统一停启策略

当前能确认的已落地节能手段主要集中在：

- UI 降频
- 屏幕降亮
- Wi-Fi modem power save

但还没有证据表明以下资源已被统一纳入低功耗状态机：

- 显示控制器的显示休眠/灭屏命令
- 音频 codec / I2S 的待机切换
- 触摸控制器的 monitor/sleep 编排
- 传感器轮询频率降低
- 后台业务任务的挂起或节流

## 对当前项目的真实判断

更准确地说，当前项目已经落地的是：

1. `Power Observe`
2. `Runtime STANDBY`
3. `Power-policy-driven Wi-Fi PS`

尚未落地的是：

1. `Light Sleep`
2. `Deep Sleep / Power Off`
3. `Wakeup orchestration`

所以后续如果要继续做低功耗，不应该把当前项目描述成“还没有低功耗”，也不应该误判成“已经有完整低功耗框架”。更合适的说法是：

- 运行态低功耗基础件已经收口到 `ui_refresh_policy -> power_policy -> 各资源 owner`；
- 系统级睡眠、唤醒恢复和外设停启还没有闭环。

## 后续最稳的接入起点

后续若继续做低功耗，优先级建议是：

1. 继续补 `power_policy` 预算消费者：音频、传感器、非关键后台任务应各自消费预算降级，而不是让策略层直接操作资源。
2. 在不碰 `Deep Sleep` 的前提下，加深 `STANDBY` 路线：
   - 可恢复的屏幕更低功耗模式
   - 更明确的网络同步暂停/恢复边界
   - 保留快速触摸/按键/P0 提醒恢复
3. 先让 `sleep_coordinator` dry-run 可观测；同时继续用 `wakeup_evidence_service` 闭环 `RTC_INT(GPIO39)` 与 AXP2101 IRQ 状态日志，作为后续外部唤醒增强，而不是恢复手动 sleep harness 的前置借口。
4. 若目标是 STANDBY 下 Wi-Fi 保持连接并继续省电，路线应从 `network_service -> wifi_control_set_power_save()` 已有 Wi-Fi modem PS 能力继续推进到 ESP-IDF Automatic Light-sleep；不要恢复手动 `sleep_coordinator` Light-sleep harness 当成 Wi-Fi 保持连接的产品方案。

这样做的原因是：

- 当前仓库已经有 `board_power`、`power_service`、`ui_refresh_policy` 这些良好接缝；
- 当前仓库已经有 `network_service` 消费 budget 并开启 Wi-Fi PS 的接缝；
- 直接跳到深睡，会同时撞上 PMIC、RTC、共享 I2C、唤醒顺序和 UI/网络恢复等多条高风险链路。

## 证据来源

- `main/app/app_main.c`
- `main/app/hardware_init.c`
- `main/app/board_power.c`
- `main/app/board_power.h`
- `main/services/power/power_service.c`
- `main/services/power/wakeup_evidence_service.c`
- `components/pcf85063atl/pcf85063atl.c`
- `main/ui/lvgl_task.c`
- `main/ui/ui_refresh_policy.c`
- `main/ui/ui_refresh_policy.h`
- `main/ui/generated/events_init.c`
- `components/lvgl_port/lv_port_input.c`
- `components/co5300_panel/co5300_panel.c`
- `components/wifi_control/src/wifi_control.c`
- `components/official_chat/application.cc`
- `sdkconfig`
- `sdkconfig.defaults`
