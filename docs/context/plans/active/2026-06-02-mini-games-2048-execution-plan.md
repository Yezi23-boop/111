---
id: mini-games-2048-execution-plan
tags: context, plans, task-memory, mini-games, 2048, lvgl, touch, button, watch
summary: 2048 小游戏第一阶段执行计划，固定 Game 入口、纯规则模块、LVGL 前台 controller、触摸滑动、物理短按/长按和验证闭环。
last_reviewed: 2026-06-02
memory_type: task
scope: repo
owners: main/features/mini_games, main/ui/custom, main/ui/generated/events_init.c, main/ui/lvgl_task.c, main/app
triggers: 2048, mini_games, Game入口, 小游戏执行计划, 物理按键, LVGL游戏页
evidence_level: design
status: active
---

# 2048 小游戏执行计划

## Problem Statement

- 当前主屏已有 `Game` 图标位，但还没有绑定实际入口；用户希望先把小游戏能力落成可执行计划，再开始代码实现。
- 已确认第一款小游戏是 `2048`，目标是在不扩大后台架构、不改显示/触摸底层、不增加音频/联网资源压力的前提下，做出可上板验证的第一版。
- 这份计划承接 `2026-05-16-mini-games-porting-plan.md`，只覆盖第一阶段 `2048 vertical slice`，不覆盖贪吃蛇、小恐龙或 Flappy Bird 的实现。

## Solution

- 复用 GUI Guider 已生成的主屏 `Game` 卡片作为入口。
- 新增一个可独立测试的 2048 纯规则模块，UI 只消费规则模块公开的棋盘、分数和状态。
- 新增一个 hand-written LVGL controller 创建 2048 页面、处理触摸滑动、渲染棋盘，并把返回/暂停/重开行为收敛在 UI 线程。
- 新增或改造一个很薄的板载按键事件桥接：按键回调只把 `single_click / long_press` 投递到 FreeRTOS queue，`mini_games_controller_poll_ui()` 在 LVGL 线程非阻塞消费，避免按键回调直接触碰 `lv_obj_t`。

## User Stories

1. 作为手表用户，我想从主屏 `Game` 图标进入 2048，这样不需要新增额外菜单也能开始游戏。
2. 作为手表用户，我想用触摸滑动控制 2048 的上下左右移动，这样交互方式符合触摸屏直觉。
3. 作为手表用户，我想看到 4x4 棋盘、当前分数和游戏结束提示，这样我能判断游戏状态。
4. 作为手表用户，我想在游戏结束后快速重开，这样小游戏可以连续游玩。
5. 作为手表用户，我想用物理按键短按暂停或继续，这样触摸不方便时也能控制游戏节奏。
6. 作为手表用户，我想用物理按键长按退出游戏页，这样任何情况下都不会被困在小游戏页面。
7. 作为维护者，我想 2048 规则与 LVGL 页面分离，这样规则可以用 source/unit tests 验证，不依赖板端显示。
8. 作为维护者，我想小游戏不触碰 Wi-Fi、BLE、音频、危险识别和电源策略 owner，这样不会增加当前并发资源风险。
9. 作为维护者，我想游戏页二次进入不访问已删除 LVGL 对象，这样避免重复出现页面生命周期崩溃。

## Current Evidence

- `main/ui/generated/gui_guider.h` 已有 `screen_main_option_4` 和 `screen_main_Game`。
- `main/ui/generated/events_init.c` 已有 AI 和危险识别入口范式：点击主屏对象后调用 hand-written controller 的 `*_open()`。
- `main/ui/lvgl_task.c` 已集中初始化 UI controller，并在主循环中串行处理 UI poll、`lv_timer_handler()` 和 `ui_refresh_policy_poll()`。
- `components/lvgl_port/lv_port_input.c` 已在触摸按下时调用 `ui_refresh_policy_notify_touch()`，小游戏不需要改触摸底层。
- `main/app/hardware_init.c` 当前只注册 `BUTTON_LONG_PRESS_START` 日志，物理按键还没有可供 UI 消费的事件快照。
- `main/CMakeLists.txt` 已有 `feature_srcs` 和 `ui_runtime_srcs` 分组，适合分别登记规则模块与 UI controller。

