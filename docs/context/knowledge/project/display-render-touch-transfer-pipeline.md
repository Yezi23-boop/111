---
id: display-render-touch-transfer-pipeline
tags: project, lvgl, display, touch, co5300, ft5x06, qspi, dma, rendering
summary: 记录当前仓库从 LVGL 渲染、CO5300 QSPI 传输到 FT5x06 触摸输入的完整链路，以及与 DMA 内存压力相关的典型故障模式。
last_reviewed: 2026-04-01
---

# 显示渲染、传输与触摸输入链路

## 适用范围

- 适用于当前仓库 `ESP32-S3 + ESP-IDF 5.5.3 + LVGL 9.3 + CO5300 + FT5x06/FT3168` 组合。
- 重点覆盖三条链路：
  - `LVGL` 渲染链
  - `CO5300` 屏幕刷新与 `QSPI/DMA` 传输链
  - `FT5x06` 触摸输入链
- 本文同时记录 `LVGL 9.2.2 -> 9.3.0` 升级后实际暴露出的显示异常排查结论。
- 若需要查看每轮调参的时间线和用户真机反馈，请优先配合阅读 [lvgl-display-tuning-log.md](/D:/esp32S3/111/docs/context/knowledge/project/lvgl-display-tuning-log.md)。

## 一、启动链路

### 1. 系统启动顺序

- `main/111.c`
  - 先调用 `hardware_init()`
  - 再创建 `lvgl_task`
  - 然后启动 `network_service` 和 `official_chat_service`
- `main/hardware_init.c`
  - 先初始化 `NVS`
  - 再初始化 `audio_app` / `SD` / `audio_codec`
  - 最后初始化 `Wi-Fi`
- `main/lvgl_task.c`
  - 在独立任务中调用 `lv_port_init_small()`
  - 然后执行 `setup_ui()` / `events_init()`
  - 最后在循环里反复调用 `lv_timer_handler()`

### 2. 启动顺序的工程含义

- 显示初始化不是系统最早阶段执行，而是发生在音频、SD、Wi-Fi 已经启动之后。
- 这意味着 LCD 刷新阶段可用的片内 DMA 内存，不是“裸系统”状态，而是“音频 + Wi-Fi 已占用一部分内部资源”后的状态。
- 因此显示链路里的 `SPI DMA priv TX buffer` 失败，不能只从显示模块本身理解，也要看启动时机和并发组件。

## 二、显示渲染链

### 1. LVGL 入口

- `main/lvgl_task.c` 中 `lv_port_init_small()` 完成 `lv_init()`、面板、触摸、显示缓冲、输入设备和 tick 初始化。
- `lv_timer_handler()` 驱动：
  - 对脏区域做失效管理
  - 进行对象绘制
  - 调用 display flush 回调

### 2. 当前显示对象与渲染模式

- `components/lvgl_port/lv_port.c`
  - `lv_display_create(LCD_WIDTH, LCD_HEIGHT)`
  - `lv_display_set_color_format(..., LV_COLOR_FORMAT_RGB565)`
  - `lv_display_set_buffers(..., buf1, buf2, buf_size, 0)`
- `managed_components/lvgl__lvgl/src/display/lv_display.h` 显示：
  - `lv_display_render_mode_t` 中 `LV_DISPLAY_RENDER_MODE_PARTIAL` 枚举值排在第一位
  - 也就是当前代码里的字面量 `0` 实际等价于 `LV_DISPLAY_RENDER_MODE_PARTIAL`

### 3. 当前缓冲策略的真实含义

- `lv_port_disp_init_single()` 虽然日志叫“Single buffer size”，但实际分配了两个 `PSRAM` 缓冲。
- `LV_PORT_FIXED_CHUNK_LINES2 = 502`，所以每个缓冲大小为 `410 * 502 * sizeof(lv_color_t)`，也就是整屏大小。
- 由于 render mode 实际是 `PARTIAL`：
  - LVGL 仍按“局部失效区域”模式工作
  - 只是缓冲区碰巧大到等于全屏
- 这是一种“全屏尺寸缓冲 + 局部刷屏语义”的混合模式。

### 4. 当前 UI 对渲染器的压力特征

`main/ui/generated` 当前不是简单纯色 UI，而是大量组合了：

- `radius`
- 半透明 `bg_opa`
- 阴影
- `RGB565A8` 图片资源

典型证据：

- `setup_scr_screen_main.c`
  - 主屏背景图是 `_5_RGB565A8_410x502`
  - 数字时钟用了 `radius = 42`、`bg_opa = 128`
  - 多个卡片用了 `radius = 18`
  - 下拉区域用了 `radius = 34`
  - 滑条和音量条用了圆角轨道与指示器
