---
id: watchface-emoji-root-ui-plan
tags: context, plans, ui, lvgl, watchface, emoji, hermes, resources, littlefs, rgb565, agent-preview
summary: 表情表盘根首页执行计划：用 GIF 离线分帧生成 LVGL RGB565 bin 资源，放入资源分区/LittleFS，由新的根首页按 Hermes 状态全屏播放表情包。
last_reviewed: 2026-06-30
memory_type: task
scope: task
owners: docs/context/plans/active/2026-06-30-watchface-emoji-root-ui-plan.md, main/ui/custom, main/ui/agent_preview, scripts
triggers: 表盘, watchface, 根首页, 屏幕亮起, 表情包, Hermes 联动, GIF 分帧, LVGL bin, RGB565, resources, LittleFS
evidence_level: design
status: active
---

# 表情表盘根首页执行计划

## 目标与全局

- 任务目标：新增一个“表情表盘”作为手表新的根首页，屏幕亮起先看到全屏表情包；现有功能主界面保留为第二层入口。
- 为什么现在做：当前主入口更像功能菜单，不够像手表第一屏；用户已有一组 Hermes 状态表情包，适合把 Hermes 的后台状态和主动提示变成更直观的表盘人格。
- 完成后用户会看到什么变化：亮屏先显示全屏动态表情，小时间轻量浮在上方；点表情进入现有功能主界面，左滑直接进入 Hermes，有新提示或失败时表情自动变化。

## 已敲定产品口径

- 表情表盘是新的根首页，不是临时锁屏层，也不是只属于 Hermes 页面的装饰。
- 表情包全屏 cover 覆盖 `410x502`，允许边缘被物理圆角裁切。
- 表盘只显示很小的时间，尽量不挡表情包；不常驻日期、天气、电量或功能说明。
- 点表情进入当前功能主界面。
- 左滑进入 Hermes 语音页。
- 有 Hermes 主动提示时点表情/提示进入 Hermes 收件箱。
- 子页面返回优先回到表情表盘。

## 资源路线

本计划不启用 `LV_USE_GIF` 作为板端主路径。

原因：

- 当前 `sdkconfig` 已支持 `LV_USE_IMAGE`、`LV_USE_LODEPNG`、`LV_COLOR_DEPTH=16`，但 `LV_USE_GIF` 未启用。
- 全屏 GIF 运行时解码会持续占用 CPU、RAM 和刷新预算，容易放大主界面帧率不稳的问题。
- 根首页是长期停留页面，应优先把运行时压力从 ESP32-S3 挪到离线资源生成阶段。

最终路线：

```text
D:\esp32S3\表情包\*.gif
  -> 离线抽帧 / 限帧 / cover 到 410x502
  -> 生成 LVGL RGB565 bin 帧
  -> 放入 resources/LittleFS 路径
  -> watchface 播放器按状态切换帧文件
```

关键约束：

- 不把全屏大帧作为 C 数组塞进 app 分区。
- UI 代码只依赖状态名和资源路径，不直接依赖原始 GIF 文件名。
- 换表情包时优先重新运行资源生成脚本并刷新资源分区，不改 UI 逻辑。
- V1 不做差分帧、精灵图大包、自定义压缩容器；如果 bin 帧体积太大，再单独评估 RLE/LZ4/差分。

## 表情状态映射

当前素材目录：

```text
D:\esp32S3\表情包\空闲.gif
D:\esp32S3\表情包\思考.gif
D:\esp32S3\表情包\工作中.gif
D:\esp32S3\表情包\来信息.gif
D:\esp32S3\表情包\失败.gif
D:\esp32S3\表情包\随机.gif
D:\esp32S3\表情包\愤怒.gif
```

V1 状态映射：

- `idle`：空闲.gif
- `thinking`：思考.gif，用于 Hermes 已收到请求、等待 ASR/回复或正在理解。
- `working`：工作中.gif，用于 Hermes 后台任务执行中。
- `message`：来信息.gif，用于 Hermes 主动提示或收件箱未读。
- `error`：失败.gif，用于 Hermes 离线、配置错误、请求失败或超时。
- `random`：随机.gif，作为后续 idle 变化素材，V1 可先不接。
- `angry`：愤怒.gif，作为后续彩蛋或特殊错误状态，V1 可先不接。

优先级建议：

```text
message > error > working > thinking > idle
```

说明：有新提示时优先让用户看到“来信息”；如果同时处于失败和普通等待，失败优先于等待；没有事件时回到空闲。

## UI 与 owner 边界

推荐新增窄模块：

- `watchface_view`：只负责 LVGL 对象、全屏图片帧、时间 label、点击/滑动事件。
- `watchface_controller`：只负责读取时间/Hermes/收件箱/网络状态快照，计算 `watchface_state`，驱动 view。
- `watchface_assets` 或资源 manifest：记录状态名、帧数量、帧间隔和资源路径。