## Implementation Decisions

- 规则模块：
  - 新增 `main/features/mini_games/mini_game_2048.[ch]`。
  - 只保存棋盘、分数、移动结果、游戏结束状态。
  - 不包含 `lvgl.h`，不调用硬件、系统服务或 NVS。
  - 随机新块第一版可使用轻量伪随机种子；若测试需要确定性，则公开 seed/reset 入口。
- UI controller：
  - 新增 `main/ui/custom/mini_games_controller.[ch]`。
  - 公开 `mini_games_controller_init(lv_ui *ui)`、`mini_games_controller_open(void)`、`mini_games_controller_poll_ui(void)`。
  - 页面首次进入时创建，退出时按现有 Wi-Fi/AI 页经验清理或延迟销毁，必须避免悬空 `lv_obj_t *`。
  - 触摸手势第一版用 `LV_EVENT_PRESSED / LV_EVENT_RELEASED` 计算滑动方向，避免依赖额外输入驱动。
- 主屏入口：
  - 修改 `events_init.c`，为 `screen_main_option_4` 和 `screen_main_Game` 绑定同一个 Game 点击 handler。
  - handler 只调用 `mini_games_controller_open()`。
- UI 主循环：
  - 修改 `lvgl_task.c`，初始化 `mini_games_controller`，并在主循环中调用 `mini_games_controller_poll_ui()`。
  - poll 只消费 FreeRTOS queue 中的按键事件和页面前台状态，不做阻塞工作。
- 物理按键桥接：
  - 优先新增窄文件 `main/app/board_button.[ch]`，由 `hardware_init.c` 调用初始化。
  - 注册 `BUTTON_SINGLE_CLICK` 和 `BUTTON_LONG_PRESS_START`。
  - 回调只通过 `xQueueSendToBack(..., 0)` 投递事件并调用 `ui_refresh_policy_notify_activity()` 或等价活跃通知，不直接修改 LVGL 对象。
  - 提供 `board_button_consume_event()` 和 `board_button_clear_events()` 之类的消费/清空接口，供 UI 线程轮询。
  - 注意：现有 `tests/test_nonblocking_boot_source.py` 禁止 `hardware_init.c` 出现 `BUTTON_SINGLE_CLICK`，因此新接口应把具体事件注册移出 `hardware_init.c`，避免重新变成配网入口语义。
- 构建登记：
  - `main/features/mini_games/mini_game_2048.c` 加入 `feature_srcs`。
  - `main/ui/custom/mini_games_controller.c` 加入 `ui_runtime_srcs`。
  - `main/app/board_button.c` 如采用该方案，则加入 `app_srcs`。

## Out of Scope

- 不实现贪吃蛇、小恐龙、Flappy Bird。
- 不实现小游戏列表或多游戏切换页。
- 不做最高分 NVS 持久化。
- 不做音效、震动、联网排行榜或 SD/资源包加载。
- 不修改 `sdkconfig`、分区表、显示/触摸驱动、Wi-Fi/BLE/音频主流程。
- 不新增后台常驻游戏 service，不新增 `ui_manager`、`resource_policy` 或事件队列。

## Milestones

### M0 - Execution Plan Gate

- 目标：把第一阶段 2048 的文件边界、入口、测试和验收标准固定下来。
- Gate：本文档存在于 `docs/context/plans/active/`，`docs/context/CHANGELOG.md` 有对应索引。
- 状态：已完成。

### M1 - 2048 Pure Rule Module

- 目标：先完成不依赖 LVGL 的 2048 规则模块。
- 预计文件：
  - `main/features/mini_games/mini_game_2048.h`
  - `main/features/mini_games/mini_game_2048.c`
- Gate：
  - source/unit test 覆盖初始化、移动、合并、得分、新块生成、game over。
  - 规则模块不包含 `lvgl.h`、不调用 service、driver 或 UI 头文件。
- 状态：已完成。

