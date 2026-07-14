---
id: watchface-emoji-root-ui-plan
tags: context, plans, ui, lvgl, watchface, emoji, hermes, animation, resources, littlefs, sdcard, psram, rgb565, rgb565a, agent-preview
summary: 表情表盘根首页重执行计划：代码已回退，后续从干净架构重做。V1 就要保留动画效果；raw animation 已实测超过 8M 阈值，当前执行路线固定为 SD 卡 `/sdcard/watchface` + PSRAM 帧缓存 + LVGL 内存图渲染。LVGL 绘制时必须使用已预加载到 PSRAM 的 lv_image_dsc_t，禁止全屏表盘在 draw 阶段直接流式读取文件。
last_reviewed: 2026-07-01
memory_type: task
scope: task
owners: docs/context/plans/active/2026-06-30-watchface-emoji-root-ui-plan.md, main/ui/custom, tools/ui_preview, scripts/watchface, sdcard/watchface, components/sd_card
triggers: 表盘, watchface, 根首页, 屏幕亮起, 表情包, Hermes 联动, 动画, GIF 分帧, resources, LittleFS, SD卡, RGB565, RGB565A, PSRAM image cache
evidence_level: design
status: active
---

# 表情表盘根首页重执行计划

## 当前结论

本计划按“代码已经回退”后的状态重新规划。上一轮表盘实现不作为继续修补对象，只保留已经确认的产品口径、资源方向和板端事故教训。

当前仓库基线：

- `main/ui/custom/watchface_view.c` 已不存在，正式 UI 表盘代码应从零重新落地。
- `scripts/watchface/` 目录仍存在；表情预览帧默认输出到 `sdcard/watchface/frames/`，不再放进 `resources/`。
- `resources/watchface/` 已删除，避免 LittleFS `resources` 镜像超过 4MiB 分区并触发 `LFS_ERR_NOSPC`。
- `partitions.csv` 当前 `resources` 分区为 `4M`；raw animation 已实测超过 `8M` 阈值，本轮不再扩大 resources 分区来塞表盘动画。
- 工程已有 SD 卡组件 `components/sd_card/sd_manager.c`，挂载点为 `/sdcard`，走 `SPI3_HOST`；当前表盘动画资源走 SD 卡路径 `/sdcard/watchface`。
- 电脑端 SD 卡目录已准备为 `E:\watchface`，对应板端挂载后的 `/sdcard/watchface`。
- `sdkconfig` 当前有其它未提交改动，表盘 V1 不应顺手混入无关配置修改。

上一轮板端关键事故：

- 旧路线让 LVGL 在绘制全屏表盘时直接从 `A:/watchface/*.bin` 读取 LittleFS/flash 文件。
- 真机日志出现 `Guru Meditation Error: Core 0 panic'ed (Cache error). Cache disabled but cached memory region accessed`。
- 回溯落在 `i2c_isr_receive_handler` 与 `esp_partition_read -> littlefs_esp_part_read -> lv_fs_read -> lv_bin_decoder_get_area -> lv_draw_image` 交叠路径。
- 判断：Wi-Fi 只是触发时序，根因是 LVGL draw 阶段流式读 flash/LittleFS 大图时撞上 cache-disabled/ISR 约束。`CONFIG_I2C_ISR_IRAM_SAFE=y` 已经开启，不能把后续修复押在“再开 I2C IRAM safe”上。

后续核心原则：

```text
当前资源文件放 SD 卡 `/sdcard/watchface`
电脑端拷贝目录为 `E:\watchface`
LVGL draw 阶段不能直接流式读取文件系统里的全屏表盘文件
活动帧必须先加载/复制/解码到内存，优先 PSRAM
watchface_view 只使用内存 lv_image_dsc_t 渲染
```

## 产品目标