- `screen_wallpaper` 页使用多张 `_yuanjiao*_RGB565A8_*` 圆角资源图

结论：

- 当前 UI 很依赖 `LVGL 9.x` 的圆角遮罩、透明混合和 `RGB565A8` 图片路径。
- 因此“圆元素先出问题”并不奇怪，它们比普通矩形更容易暴露渲染和传输链路的不稳定。

## 三、显示传输链

### 1. 面板初始化链

- `components/co5300_panel/co5300_panel.c`
  - 初始化 `SPI2_HOST`
  - 创建 `esp_lcd_panel_io_spi`
  - 创建 `esp_lcd_new_panel_co5300`
  - `esp_lcd_panel_reset()`
  - `esp_lcd_panel_init()`
  - `esp_lcd_panel_disp_on_off(true)`

### 2. 当前 QSPI 关键参数

- `components/co5300_panel/co5300_panel_defaults.h`
  - 分辨率：`410x502`
  - `CO5300_PANEL_OPTIMIZED_PCLK_HZ = 50MHz`
  - `CO5300_PANEL_OPTIMIZED_TRANS_QUEUE_DEPTH = 8`
  - `CO5300_PANEL_MAX_TRANSFER_LINES = 100`
  - `CO5300_PANEL_USE_TE_SIGNAL = 0`

### 3. `lv_port` 到面板的 flush 路径

- `lv_port_disp_flush()`
  - 根据区域高度选择整块传输或分块传输
- `lv_port_flush_area_with_sync()`
  - 对当前 chunk 准备 bounce buffer，并在需要时执行 `lv_draw_sw_rgb565_swap()`
  - 然后调用 `esp_lcd_panel_draw_bitmap()`

### 4. 当前字节序处理

- `LVGL` display color format 仍配置为 `LV_COLOR_FORMAT_RGB565`
- `flush` 阶段手动调用 `lv_draw_sw_rgb565_swap()`
- 按 `LVGL` 官方文档，这是一条合法路径：
  - 显示格式用 `RGB565`
  - 如屏幕要求交换高低字节，则在 `flush_cb` 中做 `lv_draw_sw_rgb565_swap`

本轮排查中已验证：

- 把 display format 改成 `RGB565_SWAPPED` 并移除 flush swap 后，现象更糟
- 因此当前仓库不应把“颜色格式切到 `RGB565_SWAPPED`”当作默认修复方向

## 四、触摸输入链

### 1. 当前链路

- `components/touch_ft5x06/touch_ft5x06.c`
  - 通过 `i2c_manager` 初始化共享 I2C 总线
  - 复位触摸芯片
  - 读取 `0x02` 触摸点数寄存器
  - 读取 `0x03` 起始的坐标寄存器
- `components/lvgl_port/lv_port.c`
  - `lv_indev_create()`
  - `lv_indev_set_type(..., LV_INDEV_TYPE_POINTER)`
  - `lv_indev_set_read_cb(..., lv_port_indev_read)`

### 2. 当前触摸实现特征

- 触摸是轮询读，不是中断驱动
- 默认只读取 1 个触点
- 读失败时静默返回无触摸，避免日志刷屏
- `lv_port` 保留上一次有效坐标，抬手时只改 `state = RELEASED`，不把坐标直接清零

### 3. 当前触摸链的潜在注意点

- 头文件定义了 `INT_GPIO = 38`，但当前实现没有真正把触摸输入接成中断事件链。
- 面板侧调用了 `esp_lcd_panel_set_gap(s_panel, 23, 0)`，日志文字仍写“向右偏移20像素”，代码和日志不一致。
- 触摸链当前没有显式做：
  - 旋转映射
  - gap 补偿
  - 边界裁剪后的坐标修正

这不一定代表当前触摸已经错位，但如果未来出现“显示位置与触摸热点不一致”，应优先复核这一层。

## 五、这次 LVGL 9.3 排查中已观测到的现象

### 1. 升级前后差异

- `main/idf_component.yml`
  - 从 `lvgl/lvgl 9.2.2` 升级到 `9.3.0`
- `lv_port.c` 主体逻辑基本没有本质变化
- `main/ui/generated` 在升级后重新生成

### 2. 用户表象

- `9.2.2` 正常
- `9.3.0` 后部分静态组件显示异常
- 异常对象集中在“有圆、有圆角、有透明叠加”的元素上

### 3. 已验证结论

- 不是单纯的 `RGB565_SWAPPED` 配置问题
  - 该方向已实际验证，结果更差