### M2 - Game Entry and LVGL Page Skeleton

- 目标：接通主屏 Game 图标，打开一个可返回的 2048 页面。
- 预计文件：
  - `main/ui/custom/mini_games_controller.h`
  - `main/ui/custom/mini_games_controller.c`
  - `main/ui/generated/events_init.c`
  - `main/ui/lvgl_task.c`
  - `main/CMakeLists.txt`
- Gate：
  - `screen_main_option_4` 和 `screen_main_Game` 都能路由到 `mini_games_controller_open()`。
  - 页面有标题、棋盘占位、分数、返回动作。
  - 二次进入不复用已删除对象。
- 状态：已完成。

### M3 - Touch Gesture and 2048 Rendering

- 目标：在 LVGL 页面中完成滑动输入和棋盘刷新。
- Gate：
  - 上下左右滑动分别调用规则模块移动。
  - 只有棋盘变化时才生成新块。
  - 游戏结束、重开和分数刷新可见。
  - 页面不创建高频 timer；2048 只在输入后刷新。
- 状态：已完成。

### M4 - Physical Button Bridge

- 目标：让物理按键短按暂停/继续，长按退出小游戏页。
- Gate：
  - 按键回调不直接触碰 LVGL 对象。
  - UI 线程非阻塞消费 FreeRTOS queue 事件。
  - 非小游戏页时按键事件不误触发 2048 行为。
  - 不恢复旧配网按键入口。
- 状态：已完成 source/build 验证；板端手动按键验收待执行。

### M5 - Integration and Board Readiness

- 目标：完成 source tests、context 校验和构建验证。
- Gate：
  - `uv run python -m pytest tests/test_mini_games_2048_source.py tests/test_mini_games_ui_source.py tests/test_nonblocking_boot_source.py`
  - `uv run python scripts/context/validate_context.py --level light --q "mini_games 2048 Game 入口 按键" --brief`
  - 确认 `export.ps1` 可用后运行 `idf.py build`。
  - 上板阶段：60s 日志无 `Display flush failed`、`ESP_ERR_NO_MEM`、panic、Guru Meditation、WDT。
- 状态：source tests、context light、`idf.py build`、COM3 app-flash 和 60s 启动日志已完成；触摸/短按/长按手动验收待执行。

## TaskNodes

## TaskNode: MG2048-T01

**Title:** 2048 规则模块

**Milestone:** M1 - 2048 Pure Rule Module

**Status:** done

**Expected Output:**
- 规则头文件和实现文件。
- 可从测试直接调用的初始化、移动、取格子、取分数、判断结束接口。

**Acceptance Criteria:**
- [ ] 同一块每步最多合并一次。
- [ ] 滑动无变化时不生成新块。
- [ ] 无空格且无可合并相邻块时进入 game over。
- [ ] 不包含 `lvgl.h` 或 UI/service/driver 头文件。

## TaskNode: MG2048-T02

**Title:** Game 入口绑定

**Milestone:** M2 - Game Entry and LVGL Page Skeleton

**Status:** done

**Depends On:**
- MG2048-T01

**Expected Output:**
- 主屏 `Game` 卡片和图片都绑定到小游戏入口。
- `lvgl_task` 初始化小游戏 controller。

**Acceptance Criteria:**
- [ ] `events_init.c` 包含 `mini_games_controller.h`。
- [ ] `screen_main_option_4` 和 `screen_main_Game` 都注册 Game handler。
- [ ] `lvgl_task.c` 调用 `mini_games_controller_init(&guider_ui)`。

## TaskNode: MG2048-T03

**Title:** 2048 页面骨架和触摸滑动

**Milestone:** M3 - Touch Gesture and 2048 Rendering

**Status:** done

**Depends On:**
- MG2048-T01
- MG2048-T02

**Expected Output:**
- 2048 页面、返回按钮、分数区域、4x4 棋盘。
- 触摸滑动识别和棋盘刷新。

**Acceptance Criteria:**
- [ ] 页面进入后可滑动移动棋盘。
- [ ] 返回主页后再次进入不崩溃。
- [ ] 游戏结束后可重开。
- [ ] 无持续高频 timer。