边界：

- `watchface_view` 不直接联网，不直接调用 Hermes API，不直接读取 NVS token。
- Hermes 状态继续以 `memory_watch_service` / controller 已有快照为事实源。
- 收件箱未读继续来自现有 Hermes inbox/sync 机制，不把普通 Hermes 对话回复塞进收件箱。
- 表盘作为根首页只负责入口和状态呈现，不成为新的后台服务 owner。

## 导航计划

目标导航：

```text
screen on / app boot
  -> watchface

watchface tap
  -> current function main screen

watchface swipe left
  -> Hermes voice page

watchface tap while message state
  -> Hermes inbox

subpage back
  -> watchface
```

实现时优先避免直接大改 GUI Guider 生成文件；若必须替换启动 screen，应从现有 UI 初始化入口或 controller 层接管首屏加载。

## 模拟器策略

- 先在 `main/ui/agent_preview` 做可点击原型：表盘、点进功能页、左滑进 Hermes、状态切换。
- host preview 可以用同一套 bin 帧资源或临时 PNG 帧资源；不要因为 host 方便而把板端路线改回运行时 GIF。
- UI 修改后必须运行 host build 和截图脚本，按仓库规则附带截图证据。

## 进度

- `[x]` 产品口径敲定：新根首页、全屏表情、只显示小时间、点击进功能页、左滑进 Hermes。
- `[x]` 资源路线敲定：GIF 离线分帧，生成 LVGL RGB565 bin，放资源分区/LittleFS。
- `[ ]` 确认当前 resources/LittleFS 到 LVGL image path 的真实读取链路。
- `[ ]` 设计资源 manifest 格式和生成脚本输入输出。
- `[ ]` 实现 host preview 表盘原型。
- `[ ]` 接入正式 UI 根首页导航。
- `[ ]` 接入 Hermes/inbox 状态快照。
- `[ ]` 板端构建、资源刷写和帧率验证。

## 决策记录

- 2026-06-30：用户确认表盘做成新的根首页，而不是单独 Hermes 页面或临时锁屏层。
- 2026-06-30：用户确认交互：点表情进入现有功能主界面，左滑进入 Hermes。
- 2026-06-30：用户要求只显示很小的时间，尽量不遮挡表情包。
- 2026-06-30：用户要求表情包全屏覆盖。
- 2026-06-30：确认不直接使用 `LV_USE_GIF` 作为板端主路线；采用 GIF 离线分帧到 LVGL RGB565 bin，并放入 resources/LittleFS，兼顾后续换 GIF 和板端稳定性。

## 意外与发现

- 当前 `sdkconfig` 中 `CONFIG_LV_USE_GIF` 未启用，`CONFIG_LV_USE_LODEPNG` 已启用，屏幕色深为 `RGB565`。
- PNG 帧虽然更容易调试，但全屏动画会在运行时重复解压和色彩转换；RGB565 bin 帧更占存储，但播放路径更轻、更稳。

## 验证与验收

计划运行的验证命令：

```powershell
uv run python -m unittest tests.test_*watchface*
& D:\esp32S3\111\main\ui\agent_preview\scripts\build_apple_watch_s5_preview.ps1
& D:\esp32S3\111\main\ui\agent_preview\scripts\capture_apple_watch_s5_preview.ps1 -OutputPath D:\esp32S3\111\main\ui\agent_preview\artifacts\watchface-preview.png
```

正式 UI C 文件落地后：

```powershell
. D:\esp-idf\v5.5.3\esp-idf\export.ps1
idf.py build
```

若资源分区或 LittleFS 内容发生变化，板端验证不能只刷 app；需要按当次资源落点刷新对应资源分区。

期望看到的结果：

- 模拟器首屏为全屏表情表盘。
- 小时间不遮挡主要表情。
- 点表情进入现有功能主界面。
- 左滑进入 Hermes。
- Hermes/inbox mock 状态变化时表情状态按优先级切换。
- 板端无明显触摸卡顿、无 LVGL image decode 错误、无资源路径找不到错误。

当前实际结果：

- 仅完成产品与技术路线规划，尚未实现代码。

## 幂等与恢复

- 如果中途中断，下次先从“确认当前 resources/LittleFS 到 LVGL image path 的真实读取链路”继续。
- 如果 RGB565 bin 体积超出资源分区预算，先降低帧数和帧率，再评估缩放尺寸；不要立刻回退到运行时 GIF。
- 如果 host preview 与板端资源路径不一致，优先增加薄的资源路径适配层，不让 UI 逻辑分叉。

## 下一步

- 下一步最小动作：查清当前资源分区/LittleFS 是否已有 LVGL image file decoder 路径；如果已有，设计资源 manifest；如果没有，先补最小只读资源路径适配。