- 不是“整块无分片传输”就能直接解决
  - 关闭分块后，真机日志出现：
    - `setup_dma_priv_buffer(): Failed to allocate priv TX buffer`
    - `panel_io_spi_tx_color ... failed`
    - `Display flush failed: ESP_ERR_NO_MEM`
- `LV_PORT_FIXED_CHUNK_LINES` 从 `30` 收回 `10` 后，显示有所改善
  - 说明问题与分块大小、DMA 临时缓冲压力存在明显关联
- 但简单地“异步连续发送多个 chunk”会带来新的绿色条纹
  - 这说明仅仅减小 chunk 不够，还要控制 chunk 的排队节奏

## 六、当前最可信的根因模型

### 1. 已确认部分

- 整块刷屏在当前运行态下会触发内部 DMA 私有缓冲申请失败。
- 原因不是屏幕驱动单独失效，而是系统进入 `audio + sd + wifi + lvgl` 并发状态后，片内 DMA 可用资源不足。

### 2. 高可信推断

- `LVGL 9.3` 下局部失效区域与资源混合路径更复杂，导致 flush 的 area 形状、数量或密度发生了变化。
- 当 `lv_port` 自己再把这些 area 二次切块，并且连续异步塞进 `esp_lcd` 队列时：
  - 更容易占满 `SPI` 内部私有 DMA 缓冲
  - 或更容易触发面板传输边界/时序问题
  - 最终表现为绿色条纹、错块或圆角边缘异常

### 3. 工程解释

这次问题更像“渲染结果本身没完全错，但传输层没有稳定地把它送到屏上”。

也就是说：

- 圆角元素不是根因
- 圆角元素只是最先暴露问题的“高压测试样本”

## 七、当前较稳的修复方向

### 1. 已采用的方向

- 保持 `RGB565 + flush 时 swap`
- 保持小 chunk，例如 `10` 行
- 不关闭 chunk
- 每发一个 chunk，都等待 `on_color_trans_done` 后再发下一个 chunk

### 2. 这条路径的工程意义

- 既避免整块传输导致的 `priv TX buffer` 申请失败
- 也避免多个 chunk 同时排队导致的条纹/错块
- 本质上是在“吞吐量”和“稳定性”之间，优先选稳定性

### 3. 后续推荐改进

如果未来要继续优化，应优先按以下顺序推进：

1. 把 `lv_display_set_buffers(..., 0)` 改成显式 `LV_DISPLAY_RENDER_MODE_PARTIAL`
   - 避免以后因为枚举值或阅读误解造成隐性问题
2. 给 `lv_port` 增加 flush 统计日志
   - 每帧 area 数量
   - 每个 area 的宽高
   - 每个 chunk 的耗时
   - `esp_err_t` 失败计数
3. 明确区分两类缓冲策略
   - “全屏双缓冲直刷”
   - “局部 buffer + partial flush”
   - 不要继续维持“全屏大小缓冲 + partial 字面量 0”的隐式混合配置
4. 若重新启用 TE
   - 要同时验证 `chunk` 串行发送时的帧首等待逻辑
   - 否则可能从“条纹问题”切换成“卡顿/TE 超时问题”

### 5. 可先尝试的 LVGL 软件绘制缓存开关

- 当现象集中在圆角矩形、圆、弧或带阴影控件，且升级到 `LVGL 9.3` 后才明显暴露时，可先做一个不改业务 UI 的 `sdkconfig` 级 A/B：
  - `CONFIG_LV_DRAW_SW_CIRCLE_CACHE_SIZE=0`
  - `CONFIG_LV_DRAW_SW_SHADOW_CACHE_SIZE=0`
- 这两个配置只影响 `LVGL` 软件绘制器对“圆形/圆角 mask”和“阴影结果”的缓存，不会关闭圆角或阴影功能本身。
- 若关闭后现象明显减轻，说明问题至少与 `LVGL 9.3` 的圆/阴影缓存复用路径强相关；若完全无变化，再优先回到 flush / DMA / TE 链路继续排查。
- 这是一个低侵入定位开关，但代价是 CPU 开销增加、刷新率可能下降，因此更适合作为诊断或临时规避手段，而不是默认长期配置。

### 6. 可直接验证的透明混合开关

- 若问题对象本身带 `radius` 且 `bg_opa < 255`，可以先把该对象的 `bg_opa` 临时改成 `255` 做单变量验证。
- 在当前仓库主屏里，适合优先做此验证的对象包括：
  - `screen_main_digital_clock_1`
  - `screen_main_Dropdown_menu`
  - `screen_main_Brightness` 的 `LV_PART_MAIN / LV_PART_INDICATOR`
  - `screen_main_loudness` 的 `LV_PART_MAIN / LV_PART_INDICATOR`
  - `screen_main_slider_1` 的 `LV_PART_MAIN / LV_PART_INDICATOR`
