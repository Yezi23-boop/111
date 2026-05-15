---
id: mini-games-porting-plan
tags: context, plans, task-memory, mini-games, lvgl, touch, button, watch
summary: 将当前手表固件可移植的经典小游戏收敛为分阶段计划，先以 2048 验证统一输入、页面生命周期和低资源 UI，再推进贪吃蛇、小恐龙和 Flappy Bird。
last_reviewed: 2026-05-16
memory_type: task
scope: repo
owners: main/ui/custom, main/features, main/ui/lvgl_task.c, main/ui/ui_refresh_policy.c
triggers: 小游戏, 2048, 贪吃蛇, 小恐龙, Flappy Bird, mini_games, games, game_page
evidence_level: design
status: active
---

# 手表小游戏移植计划

## Purpose / Big Picture

- 任务目标：在当前 `ESP32-S3 + LVGL 9.3 + 410x502 AMOLED + 单按键 + 触摸屏` 固件里，先做一套轻量小游戏入口和第一款可玩游戏，再按资源风险逐步扩展。
- 为什么现在做：2048、贪吃蛇、小恐龙和 Flappy Bird 都适合小屏休闲场景，但它们的刷新节奏不同；先把输入、暂停、退出、页面生命周期和验证口径固定下来，后续实现不会各走一套。
- 完成后用户会看到什么变化：主 UI 可以进入“小游戏”页，第一阶段先可玩 2048；后续按同一框架增加贪吃蛇、小恐龙和 Flappy Bird。

## Scope / Non-Goals

- 本计划明确要做：
  - 先落地统一小游戏入口、输入约定、状态机和退出恢复路径。
  - 第一款游戏选 `2048`，作为最低资源风险的 vertical slice。
  - 第二款游戏选 `贪吃蛇`，验证固定 tick、碰撞、分数和游戏结束。
  - 第三批再评估 `小恐龙` 与 `Flappy Bird`，验证 33-50ms 动画节奏和更频繁的碰撞判定。
  - 优先使用 host/LVGL 预览确认布局和基础交互，再做板端构建、进入/退出和长时间运行验证。
- 本计划明确不做：
  - 不新增后台常驻游戏 service。
  - 不引入新的 `ui_manager`、桥接层或 UI 事件队列。
  - 不为小游戏修改 `sdkconfig`、分区表、启动流程、Wi-Fi/BLE/音频主链路。
  - 第一版不上音效、大图、GIF、排行榜、联网同步或 SD 卡资源包。
  - 不让小游戏反向控制 `power_policy`、`background_service_manager` 或危险识别 owner。

## Current Resource Assumptions

- 当前 `sdkconfig` 已启用 `CONFIG_SPIRAM=y`，Octal PSRAM 80MHz，LVGL 使用 `RGB565`，默认刷新周期 `33ms`。
- 当前触摸链路是 LVGL pointer 输入，默认单点、轮询读取；小游戏交互应优先用滑动、点按和长按。
- 当前真实风险不是 2048/贪吃蛇的游戏逻辑算力，而是 `audio + sd + wifi + lvgl` 并发时内部 DMA/片内内存压力。
- 因此小游戏第一版应少对象、少动画、少图片，避免高频全屏重绘；动态游戏 tick 要可暂停、可退出、可降频。

## Input and Lifecycle Contract

- 触摸滑动：主方向输入。
- 触摸点按：开始、确认、旋转或轻动作，按具体游戏解释。
- 触摸长按：仅用于辅助动作；第一版 2048 可以不用。
- 物理按键短按：暂停/继续；在 `READY` 或 `GAME_OVER` 状态可作为开始/重开。
- 物理按键长按：退出小游戏页，返回上一级页面。
- 统一游戏状态：`READY -> RUNNING -> PAUSED -> GAME_OVER`，退出时进入短暂 `EXITING` 清理路径。
- UI 对象只允许在 LVGL 线程内创建和修改；游戏逻辑可以是纯 C 状态，但不得从后台任务直接触碰 `lv_obj_t`。
- 页面删除时必须停止 LVGL timer、清空 cached object 指针，并避免二次进入时访问已删除对象。

## Architecture Boundary

