---
id: project-low-power-management-baseline
tags: project, power, low-power, axp2101, lvgl, wifi
summary: 当前仓库已落地的低功耗能力碎片、未落地缺口，以及后续统一功耗策略的接入起点。
last_reviewed: 2026-04-21
memory_type: semantic
scope: repo
owners: components/axp2101, main/app/board_power.c, main/services/power_service.c, main/ui/ui_refresh_policy.c
triggers: low, power, management, baseline
evidence_level: observed
---

# 当前项目低功耗管理基线

## 一句话结论

当前项目已经落地了“运行态省电 + 电源观测 + 局部场景节能”三层能力，但还没有真正进入 `Standby / Light Sleep / Deep Sleep` 的系统级统一编排。

## 已落地能力

### 1. 电源观测层已经接入主流程，但仍停留在只读阶段

- `components/axp2101/axp2101.c` 已能探测 `AXP2101` 并读取 `VBUS / battery / vsys / charging` 等快照。
- `main/app/board_power.c` 把 PMIC 原始快照转换成板级统一语义，并维护最近一次成功采样缓存。
- `main/services/power_service.c` 以后台任务方式发布稳定电源状态：
  - 正常路径默认 `1s` 轮询；
  - 连续失败时退避到 `2s / 5s`；
  - 对 `battery_mv / system_mv` 使用 `20mV` 抖动阈值；
  - 通过双缓冲发布只读快照，避免上层读到半更新状态。
- `main/app/hardware_init.c` 会在启动阶段调用 `board_power_init()`，随后立即抓取并打印一帧 `Board power boot snapshot`。

结论：

- 当前项目已经具备“看见电源状态变化”的能力。
- 当前项目还没有开放 PMIC 电源轨控制、充电参数调节、睡眠/唤醒寄存器编排。

### 2. UI 运行态省电已经实际落地

- `main/ui/ui_refresh_policy.c` 已实现最小运行态低功耗策略：
  - 最近一次交互后的活跃窗口为 `5s`；
  - 活跃态把 UI 主循环最大延时压到 `16ms`；
  - 空闲态把 UI 主循环最小延时抬到 `100ms`；
  - 空闲态亮度降到用户亮度的 `40%`；
  - 仍保留 `force_active` 逃生口，供动画、提示页或特殊页面强制常亮。
- `main/ui/lvgl_task.c` 在每次 `lv_timer_handler()` 后调用 `ui_refresh_policy_poll()`，再通过 `ui_refresh_policy_adjust_delay()` 调整下一轮 `vTaskDelay()`。
- `components/lvgl_port/lv_port_input.c` 在触摸命中时调用 `ui_refresh_policy_notify_touch()`，触摸会把 UI 从 `idle_dim` 拉回 `active`。
- `main/ui/generated/events_init.c` 中的亮度滑条会调用 `ui_refresh_policy_set_user_brightness_percent()`，说明用户亮度设置已经纳入策略层。
- `components/co5300_panel/co5300_panel.c` 已提供 `co5300_panel_set_brightness_percent()`，`ui_refresh_policy` 通过它实际下发屏幕亮度。

结论：

- 当前项目已经不是“全程满亮 + 固定高频刷新”。
- 当前 UI 省电仍属于 `Active -> Idle-Dim` 级别，不等于灭屏、待机或休眠。

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

### 1. 没有系统级睡眠入口

在当前 `main/` 与业务相关 `components/` 中，未检索到以下系统级低功耗入口：

- `esp_light_sleep_start`
- `esp_deep_sleep_start`
- `esp_sleep_enable_*`
- `esp_pm_configure`

这说明当前项目还没有真正进入 ESP-IDF 的系统级 light sleep / deep sleep 编排阶段。

### 2. 没有统一的 `power_policy`

当前代码里已经能看到：

- 只读电源事实源：`components/axp2101`、`main/app/board_power.c`
- 电源状态发布层：`main/services/power_service.c`
- UI 运行态省电：`main/ui/ui_refresh_policy.c`
- 局部网络省电：`components/official_chat/application.cc`

但还没有一个统一的策略层去编排：

- 何时 dim
- 何时灭屏
- 何时降低网络活跃度
- 何时暂停音频/传感器
- 何时允许进入 `Standby / Light Sleep`
- 唤醒后先恢复哪些模块

### 3. RTC/PMIC 唤醒链路仍未接入

当前 `main/` 与 `components/` 中未检索到：

- `RTC_INT`
- `AXP_IRQ`
- `PWRON`
- `PWROK`
- `PCF85063ATL` 正式驱动接入

这意味着：

- 现在还没有可靠的 RTC 定时唤醒闭环；
- 也没有 PMIC 中断事件驱动的电源状态编排；
- 长按关机、低电量保护关机、RTC 定时唤醒这些能力仍不应直接落到现有主链路里。

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
2. `UI Idle-Dim`
3. `Scene-based Wi-Fi PS`

尚未落地的是：

1. `Standby`
2. `Light Sleep`
3. `Deep Sleep / Power Off`
4. `Wakeup orchestration`

所以后续如果要继续做低功耗，不应该把当前项目描述成“还没有低功耗”，也不应该误判成“已经有完整低功耗框架”。更合适的说法是：

- 低功耗基础件已经分散落地；
- 系统级功耗策略还没有收口成一个统一状态机。

## 后续最稳的接入起点

后续若继续做低功耗，优先级建议是：

1. 新增 `main/services/power_policy.[ch]`，先统一编排 `ui_refresh_policy + wifi_control_set_power_save + 音频/传感器/后台任务节流`。
2. 在不碰 `Deep Sleep` 的前提下，先补 `Standby` 路线：
   - 灭屏
   - 更激进的网络省电
   - 停音频活动
   - 保留快速触摸/按键恢复
3. 等 `RTC_INT / AXP_IRQ / PCF85063ATL` 证据链闭环后，再讨论真正的 `Light Sleep / Deep Sleep / Power Off`。

这样做的原因是：

- 当前仓库已经有 `board_power`、`power_service`、`ui_refresh_policy` 这些良好接缝；
- 直接跳到深睡，会同时撞上 PMIC、RTC、共享 I2C、唤醒顺序和 UI/网络恢复等多条高风险链路。

## 证据来源

- `main/app/app_main.c`
- `main/app/hardware_init.c`
- `main/app/board_power.c`
- `main/app/board_power.h`
- `main/services/power_service.c`
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