- 若改成 `255` 后明显改善，说明问题与“圆角 + 半透明混合”强相关；若仍无明显变化，再继续看阴影、`RGB565A8` 图像或 flush/传输链。

### 7. `5 行 chunk + 40MHz PCLK` 的联合 A/B

- 若现象表现为：
  - `30 -> 10` 行分块后仍有圆角/滑条异常
  - 继续降到 `5` 行后圆角异常明显减轻
  - 但同时出现大量绿色条纹
- 这通常意味着有两层问题叠加：
  - 上层 `lv_port` 的 `30` 行硬切块会放大圆角控件在 chunk 边界的异常
  - 下层 `CO5300 QSPI` 在极小 chunk 高频传输下，`50MHz` 时序裕量不足，开始暴露绿色条纹
- 当前仓库可先做一个最小联合 A/B：
  - `LV_PORT_FIXED_CHUNK_LINES = 5`
  - `CO5300_PANEL_OPTIMIZED_PCLK_HZ = 40 * 1000 * 1000`
- 这组改动的目的不是直接定版，而是验证：
  - 小 chunk 是否继续改善圆角/滑条问题
  - 降低面板时钟后绿色条纹是否减少
- 如果这组现象成立，说明根因不是单一的 `LVGL` 渲染 bug，而是“上层 chunk 边界 + 下层面板时序裕量”共同作用。

### 8. 不要直接原地修改 `LVGL` 渲染缓冲再送 SPI

- `LVGL` 文档在 `lv_display_set_color_format()` 说明里明确提到：若屏幕是 `RGB565` 且需要交换高低字节，应在 `flush_cb` 中调用 `lv_draw_sw_rgb565_swap()`。
- 但在当前仓库这种 `PSRAM 全屏渲染缓冲 + esp_lcd SPI/QSPI 异步发送` 组合下，直接对 `px_map` 原地 swap 风险很高：
  - `px_map` 同时承担 `LVGL` 渲染缓冲角色
  - `esp_lcd_panel_io_spi` 的 `tx_color()` 会把颜色数据以 queue 方式送 SPI
  - 当源缓冲不满足 DMA 条件时，底层 `spi_master` 还可能额外申请私有 TX buffer
- 这会把“渲染缓冲的数据内容”和“总线发送时的数据生命周期”耦合在一起，尤其在圆角/局部刷新更碎的场景下，更容易表现成：
  - 绿色条纹
  - 错块
  - 静止控件也出现异常
- 当前仓库更稳的方式是在 `lv_port` 里增加片内 `DMA bounce buffer`：
  - 先把 chunk 从 `px_map` 拷到 bounce buffer
  - 再在 bounce buffer 上做 `lv_draw_sw_rgb565_swap()`
  - 最后把 bounce buffer 交给 `esp_lcd_panel_draw_bitmap()`
- 这样做的好处：
  - 不污染 `LVGL` 原始渲染缓冲
  - 更容易让 LCD 刷新直接吃到 `Internal + DMA` 内存，减少底层私有 DMA 缓冲申请和隐藏复制成本

## 八、复用排障清单

当未来再遇到显示异常，建议按下面顺序排：

1. 先分清是“渲染错误”还是“传输错误”
   - 渲染错误更像形状、半透明、圆角逻辑不对
   - 传输错误更像条纹、错块、局部花屏、偶发失败
2. 先看 `Display flush failed`、`panel_io_spi_tx_color failed`、`setup_dma_priv_buffer failed`
   - 有这些日志时，优先查传输链和内部 DMA 压力
3. 再看当前 chunk 大小和是否连续异步排队
4. 再看是否在运行态同时启用了音频、Wi-Fi、录音、AI 等高占用模块
5. 最后才去怀疑 `LVGL` 的圆角算法或颜色格式路径

## 九、对以后最有价值的经验

- 在当前仓库里，屏幕问题不能只看 `LVGL`，必须把 `audio / wifi / dma / qspi` 一起看。
- `圆角元素出问题` 不等于 `圆角算法出问题`，很多时候只是它们更容易暴露 flush/传输异常。
- `关闭 chunk` 不一定更稳，在资源紧张时反而可能最先触发 `priv TX buffer` 失败。
- 对这块屏来说，稳定刷新更重要的是“传输节奏受控”，而不是单纯“块越大越快”。