## TaskNode: MG2048-T04

**Title:** 物理按键事件桥接

**Milestone:** M4 - Physical Button Bridge

**Status:** done-source

**Expected Output:**
- 板载按键事件以 FreeRTOS queue 方式提供给 UI 线程。
- 小游戏页消费短按/长按。

**Acceptance Criteria:**
- [ ] 按键回调只发布事件，不直接改 LVGL。
- [ ] 短按暂停/继续，长按退出小游戏页。
- [ ] 不恢复 `hardware_init.c` 里的旧配网按键入口。

## TaskNode: MG2048-T05

**Title:** Source tests and build gate

**Milestone:** M5 - Integration and Board Readiness

**Status:** partial

**Depends On:**
- MG2048-T01
- MG2048-T02
- MG2048-T03
- MG2048-T04

**Expected Output:**
- 新增 source tests。
- 通过 context light 和 `idf.py build`。

**Acceptance Criteria:**
- [ ] 规则测试覆盖核心 2048 行为。
- [ ] source tests 锁定入口、CMake 登记、按键桥接边界。
- [ ] `idf.py build` 通过。

## Testing Decisions

- 规则模块测试应直接验证外部行为，不检查内部数组移动过程。
- UI source tests 只锁定关键契约：入口绑定、controller 初始化、CMake 源文件登记、按键回调不直接操作 LVGL。
- 继续保留现有 `test_nonblocking_boot_source.py` 的配网按键退场约束，防止物理按键重新变成隐藏配网入口。
- 板端验证不追求性能分数，只看能否进入、滑动、暂停、退出、二次进入和 60s 稳定性。

## Progress

- `[x]` 已确认入口：主屏已有 `Game` 图标位。
- `[x]` 已确认第一阶段范围：只做 2048。
- `[x]` 已确认不改显示/触摸底层和 `sdkconfig`。
- `[x]` 已实现规则模块：`main/features/mini_games/mini_game_2048.[ch]`。
- `[x]` 已实现入口绑定：`screen_main_option_4` 与 `screen_main_Game` 调用 `mini_games_controller_open()`。
- `[x]` 已实现 UI 页面：2048 标题、分数、状态、返回、新局、暂停按钮、4x4 棋盘和触摸滑动。
- `[x]` 已实现按键桥接：`main/app/board_button.[ch]` 使用 FreeRTOS queue 发布按键事件，UI 线程非阻塞消费短按/长按。
- `[x]` 已跑 source tests、context light 和 `idf.py build`。
- `[x]` 已完成板端 COM3 app-flash 与 60s 启动日志验收。
- `[ ]` 未完成触摸滑动、短按暂停/继续、长按退出的手动交互验收。

## Decision Log

- 2026-06-02：
  - 决策：第一阶段不做“小游戏列表”，直接让 Game 入口进入 2048。
  - 原因：当前目标是验证小游戏能力闭环；列表页会增加页面生命周期和导航复杂度，但不增加第一款游戏可玩性。
- 2026-06-02：
  - 决策：2048 规则做成深模块，LVGL controller 只做输入和渲染。
  - 原因：规则行为稳定、测试价值高；UI 层后续可复用同一入口扩展贪吃蛇。
- 2026-06-02：
  - 决策：物理按键桥接从 `hardware_init.c` 拆出去。
  - 原因：现有测试明确防止 `hardware_init.c` 重新承担配网按键语义；独立 `board_button` 更容易保持“只发布事件”的窄职责。
- 2026-06-02：
  - 决策：按用户确认保留“触摸滑动 + 物理短按/长按”的首版交互。
  - 原因：触摸负责核心移动，物理按键提供暂停/退出兜底，符合单按键手表交互。
- 2026-06-02：
  - 决策：物理按键桥接从 `volatile` pending bitmask 重构为 FreeRTOS queue。
  - 原因：短按/长按是跨上下文边沿事件，queue 能保留事件顺序并避免读改写竞态；`board_button_clear_events()` 用于丢弃非小游戏页产生的旧事件，防止进入 2048 后误暂停或误退出。