- 表情表盘是屏幕亮起/唤醒或 UI 启动后的第一视觉入口。
- 表情包全屏覆盖 `410x502`，并且 V1 就要有动画效果，允许被物理圆角自然裁切。
- 表盘不叠加时间、状态文字或其它常驻 UI 元素，让表情动画完整占据第一视觉。
- 点表盘进入现有功能主界面。
- 左滑始终进入 Hermes 语音页，不因未读或 `message` 状态改跳收件箱。
- Hermes 有主动提示时，表情切到来信息状态；点表盘仍然进入现有功能主界面。
- 子页面返回走旧主界面；表盘是屏幕亮起/启动后的第一视觉入口，不作为所有返回动作的唯一目标。

## 本轮不做

- 不恢复上一轮已回退的 `watchface_view` 代码。
- 不做 `205x251` 半尺寸拉伸到 `410x502` 的降级方案。
- 不让 `lv_image_set_src()` 在表盘 draw 路径直接指向 `"/sdcard/watchface/..."`、`"A:/watchface/..."` 或任何文件路径。
- 不在 `watchface_view` 的 timer/getter 中做 JSON 解析、文件 IO、网络 IO 或 Hermes API 调用。
- 不把模拟器物理圆角遮罩写进正式业务 UI。
- 不把全局气泡/通知弹层做成表盘内容；它们属于顶层 overlay，可以临时覆盖表盘。
- 不把全屏表情帧作为巨型 C 数组塞进 app 分区。
- 不再继续 resources/LittleFS 表盘动画路线；raw animation 已超过 `8M` 阈值，当前直接走 SD 卡。
- 不在没有测量结果和验证计划时扩大 `resources` 分区。
- V1 不做完整 GIF 运行时解码、不启用 `LV_USE_GIF` 作为板端主路线。
- V1 不做高帧率全屏动画，不追求原 GIF 每一帧完整复刻。
- V1 不在 LVGL timer 中读文件或解码图片；timer 只允许切换已经准备好的内存帧。
- V1 不依赖 LVGL 文件系统直读路径；表盘缓存层从 SD 卡 `/sdcard/watchface` 读取资源后再给 LVGL 内存图。
- V1 不做表盘按下视觉反馈、缩放、暂停或变暗效果；只处理点击进入主界面入口页和左滑进入 Hermes。

## UI 架构

推荐按 4 个窄模块落地：

```text
watchface_assets
  静态资源 manifest：状态、帧数、帧间隔、文件路径、尺寸、颜色格式

watchface_anim_assets
  静态资源 manifest：状态、动画包路径、帧数、帧间隔、尺寸、颜色格式

watchface_anim_cache
  从 SD 卡 /sdcard/watchface 读取动画资源
  校验包头、帧表、尺寸、颜色格式
  把当前帧/下一帧解码到 PSRAM 帧槽
  暴露 lv_image_dsc_t 给 view

watchface_view
  纯 LVGL 对象树
  全屏 image
  点击和左滑事件
  只消费已经 ready 的 lv_image_dsc_t，不直接读文件系统

watchface_controller
  读取时间、Hermes、inbox、网络等快照
  计算 watchface_state
  驱动 view 切状态、更新时间
```

职责边界：

- `watchface_view` 不直接联网，不直接读 NVS token，不直接调用 Hermes service/API。
- `watchface_controller` 不拥有 Hermes 状态，只读现有 service/controller 快照。
- `watchface_anim_cache` 只做资源加载、解码和内存描述符管理，不做导航或产品状态判断。
- `watchface_anim_cache` 依赖 `sd_manager` 已挂载事实，但不负责初始化 SD 硬件。
- 表盘是根首页和状态呈现层，不成为新的后台 service owner。

## 动画缓存策略

V1 就做动画，但采用“低帧率 + 预解码 + PSRAM 帧槽”的稳妥路线：