- 建议逻辑 owner：
  - `main/features/mini_games/`：纯游戏状态、规则、分数、输入归一化，不包含硬件调用。
  - `main/ui/custom/mini_games_controller.[ch]`：小游戏入口、页面切换、按键/触摸事件分发。
  - `main/ui/custom/mini_games_*.c`：各游戏 LVGL 页面和轻量绘制。
- 调用方向：
  - UI 事件进入 controller。
  - controller 调用当前游戏逻辑更新状态。
  - controller 在 LVGL 线程内刷新页面对象。
- 禁止方向：
  - 游戏逻辑不得调用 Wi-Fi、BLE、音频、危险识别、电源策略或 LCD/Touch driver。
  - 后台 service 不得主动修改小游戏页面对象。

## Milestones

### M0 - Plan Gate

- 目标：固定小游戏移植顺序、输入契约、资源边界和验证口径。
- Gate：本文档存在于 `docs/context/plans/active/`，`docs/context/CHANGELOG.md` 有对应索引。
- 状态：已完成。

### M1 - Mini Games Shell and 2048 Vertical Slice

- 目标：完成小游戏入口、统一暂停/退出路径、2048 规则和 UI。
- 推荐 tick：2048 不需要常驻 tick，只在输入后更新。
- 验收：
  - 2048 可开始、滑动、合并、判输、重开、暂停、退出。
  - 进入/退出小游戏页两次不崩溃，无悬空 LVGL 指针。
  - host 预览或板端截图确认 410x502 布局可读。
  - 板端运行 60s 无 `Display flush failed`、`ESP_ERR_NO_MEM`、panic、Guru Meditation 或 WDT。

### M2 - Snake Tick Game

- 目标：在同一入口和生命周期下增加贪吃蛇，验证定时推进型游戏。
- 推荐 tick：100-180ms，暂停和退出时必须停止 timer。
- 验收：
  - 滑动换方向，禁止 180 度立即反向。
  - 分数、食物生成、撞墙/撞身结束可见。
  - 与 2048 共用暂停、退出、重开口径。

### M3 - Runner and Flappy Evaluation

- 目标：在 2048 与贪吃蛇稳定后，再选择小恐龙或 Flappy Bird 作为动画型游戏。
- 推荐 tick：33-50ms；优先从小恐龙开始，因为碰撞和手感更容易稳定。
- 验收：
  - 游戏页可在后台 Wi-Fi/音频空闲时稳定运行。
  - 动画不要求满帧，但不能拖慢整机 UI 响应。
  - 如果出现 DMA/internal memory 压力，先降 tick、减少对象和减少全屏重绘，不修改显示底层参数。

## TaskNodes

## TaskNode: M1-T01

**Title:** 小游戏入口与输入契约

**Milestone:** M1 - Mini Games Shell and 2048 Vertical Slice

**Status:** planned

**Expected Output:**
- 小游戏入口页或入口按钮。
- 短按暂停/继续、长按退出的统一处理。
- 触摸滑动方向识别。

**Acceptance Criteria:**
- [ ] 入口可进入和返回，不影响主页面。
- [ ] 按键和触摸事件不会被单个游戏私有化。
- [ ] 页面删除时 timer 和 cached object 清理完整。

## TaskNode: M1-T02

**Title:** 2048 规则和页面

**Milestone:** M1 - Mini Games Shell and 2048 Vertical Slice

**Status:** planned

**Depends On:**
- M1-T01

**Expected Output:**
- 4x4 棋盘、数字块、分数、最高分占位、游戏结束和重开。
- 规则逻辑与 LVGL 页面分离，便于 source test。

**Acceptance Criteria:**
- [ ] 滑动后只在棋盘实际变化时生成新块。
- [ ] 合并规则符合经典 2048，同一块每步最多合并一次。
- [ ] 无可移动格时进入 `GAME_OVER`。

## TaskNode: M2-T01

**Title:** 贪吃蛇规则和页面

**Milestone:** M2 - Snake Tick Game

**Status:** planned

**Depends On:**
- M1-T01

**Expected Output:**
- 固定网格、蛇身、食物、分数和结束状态。
- timer 推进与暂停/退出联动。

**Acceptance Criteria:**
- [ ] tick 停止后页面无继续移动。
- [ ] 触摸滑动只改变下一步方向，不阻塞 LVGL。
- [ ] 退出后再次进入状态重新初始化。

## TaskNode: M3-T01