## Surprises & Discoveries

- `Game` 主屏对象已经存在，只缺事件绑定。
- 触摸活动通知已经在 `lvgl_port` 输入回调里完成，小游戏不需要改触摸驱动。
- 物理按键当前只有长按日志，且 handle 是局部变量；要给 UI 用，必须新增一个可消费的事件桥接，而不是从 UI 层直接拿 handle。
- `COM3` 当前枚举为 USB 串行设备，可作为后续 `idf.py -p COM3 app-flash` 和限时 monitor 的候选端口。

## Validation and Acceptance

- 计划运行的验证命令：
  - `uv run python -m pytest tests/test_mini_games_2048_source.py tests/test_mini_games_ui_source.py tests/test_nonblocking_boot_source.py`
  - `uv run python scripts/context/validate_context.py --level light --q "mini_games 2048 Game 入口 按键" --brief`
  - 确认 `export.ps1` 可用后运行 `idf.py build`
- 期望看到的结果：
  - 所有 source tests 通过。
  - context light 无重复旧方案或路由警告。
  - `idf.py build` 通过。
  - 上板 60s 无显示 flush、NO_MEM、panic、Guru 或 WDT。
- 当前实际结果：
  - `uv run python -m pytest tests/test_mini_games_2048_source.py tests/test_mini_games_ui_source.py tests/test_nonblocking_boot_source.py`：15 passed。
  - `uv run python scripts/context/validate_context.py --level light --q "mini_games 2048 Game 入口 按键" --brief`：完成，命中按键配网边界与当前 owner 文档。
  - `. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`：通过，`111.bin` 大小 `0x983870`，最小 app 分区剩余 `0x7c790`，约 5%。
  - `./scripts/board/agent_serial_monitor.ps1 -Port COM3 -Action app-flash-monitor -DurationSeconds 60 -FlashTimeoutSeconds 180 -Tag mini-games-2048 -QuietConsole -LiteralPattern @('boot_stage: ui_first_frame_ready','boot_stage: startup_sequence_done')`：完成，日志 `board_logs/2026-06-02-01-36-57-mini-games-2048.log`，摘要 `board_logs/2026-06-02-01-36-57-mini-games-2048.summary.json`。
  - 板端日志确认 `startup_sequence_done`、`ui_first_frame_ready`，fatal 扫描中 `Guru Meditation / panic / abort / ESP_ERR_NO_MEM / watchdog / checksum mismatch` 均为 0。
  - `Select-String` 未命中 `Display flush failed`、`ESP_ERR_NO_MEM`、`Guru Meditation`、`panic`、`WDT`、`watchdog`。
  - FreeRTOS queue 重构后，`uv run python -m pytest tests/test_mini_games_2048_source.py tests/test_mini_games_ui_source.py tests/test_nonblocking_boot_source.py`：15 passed。
  - FreeRTOS queue 重构后，`. D:\esp-idf\v5.5.3\esp-idf\export.ps1; idf.py build`：通过，`111.bin` 大小 `0x9839f0`，最小 app 分区剩余 `0x7c610`，约 5%。
  - 滑动/短按/长按手动验收尚未执行。

## Idempotence and Recovery

- 如果中途中断，下次从手动交互验收开始：进入 Game，确认触摸上下左右移动、物理短按暂停/继续、物理长按退出。
- 如果 UI 页面崩溃，保留规则模块，回退入口绑定和 controller。
- 如果物理按键桥接引入歧义，先保留触摸可玩与屏幕返回按钮，暂缓按键消费。
- 最小回退路径：删除新增 `mini_games` / `board_button` 文件，撤销 `events_init.c`、`lvgl_task.c`、`main/CMakeLists.txt` 中的小游戏相关行。

## Next Step

- 下一步最小动作：在板子上手动验证触摸滑动、短按暂停/继续、长按退出；若需要证据，可保持 monitor 打开并观察是否出现 `2048 screen created` 或 fatal 日志。