- 每个状态是一段短循环动画，例如 `/sdcard/watchface/idle.rawanim`、`/sdcard/watchface/thinking.rawanim`，但该路径只允许被 cache 层读取，不允许直接交给 LVGL draw。
- 首版目标帧率为 `6 fps`，上限先按 `8 fps` 评估；不要从 GIF 原帧率无脑照搬。
- 每个状态首版建议抽 `8-16` 帧，优先保留表情变化关键帧。
- `watchface_anim_cache` 默认维护 4 个 PSRAM 帧槽：`current`、`next`、`decode`、`standby`；低内存时可降到 3 个，高余量且动画明显卡顿时再升到 6 个。
- 后台 load/decode worker 只负责读 SD 卡 `/sdcard/watchface` 动画资源并加载/解码到 PSRAM 帧槽。
- LVGL timer 只在下一帧 ready 后执行 `lv_image_set_src(image, &frame_dsc)` 切帧，不做文件 IO 和解码。
- 如果下一帧没有 ready，就继续显示当前帧，宁可掉帧也不要阻塞 UI。
- 状态切换时先保留当前动画帧，等新状态首帧解码完成后再切过去，避免黑屏。
- 如果资源缺失、读取失败、解码失败或 PSRAM 分配失败，保留上一帧，并把状态降级到 `error` 或 `idle`；冷启动不等待首帧，表盘资源未 ready 时直接回到现有主界面入口页，不做内置兜底表盘，不阻塞系统启动。
- 如果系统已经因为表盘资源未 ready 进入现有主界面入口页，后续资源加载完成也不要自动跳回表盘，避免打断用户操作；只在下一次亮屏、返回根页或用户主动返回时重新尝试显示表盘。

缓存边界：

- 单帧 RGB565 大小约 `410 * 502 * 2 = 411,640 bytes`。
- V1 默认 4 个帧槽约 1.57 MB，必须放 PSRAM，不能放任务栈或 internal RAM。
- 低内存时降到 3 个帧槽约 1.18 MB；PSRAM 充足且 SD 预读不足导致明显卡顿时，才考虑升到 6 个帧槽约 2.36 MB。
- 不默认预加载 8 帧或整段动画，避免把表盘缓存长期固定到 3MB 以上。
- 若使用 RGB565A，单帧体积会更大，只在确实需要透明 alpha 时启用；当前白底全屏画布优先 RGB565。
- 解码任务不能在 critical section 中访问 PSRAM 大块内存。
- 不在 ISR、critical section、LVGL draw 回调或 LVGL timer 中做文件读取和大块解码。

## 资源路线

素材源：

```text
D:\esp32S3\表情包\空闲.gif
D:\esp32S3\表情包\思考.gif
D:\esp32S3\表情包\工作中.gif
D:\esp32S3\表情包\来信息.gif
D:\esp32S3\表情包\失败.gif
D:\esp32S3\表情包\随机.gif
D:\esp32S3\表情包\愤怒.gif
```

电脑端预览输出：

```text
sdcard/watchface/frames/idle/frame_*.png
sdcard/watchface/frames/thinking/frame_*.png
sdcard/watchface/frames/working/frame_*.png
sdcard/watchface/frames/message/frame_*.png
sdcard/watchface/frames/error/frame_*.png
sdcard/watchface/frames/manifest.json
```

历史 resources 目标已放弃：

```text
resources/watchface/idle.rawanim
resources/watchface/thinking.rawanim
resources/watchface/working.rawanim
resources/watchface/message.rawanim
resources/watchface/error.rawanim
resources/watchface/manifest.json
```

当前板端 SD 卡目标：

```text
/sdcard/watchface/idle.rawanim
/sdcard/watchface/thinking.rawanim
/sdcard/watchface/working.rawanim
/sdcard/watchface/message.rawanim
/sdcard/watchface/error.rawanim
/sdcard/watchface/manifest.json
```

电脑端 SD 卡拷贝目录：

```text
E:\watchface\idle.rawanim
E:\watchface\thinking.rawanim
E:\watchface\working.rawanim
E:\watchface\message.rawanim
E:\watchface\error.rawanim
E:\watchface\manifest.json
```

V1 生成规则：