**Title:** 小恐龙优先评估

**Milestone:** M3 - Runner and Flappy Evaluation

**Status:** planned

**Depends On:**
- M2-T01

**Expected Output:**
- 一键/点按跳跃、障碍滚动、分数递增、失败重开。

**Acceptance Criteria:**
- [ ] 33-50ms tick 下 UI 仍可响应暂停和长按退出。
- [ ] 不使用大图资源也能看清角色和障碍。

## TaskNode: M3-T02

**Title:** Flappy Bird 评估

**Milestone:** M3 - Runner and Flappy Evaluation

**Status:** planned

**Depends On:**
- M2-T01

**Expected Output:**
- 点按/按键上跳、重力下落、管道碰撞、分数。

**Acceptance Criteria:**
- [ ] 手感可调参数集中，不散落在 UI 绘制代码里。
- [ ] 与小恐龙二选一或先后实现由 M3 实测结果决定。

## Progress

- `[x]` 已确认候选游戏：2048、贪吃蛇、小恐龙、Flappy Bird。
- `[x]` 已确认第一优先级：2048。
- `[x]` 已确认输入条件：一个物理按键 + 触摸屏。
- `[x]` 已通过 context standard 验证。
- `[ ]` 未实现小游戏入口。
- `[ ]` 未实现 2048。
- `[ ]` 未做 host 预览或板端验证。

## Decision Log

- 2026-05-16：
  - 决策：第一款做 2048，第二款做贪吃蛇，小恐龙和 Flappy Bird 放到动画型游戏评估阶段。
  - 原因：2048 与贪吃蛇资源压力低，最适合先验证页面生命周期、触摸/按键输入和 LVGL timer；小恐龙与 Flappy Bird 刷新更频繁，应在基础框架稳定后接入。
- 2026-05-16：
  - 决策：小游戏不新增后台 service，不改 `sdkconfig`，不接入音频和联网。
  - 原因：当前仓库已经有 UI、音频、Wi-Fi、危险识别、电源策略并发资源压力，小游戏第一版应作为前台轻量 UI 功能。

## Surprises & Discoveries

- 当前资源足以支持这四类小游戏；主要风险来自显示刷新和系统并发资源，而不是游戏规则逻辑。
- 2048 不需要持续 tick，是最适合做第一版验证的游戏。
- 小恐龙和 Flappy Bird 都能做，但需要在 M3 里用实际 tick、对象数量和板端日志决定先后。

## Validation and Acceptance

- 计划运行的验证命令：
  - `uv run python scripts/context/validate_context.py --level standard --q "小游戏 2048 贪吃蛇 小恐龙 Flappy Bird LVGL 手表" --brief`
  - 实现阶段补对应 source tests，例如 2048 规则、页面生命周期、入口路由和按键/触摸事件扫描。
  - 实现阶段在确认 `export.ps1` 可用后运行 `idf.py build`。
- 期望看到的结果：
  - context standard 无错误。
  - source tests 证明规则逻辑、状态转换和 UI 生命周期不回退。
  - 板端日志无 `Display flush failed`、`ESP_ERR_NO_MEM`、panic、Guru Meditation、WDT。
- 当前实际结果：
  - 已完成计划文档和 `docs/context/CHANGELOG.md` 索引。
  - `uv run python scripts/context/validate_context.py --level standard --q "小游戏 2048 贪吃蛇 小恐龙 Flappy Bird LVGL 手表" --brief` 通过，错误 0，警告 0。
  - 尚未实现代码、host 预览和板端验证。

## Idempotence and Recovery

- 如果中途中断，下次从 `M1-T01` 继续，先做入口和输入契约，再做 2048。
- 如果 2048 页面出现 LVGL 生命周期问题，先回退小游戏入口挂接，保留纯规则逻辑测试。
- 如果动态游戏触发显示或 DMA 资源问题，先降 tick、减少对象和动画，再考虑暂缓小恐龙/Flappy Bird。
- 最小回退路径：移除小游戏入口路由和新增 `mini_games` 文件，不触碰显示、触摸、Wi-Fi、音频、危险识别和电源主链路。

## Next Step

- 下一步最小动作：实现 `M1-T01` 小游戏入口与输入契约，并用一个空白/占位 2048 页面验证进入、暂停、长按退出和二次进入生命周期。