- 离线从 GIF 抽取短循环关键帧，生成屏幕原生 `410x502`。
- 表情放入全屏白色画布，不做低分辨率拉伸。
- 允许按素材构图做 contain/pad；是否 crop 必须显式记录，不要静默裁掉表情主体。
- 第一阶段优先生成简单 RGB565 raw animation 包；因实测总量超过 `8M`，当前直接放 SD 卡，不再扩大 `resources` 分区。
- RGB565A 仅在确实需要透明叠层时使用。
- 生成脚本输出 manifest，记录原始素材、抽帧索引、目标尺寸、帧率、帧数、颜色格式、压缩格式和文件大小。
- 电脑端生成后，把 `*.rawanim` 与 manifest 放入 `E:\watchface\`，板端以 `/sdcard/watchface/` 读取。

动画包建议：

- V1 先定义一个极简 raw animation 包格式，避免每帧一个小文件造成 LittleFS open/read 频繁抖动。
- 包头包含 magic、width、height、fps、frame_count、format。
- 帧表包含 offset、frame_size、delay_ms。
- 帧数据首选未压缩 RGB565 全帧，方便板端直接批量读入 PSRAM。
- 如果 SD 卡读取体积或启动加载时间不可接受，再评估 RLE/QOI/差分帧；V1 先不压缩，优先简单稳定。
- 解码/加载输出统一是 PSRAM 中的 RGB565 全屏帧，LVGL 不直接理解 raw animation 包。

后续增强：

- 如果 SD 卡读取时间或 PSRAM 压力过大，再做 delta rectangle / keyframe 或压缩。
- 如果动画切帧带来 LCD 刷新压力，再降低 fps 或只让 idle 使用慢动画，message/error 使用短循环。
- 不因为资源压力降成半尺寸拉伸；要么减少帧数/fps，要么后续引入压缩。

## 表情状态映射

V1 状态：

- `idle`：空闲.gif
- `thinking`：思考.gif，用于 Hermes 正在听、识别、等待回复或普通对话理解中。
- `working`：工作中.gif，仅用于明确的 Hermes 后台任务执行中；没有明确后台任务状态时不要猜成 working。
- `message`：来信息.gif，用于 Hermes 主动提示或收件箱未读。
- `error`：失败.gif，用于 Hermes 离线、配置错误、请求失败或超时。

预留状态：

- `random`：随机.gif，后续可作为 idle 变化素材。
- `angry`：愤怒.gif，后续可作为彩蛋或特殊错误状态。

优先级：

```text
message > error > working > thinking > idle
```

说明：

- Hermes 主动提示或 inbox 未读优先于其它状态。
- 即使 Hermes 当前离线，只要已有未读提示，表盘仍优先显示 `message`，不要让 `error` 覆盖已到达消息。
- `message` 是保持态，不是一次性短动画；只要 Hermes 主动提示或 inbox 未读仍存在，就一直保持 `message` 状态。
- `message` 退出条件是用户处理消息、进入 Hermes/收件箱后未读清零，或后台快照明确显示未读数为 0。
- 错误优先于普通工作/思考状态。
- `thinking` 和 `working` 必须区分：普通语音对话等待归 `thinking`，只有明确长任务/后台执行状态才归 `working`。
- 普通 Hermes 对话回复不自动进入收件箱；收件箱只放 Hermes 主动下发提示。

## 导航计划

目标导航：

```text
screen on / app boot
  -> watchface

watchface tap
  -> current function main screen

watchface swipe left
  -> Hermes voice page

subpage back
  -> current function main screen
```

实现建议：

- 不直接大改 GUI Guider 生成文件。
- 从现有 UI 初始化入口或 controller 层接管首屏加载。
- 保留现有功能主界面的生命周期，不把它删除；它仍承担子页面返回目标，表盘只接管屏幕亮起/唤醒或启动后的第一视觉入口。
- 状态栏不应常驻覆盖表盘；全局气泡/通知弹层允许作为顶层 overlay 临时覆盖表盘。

## SD 卡资源与运行时预算

当前执行路线走 SD 卡。`resources` 分区路线已按 `8M` 阈值评估放弃，不再作为本轮实现目标。

历史 resources 事实：

- `resources` 分区当前由 `littlefs_create_partition_image(resources ... FLASH_IN_PROJECT)` 生成 `build/resources.bin`。
- LVGL 侧已有 `A:/...` 资源路径概念，但表盘不把该路径直接交给 `lv_image_set_src()`。
- raw animation 已超过 `8M` 阈值，因此本轮不扩大 `resources` 分区。

V1 预算与结果：

1. 生成 5 个 `410x502` 低帧率 raw animation 包，放电脑端 `E:\watchface`，板端路径为 `/sdcard/watchface`。
2. 记录每个状态包大小、总帧数、SD 卡读取时间和首帧 ready 时间。
3. 运行时 PSRAM 表盘预算不固定为 4MB；V1 默认 4 帧槽约 `1.57MB`。
4. 低内存时降到 3 帧槽约 `1.18MB`；高余量且动画明显卡顿时再升到 6 帧槽约 `2.36MB`。
5. 不默认预加载 8 帧或整段动画；其余空间留给 frame descriptor、文件读缓冲、对齐和 LVGL 周边对象。
6. 用户确认：如果 resources 需要超过 `8M`，直接使用 SD 卡方案。
7. 2026-07-01 实测 raw animation 结果：5 个状态合计 `22,238,518 bytes`（含 manifest），超过 `8M` 阈值，因此脚本已自动输出到 `sdcard/watchface/` staging，不再塞进 `resources/watchface`。

实测明细：

- `idle.rawanim`：12 帧，`4,939,828 bytes`
- `thinking.rawanim`：8 帧，`3,293,228 bytes`
- `working.rawanim`：12 帧，`4,939,828 bytes`
- `message.rawanim`：12 帧，`4,939,828 bytes`
- `error.rawanim`：10 帧，`4,116,528 bytes`
- `manifest.json`：`9,278 bytes`
- 总量：`22,238,518 bytes`

当前 SD 卡事实：

- 工程已有 SD 卡组件 `components/sd_card/sd_manager.c`，挂载点 `/sdcard`，总线 `SPI3_HOST`，引脚 `MOSI=1/MISO=3/CLK=2/CS=17`。
- raw animation 已超过 `8M` 阈值，当前转入 SD 卡路线：把 `sdcard/watchface/` staging 目录内容复制到 SD 卡根目录的 `watchface/`。

禁止事项：

- 不因为 resources 资源大而静默降到半尺寸拉伸。
- 不因为动画资源大而退回静态单帧。
- 修改 `partitions.csv` 前必须记录新布局、影响范围和刷写要求。
- 如果修改分区表，板端验证不能只刷 `app-flash`，必须刷新 partition table、app 和相关资源分区。
- 不为表盘开启 LVGL 文件系统直读路径。

## 测试与验收

Source tests：

- 断言 `watchface_view` 不包含 `"A:/watchface"`、`"/sdcard/watchface"` 或其它文件路径作为 `lv_image_set_src()` 的 draw-time source。
- 断言没有 `LV_IMAGE_ALIGN_STRETCH` 或半尺寸拉伸逻辑。
- 断言存在 `watchface_anim_cache` 或等价内存动画缓存层。
- 断言 LVGL timer 不调用 `lv_fs_*`、`fread`、PNG/GIF 解码或自定义包解码。
- 断言 `watchface_anim_cache` 读取 SD 卡 `/sdcard/watchface` 文件，但只输出 PSRAM 内存帧给 LVGL。
- 断言 `watchface_view` 不调用网络、HTTP、Hermes API、NVS token 读取。
- 断言状态优先级为 `message > error > working > thinking > idle`。

Host preview：

```powershell
& D:\esp32S3\111\tools\ui_preview\scripts\build_apple_watch_s5_preview.ps1
& D:\esp32S3\111\tools\ui_preview\scripts\capture_apple_watch_s5_preview.ps1 -OutputPath D:\esp32S3\111\tools\ui_preview\artifacts\watchface-preview.png
```

验收点：

- 模拟器首屏是表情表盘。
- 表情有低帧率循环动画，不是静态图。
- 表盘上不显示时间或状态文字，表情动画完整占据画面。
- 点表盘进入现有功能主界面。
- 左滑进入 Hermes。
- message 状态点表盘仍进入现有功能主界面。
- 截图四角由模拟器全局圆角遮罩处理，不由业务 UI 自己画物理外壳。

Firmware build：

```powershell
. D:\esp-idf\v5.5.3\esp-idf\export.ps1
idf.py build
```

Board test：

- 如果只改 app 且资源没有变化，可按常规 `idf.py -p <PORT> app-flash`。
- 如果只更新表盘动画资源，只需要更新 SD 卡 `E:\watchface` 内容；如果后续另行修改分区表，必须全量刷新相关分区。
- 真机启动后至少监控到 Wi-Fi 获取 IP 和表盘稳定显示。
- 表盘动画连续运行 2 分钟，不出现明显卡顿累积、黑屏或内存持续下降。
- 缺失 SD 卡动画文件时，固件不能崩溃；冷启动首帧不可用时应回到现有主界面入口页。
- 重点确认不再出现：
  - `Cache disabled but cached memory region accessed`
  - `lv_bin_decoder_get_area` 相关崩溃
  - resources 读取失败导致 LVGL 空指针或黑屏
  - display bounce buffer / SPI flush `ESP_ERR_NO_MEM`
  - LVGL image decode/path not found

## 进度

- `[x]` 产品口径敲定：新根首页、全屏表情、不叠加时间或状态文字、点击进功能页、左滑进 Hermes。
- `[x]` 资源路线初版敲定：GIF 离线抽帧，生成屏幕原生低帧率动画包；该 resources 初版路线已被后续 `8M` 阈值决策替换为 SD 卡路线。
- `[x]` 板端事故复盘：旧实现 draw 阶段直读 LittleFS 大图会触发 cache-disabled 风险，已禁止继续沿用。
- `[x]` 重执行架构敲定：SD 卡只做资源仓库，活动动画帧先进入 PSRAM，再以 `lv_image_dsc_t` 渲染。
- `[x]` V1 动画口径敲定：低帧率短循环，后台解码，LVGL timer 只切 ready 帧。
- `[x]` 完成电脑端离线拆帧预览：新增 `scripts/watchface/extract_watchface_frames.py`，从 GIF 生成 5 个 V1 状态的 `410x502` 白底 PNG 预览帧和 manifest。
- `[x]` 用户调整路线：先评估 resources/LittleFS，若超过 `8M` 直接切 SD 卡。
- `[x]` 实现 raw animation 打包器：新增 `scripts/watchface/pack_watchface_rawanim.py`，支持 8MB resources 上限自动切 SD staging。
- `[x]` 运行打包器：raw animation 总量 `22,238,518 bytes`，超过 8MB 阈值，已生成 `sdcard/watchface/` staging。
- `[x]` 补齐 SD manager 通用文件 API：`read_file/write_file/create_dir/delete_file/get_file_size` 已在 `components/sd_card/sd_manager.c` 实现，避免后续缓存层调用头文件声明时链接失败。
- `[x]` 修复 LittleFS resources 镜像超分区：删除 `resources/watchface`，脚本默认预览帧改到 `sdcard/watchface/frames`，`idf.py build` 已通过。
- `[ ]` 实现 `watchface_anim_cache`，完成 SD 读取、PSRAM 帧槽、包头校验、ready 帧切换和失败保留上一帧。
- `[ ]` 实现 `watchface_view`，只负责 LVGL 图像、时间和手势。
- `[ ]` 实现 `watchface_controller`，接入 Hermes/inbox/time 快照并计算状态。
- `[ ]` 接入正式 UI 根首页导航。
- `[ ]` 接入 host preview，生成截图闭环。
- `[ ]` 运行 source tests、host build、firmware build。
- `[ ]` 板端全链路验证，确认 Wi-Fi 联网后表盘仍稳定且无 cache-disabled 崩溃。

## 决策记录

- 2026-06-30：用户确认表盘做成新的根首页，而不是单独 Hermes 页面或临时锁屏层。
- 2026-06-30：用户确认交互：点表情进入现有功能主界面，左滑进入 Hermes。
- 2026-07-01：再次确认保留左滑进入 Hermes；不要改成右滑。
- 2026-07-01：确认表盘点击行为不随状态变化；即使是 `message` 状态，点击表盘仍进入现有功能主界面入口页，不直接进 Hermes 收件箱。
- 2026-07-01：确认 `message` 是持续状态，不是短暂播放后自动回 idle；只要未读/主动提示存在就保持来信息表情。
- 2026-07-01：确认 `thinking`/`working` 边界：普通语音对话等待归 `thinking`，只有明确 Hermes 后台任务执行才归 `working`。
- 2026-07-01：确认状态优先级保持 `message > error > working > thinking > idle`；Hermes 离线不能覆盖已有未读提示。
- 2026-07-01：确认表盘冷启动不等待首帧；资源未 ready 时直接回到现有主界面入口页，不做内置兜底表盘。
- 2026-07-01：确认表盘 PSRAM 缓存采用弹性帧槽：默认 4 帧约 1.57MB，低内存降 3 帧，高余量且卡顿时升 6 帧，不默认 8 帧或整段预加载。
- 2026-07-01：确认资源晚到时不要自动从现有主界面跳回表盘；只在下一次亮屏、返回根页或用户主动返回时重新尝试。
- 2026-07-01：用户取消表盘时间显示；V1 表盘不叠加时间、状态文字或其它常驻 UI 元素。
- 2026-07-01：确认全局气泡/通知弹层允许作为顶层 overlay 临时覆盖表盘，但不属于表盘自身 UI。
- 2026-07-01：确认 V1 表盘不做按下视觉反馈，只处理点击和左滑手势。
- 2026-07-01：确认左滑始终进入 Hermes 语音页，不因未读或 `message` 状态改跳收件箱。
- 2026-07-01：确认子页面返回目标是旧主界面，不是表情表盘；表盘是屏幕亮起/启动后的第一视觉入口。
- 2026-07-01：确认屏幕从熄屏/待机被唤醒时也先显示表情表盘。
- 2026-06-30：用户要求表情包全屏覆盖。
- 2026-06-30：确认不直接使用 `LV_USE_GIF` 作为板端主路线；采用 GIF 离线抽帧到板端可播放动画资源。
- 2026-07-01：用户已回退上一轮代码；本计划改为重执行文档。
- 2026-07-01：新增硬约束：全屏表盘不能在 LVGL draw 阶段直接从 SD/LittleFS 流式读取，必须经 PSRAM 图像缓存层转换为内存 `lv_image_dsc_t` 后渲染。
- 2026-07-01：用户明确需要动画效果；V1 从“单帧状态图”升级为“低帧率短循环动画”，采用动画包 + PSRAM 多帧槽 + 后台解码 + LVGL ready 帧切换。
- 2026-07-01：已完成电脑端离线拆帧预览，当前画布为白底。PNG 检查帧不是最终板端资源格式，默认输出目录已从 `resources/watchface/frames/` 改为 `sdcard/watchface/frames/`，避免进入 LittleFS resources 镜像。
- 2026-07-01：用户先确认表盘资源可放 SD 卡，随后补充阈值：如果 resources 需要超过 `8M`，直接使用 SD 卡方案。打包器实测 raw animation 总量约 `22.24MB`，因此当前固定切到 SD 卡路线。
- 2026-07-01：用户确认当前 SD 卡电脑目录已有 `E:\watchface`，后续直接按板端 `/sdcard/watchface` 路径实现，不再走 resources 测试。
- 2026-07-02：确认 `resources` 不需要表情资源；已删除 `resources/watchface` 并修复脚本默认路径，避免 `LFS_ERR_NOSPC`。

## 下一步最小动作

1. 先把电脑端 `E:\watchface` 作为 SD 卡资源源，板端按 `/sdcard/watchface` 设计 `watchface_anim_cache` 的读取路径。
2. 实现 `watchface_anim_cache`：只做 SD raw animation -> PSRAM 帧槽 -> `lv_image_dsc_t`，先不接 UI。
3. 写 source tests 锁住“不直读文件系统绘制”“LVGL timer 不解码”和“不半尺寸拉伸”。
4. 最后接 `watchface_view/controller` 与根首页导航。
