---
id: lvgl-display-tuning-log
tags: project, lvgl, display, co5300, tuning, debugging
summary: 按时间记录 LVGL 9.3 显示异常排查中的每轮参数修改、实测现象与当前结论，供后续 agent 直接复用，避免重复调参。
last_reviewed: 2026-04-08
memory_type: semantic
scope: repo
owners: components/lvgl_port, components/co5300_panel, main/ui/lvgl_task.c, main/ui/ui_refresh_policy.c
triggers: lvgl, display, tuning, log
evidence_level: observed
---

# LVGL 显示调参实验记录

## 适用范围

- 当前仓库 `ESP32-S3 + ESP-IDF 5.5.3 + LVGL 9.3 + CO5300 + FT5x06/FT3168`
- 重点问题：
  - `LVGL 9.2.2` 正常，升级到 `9.3.0` 后，圆角/圆形相关控件更容易出现异常
  - 某些调参组合会伴随绿色条纹
  - 音频、Wi-Fi、LCD 刷新并发时，DMA 可用内存与传输节奏都会参与放大问题

## 当前结论快照

- 当前最像根因的不是单一 `LVGL` 圆角算法 bug，而是两层问题叠加：
  - 上层 `lv_port` 对局部脏区再做固定行数二次切块，`chunk` 边界会放大圆角/滑条异常
  - 小 `chunk` 下若允许多个块同时在飞，容易放大绿色条纹或错块
- 当前必须额外接受一个硬件边界：
  - `CO5300` 在本项目上的 `PCLK` 实测/资料上限按 `50MHz` 收敛
  - 因此“全屏稳定 60 FPS”不再作为当前架构下的现实目标，目标改为“局部交互高帧率 + 主观顺滑”
- `CO5300_PANEL_OPTIMIZED_PCLK_HZ` 从 `50MHz` 降到 `40MHz` 后，绿色条纹没有明显减少，说明“单纯时钟太快”不是主因
- `main/ui/lvgl_task.c` 与正常基线提交 `aa165525fddae2eb711da144c90539e33e0b67c` 的核心逻辑一致，目前不认为它是决定性根因

## 调整记录

### 1. 尝试切到 `RGB565_SWAPPED`

- 修改：
  - `components/lvgl_port/lv_port.c`
  - 将显示色彩格式改到 `LV_COLOR_FORMAT_RGB565_SWAPPED`
  - 去掉 flush 阶段手工 `lv_draw_sw_rgb565_swap()`
- 现象：
  - 用户反馈没有恢复，反而更严重
- 结论：
  - 当前仓库不应直接把问题归咎到 `RGB565 swap` 配置
  - 该方向不是本轮主线
- 后续更正（2026-08-05）：该实验是 LVGL 9.3 期间“换格式 + 删 swap”双变量叠加的观察，且当时画面本身有圆角/绿条纹异常。已在 LVGL 9.5 正常基线上重做 A/B/C 三组真机对照：路线 B（`RGB565_SWAPPED` + 不 swap）显示一切正常，仓库已正式切换。详见 `runs/2026-08-05-attempt-rgb565-swapped-route-b.md`。

### 2. 关闭上层二次分块传输

- 修改：
  - `components/lvgl_port/lv_port_config.h`
  - `LV_PORT_CHUNKED_TRANSFER_ENABLE = 0`
- 现象：
  - 运行日志出现 `spi_master: setup_dma_priv_buffer ... Failed to allocate priv TX buffer`
  - 随后 `Display flush failed: ESP_ERR_NO_MEM`
- 结论：
  - 当前运行态下，整块刷屏会打到内部 DMA 私有缓冲瓶颈
  - 不能简单回到“无 chunk”模式

### 3. `LV_PORT_FIXED_CHUNK_LINES 30 -> 10`

- 修改：
  - `components/lvgl_port/lv_port_config.h`
  - `LV_PORT_FIXED_CHUNK_LINES = 10`
- 现象：
  - 比 `30` 行略好，但圆角/圆形相关异常仍存在
- 结论：
  - `30` 行过粗，确实会放大圆角控件问题
  - 但 `10` 行还不够细，尚未彻底压住

### 4. 每发一个 chunk 就等待回调完成

- 修改：
  - `components/lvgl_port/lv_port.c`
  - 将 flush 改成严格串行的 “发一块，等一块”
- 现象：
  - 画面更稳
  - 刷新率明显下降，用户明确表示太低
- 结论：
  - 这说明“同步过严”能抑制显示异常，但吞吐不可接受
  - 只能作为诊断档，不能作为长期方案

### 5. `LV_PORT_FIXED_CHUNK_LINES = 30`，`LV_PORT_MAX_INFLIGHT_CHUNKS = 2`

- 修改：
  - `components/lvgl_port/lv_port_config.h`
  - `LV_PORT_FIXED_CHUNK_LINES = 30`
  - `LV_PORT_MAX_INFLIGHT_CHUNKS = 2`
- 现象：
  - 刷新率回升
  - 用户反馈原来的撕裂/圆角异常又回来了
- 结论：
  - 大 `chunk` 会重新把边界问题放大
  - `inflight = 2` 本身不能解决圆角类问题

### 6. 关闭 `LVGL` 圆形/阴影缓存

- 修改：
  - `sdkconfig`
  - `CONFIG_LV_DRAW_SW_CIRCLE_CACHE_SIZE = 0`
  - `CONFIG_LV_DRAW_SW_SHADOW_CACHE_SIZE = 0`
- 现象：
  - 作为低侵入 A/B 已保留
  - 目前没有单独证据表明它就是主因，但它依然是值得保留的诊断挡位
- 结论：
  - 这组配置更像是帮助缩小“LVGL 软件绘制缓存路径”嫌疑范围
  - 当前主要矛盾仍在 flush / chunk / 传输节奏

### 7. 提高主屏几个圆角控件 `bg_opa` 到 `255`

- 修改：
  - `main/ui/generated/setup_scr_screen_main.c`
- 现象：
  - 用于验证“圆角 + 半透明混合”是否是主要触发条件
  - 后续已按用户要求恢复 GUI Guider 原始透明度
- 结论：
  - 透明混合可能参与放大问题，但不是当前默认修复方案
  - 所有这类 A/B 只应作为定位手段，不应长期混在默认 UI 配置里

### 8. `LV_PORT_FIXED_CHUNK_LINES = 5`

- 修改：
  - `components/lvgl_port/lv_port_config.h`
  - `LV_PORT_FIXED_CHUNK_LINES = 5`
- 现象：
  - 用户明确反馈：圆角变好了
  - 但出现大量绿色条纹
- 结论：
  - 这组反馈非常关键，说明小 `chunk` 的确能改善圆角/滑条异常
  - 同时也说明还有另一个问题在小块高频传输下被放大

### 9. `LV_PORT_FIXED_CHUNK_LINES = 5` + `CO5300_PANEL_OPTIMIZED_PCLK_HZ = 40MHz`

- 修改：
  - `components/lvgl_port/lv_port_config.h`
  - `components/co5300_panel/co5300_panel_defaults.h`
- 现象：
  - 用户反馈绿色条纹没有明显减少
  - 但圆角/滑条继续变好
- 结论：
  - 绿色条纹的主因不像是“单纯面板时钟太快”
  - 当前更应怀疑“小块 + 多块并发在飞”的传输模式

### 10. `LV_PORT_MAX_INFLIGHT_CHUNKS 2 -> 1`

- 修改：
  - `components/lvgl_port/lv_port_config.h`
- 目的：
  - 保持 `LV_PORT_FIXED_CHUNK_LINES = 5`
  - 保持 `CO5300_PANEL_OPTIMIZED_PCLK_HZ = 40MHz`
  - 只单独验证绿色条纹是否主要来自“多个小块同时在飞”
- 预期观察：
  - 若绿色条纹明显减少，而圆角改善保持，说明当前主要矛盾就是并发传输窗口过大
  - 若条纹变化不大，则需要进一步下钻 `co5300_panel` 或底层 SPI/QSPI 刷新策略

### 11. 引入片内 DMA bounce buffer，避免直接原地 swap `LVGL` 渲染缓冲

- 修改：
  - `components/lvgl_port/lv_port.c`
  - 为每个在飞 chunk 分配一块 `Internal + DMA` bounce buffer
  - flush 时先把 `px_map` 拷到 bounce buffer，再在 bounce buffer 上执行 `lv_draw_sw_rgb565_swap()`，最后交给 `esp_lcd_panel_draw_bitmap()`
- 触发原因：
  - 用户在 `LV_PORT_MAX_INFLIGHT_CHUNKS = 1` 下仍然反馈“大量绿色条纹且刷新率低”
  - 说明问题不止是“多个 chunk 同时在飞”
- 代码与文档依据：
  - `LVGL` 文档说明 `flush_cb` 里的 `px_map` 是渲染后的原始像素图，`RGB565` 字节序需要在 `flush_cb` 里处理
  - `ESP-IDF esp_lcd/spi` 在 `tx_color()` 中会对大块颜色数据做 queue 发送；当源缓冲不满足 DMA 条件时，底层 `spi_master` 还可能额外申请私有 TX 缓冲
- 当前判断：
  - 直接原地 swap `LVGL` 渲染缓冲，会把“渲染缓冲生命周期”和“SPI/QSPI 发送生命周期”耦合到一起
  - bounce buffer 的目的有两个：
    - 不污染 `LVGL` 原始渲染缓冲
    - 优先让 LCD 刷新直接使用片内 DMA 缓冲，减少底层再申请私有 TX buffer 的概率
- 待观察：
  - 真机上绿色条纹是否明显减少
  - 在 `chunk = 5` 条件下刷新率是否回升

### 12. 参考 Waveshare BSP 引入 `rounder_event_cb`

- 参考来源：
  - `ESP32-S3-Touch-AMOLED-2.06/examples/ESP-IDF-v5.4.2/02_lvgl_demo_v9`
  - 关键实现位于 `waveshare__esp32_s3_touch_amoled_2_06/esp32_s3_touch_amoled_2_06.c`
- 修改：
  - `components/lvgl_port/lv_port.c`
  - 给 `lv_display` 注册 `LV_EVENT_INVALIDATE_AREA`
  - 把无效区域按偶数边界对齐：
    - `x1/y1` 向下对齐偶数
    - `x2/y2` 向上对齐奇数
- 触发原因：
  - Waveshare 的示例也使用 `RGB565 + swap_bytes`
  - 但它额外对失效区域做 rounder，对圆角和弧线类控件更友好
- 当前仓库同步状态：
  - `LV_PORT_FIXED_CHUNK_LINES1 = 10`
  - `LV_PORT_FIXED_CHUNK_LINES2 = 10`
  - `LV_PORT_FIXED_CHUNK_LINES = 10`
  - `LV_PORT_MAX_INFLIGHT_CHUNKS = 2`
- 当前判断：
  - 这项更像“减少圆角控件局部刷新碎片化”的修正
  - 它不直接解决线速问题，但有机会减少 `LVGL 9.3` 下圆角相关控件的边界异常

## 当前推荐的排障顺序

1. 先判断现象更像“圆角边界异常”还是“绿色条纹/错块”
2. 若圆角类问题突出，先优先看 `LV_PORT_FIXED_CHUNK_LINES`
3. 若绿色条纹突出，先优先看 `LV_PORT_MAX_INFLIGHT_CHUNKS`
4. 只有当 `chunk / inflight` 单变量都不能解释现象时，再深入 `co5300_panel`
5. 不要一开始就把问题归结为 `lvgl_task.c` 或单纯 `PCLK` 过高

## 60 FPS 预算修正

- 当前屏幕分辨率 `410 x 502`，`RGB565` 每帧数据量为：
  - `410 * 502 * 2 = 411,640 bytes`
- 若按全屏 `60 FPS` 计算，总线每秒至少要搬运：
  - `411,640 * 60 = 24,698,400 bytes/s`
- 当前 `co5300_panel` 在真实硬件上限 `QSPI 50MHz` 下，理论裸带宽约为：
  - `50,000,000 * 4 / 8 = 25,000,000 bytes/s`
- 结论：
  - `50MHz` 只是理论裸上限，几乎没有命令、窗口设置、调度和同步余量
  - 当前软件架构下，不应再以“全屏稳定 60 FPS”为默认目标
  - 更现实的目标是：尽量减少滑动切割感、减少每帧块数，并把局部交互 FPS 做高

## 50MHz 上限下的当前工程阻塞项

- `sdkconfig`
  - `CONFIG_LV_DEF_REFR_PERIOD = 16`
  - `CONFIG_LV_DRAW_SW_CIRCLE_CACHE_SIZE = 4`
  - `CONFIG_LV_DRAW_SW_SHADOW_CACHE_SIZE = 4`
  - 已经解除“40ms 刷新周期”这个明显上限，但这仍不等于全屏真 60 帧
- `components/lvgl_port/lv_port_config.h`
  - `LV_PORT_FIXED_CHUNK_LINES1 = 512`
  - `LV_PORT_FIXED_CHUNK_LINES2 = 512`
  - `LV_PORT_FIXED_CHUNK_LINES = 100`
  - `LV_PORT_CHUNKED_TRANSFER_ENABLE = 1`
  - `LV_PORT_MAX_INFLIGHT_CHUNKS = 2`
- `components/co5300_panel/co5300_panel_defaults.h`
  - `CO5300_PANEL_MAX_TRANSFER_LINES = 100`
  - `CO5300_PANEL_OPTIMIZED_PCLK_HZ = 50MHz`
- 这意味着：
  - LVGL render tile 已接近整屏，优先减少滑动画面的 tile 分片感
  - 面板 flush 仍按 `100` 行发送，全屏约 `6` 块，优先减少块管理开销
  - 当前最值得优化的是“主观顺滑度和局部交互 FPS”，而不是继续用错误的 80MHz 前提追全屏 60 帧

## 50MHz 上限下的推荐起点

- 若目标是“在真实硬件边界内尽量提帧”，当前建议以这组参数为起点：
  - `CONFIG_LV_DEF_REFR_PERIOD = 16`
  - `LV_PORT_FIXED_CHUNK_LINES1 = 512`
  - `LV_PORT_FIXED_CHUNK_LINES2 = 512`
  - `LV_PORT_FIXED_CHUNK_LINES = 100`
  - `LV_PORT_MAX_INFLIGHT_CHUNKS = 2`
  - `CO5300_PANEL_MAX_TRANSFER_LINES = 100`
  - `CO5300_PANEL_OPTIMIZED_PCLK_HZ = 50MHz`
- 这组的意义是：
  - 把单帧 flush 块数压到约 `6` 块
  - 用 `512` 行 render tile 优先减轻“滑动画面有切割感”
  - 明确以真实 `50MHz` 上限做调优，避免建立在错误硬件假设上的虚高目标
- 失效边界：
  - 若仍需频繁全屏重绘，单靠软件渲染和 SPI/QSPI 传输仍可能达不到真 `60 FPS`
  - 若 UI 大量使用圆角、阴影、半透明图片，渲染端 CPU 也可能先成为瓶颈

### 13. 已落地的 60FPS 尝试档（已被 50MHz 真实上限否定）

- 已修改：
  - `components/lvgl_port/lv_port_config.h`
    - `LV_PORT_FIXED_CHUNK_LINES1 = 100`
    - `LV_PORT_FIXED_CHUNK_LINES2 = 100`
    - `LV_PORT_FIXED_CHUNK_LINES = 100`
    - `LV_PORT_MAX_INFLIGHT_CHUNKS = 2`
  - `components/co5300_panel/co5300_panel_defaults.h`
    - `CO5300_PANEL_MAX_TRANSFER_LINES = 100`
    - `CO5300_PANEL_OPTIMIZED_PCLK_HZ = 80MHz`
    - `CO5300_PANEL_OPTIMIZED_TRANS_QUEUE_DEPTH = 8`
  - `sdkconfig`
    - `CONFIG_LV_DEF_REFR_PERIOD = 16`
    - `CONFIG_LV_DRAW_SW_CIRCLE_CACHE_SIZE = 4`
    - `CONFIG_LV_DRAW_SW_SHADOW_CACHE_SIZE = 4`
- 已验证：
  - 源码级测试通过
  - `idf.py fullclean`
  - `idf.py build`
- 后续修正：
  - 用户随后明确给出硬件边界：`CO5300` 当前最高 `PCLK = 50MHz`
  - 因此这一档只能作为“错误前提下的历史尝试”保留，不再作为当前推荐配置

### 14. 滑动优先档：LVGL render tile 提到 512

- 触发原因：
  - 用户反馈 `LV_PORT_FIXED_CHUNK_LINES1/2 = 100` 时，滑动画面仍有明显“切割感”
  - 目标优先从 LVGL 渲染层减少 tile 边界感，而不先改动 flush / 面板发送逻辑
- 修改：
  - `components/lvgl_port/lv_port_config.h`
    - `LV_PORT_FIXED_CHUNK_LINES1 = 512`
    - `LV_PORT_FIXED_CHUNK_LINES2 = 512`
  - 保持：
    - `LV_PORT_FIXED_CHUNK_LINES = 100`
    - `LV_PORT_MAX_INFLIGHT_CHUNKS = 2`
    - `CO5300_PANEL_MAX_TRANSFER_LINES = 100`
    - `CO5300_PANEL_OPTIMIZED_PCLK_HZ = 80MHz`
- 当前判断：
  - 这不是为了提高总线吞吐，而是为了减少 LVGL render tile 在滚动过程中的分片感
  - 它相当于“更大渲染块 + 仍按 100 行发送”，重点改善交互观感
- 已验证：
  - 源码级测试通过
  - `idf.py build` 通过
- 待验证风险：
  - 更大的 render tile 可能重新放大局部刷新面积
  - 真机上仍需观察是否带回撕裂、条纹或圆角异常

### 15. 按真实硬件上限收敛：`PCLK = 50MHz`，flush/max transfer 保持 `100`

- 触发原因：
  - 用户确认：`CO5300` 当前最高 `PCLK = 50MHz`
  - 继续沿用 `80MHz` 的 60FPS 尝试档会把后续所有提帧讨论建立在错误前提上
- 当前收敛配置：
  - `components/lvgl_port/lv_port_config.h`
    - `LV_PORT_FIXED_CHUNK_LINES1 = 512`
    - `LV_PORT_FIXED_CHUNK_LINES2 = 512`
    - `LV_PORT_FIXED_CHUNK_LINES = 100`
    - `LV_PORT_MAX_INFLIGHT_CHUNKS = 2`
  - `components/co5300_panel/co5300_panel_defaults.h`
    - `CO5300_PANEL_MAX_TRANSFER_LINES = 100`
    - `CO5300_PANEL_OPTIMIZED_PCLK_HZ = 50MHz`
    - `CO5300_PANEL_OPTIMIZED_TRANS_QUEUE_DEPTH = 8`
  - `sdkconfig`
    - `CONFIG_LV_DEF_REFR_PERIOD = 16`
    - `CONFIG_LV_DRAW_SW_CIRCLE_CACHE_SIZE = 4`
    - `CONFIG_LV_DRAW_SW_SHADOW_CACHE_SIZE = 4`
- 当前判断：
  - 这是一组“50MHz 上限下的交互顺滑档”，不是“全屏 60FPS 档”
  - 接下来如果继续追帧，优先看 `flush chunk`、`refr period` 和 UI 重绘面积，而不是再虚构更高的总线时钟

### 16. 30FPS 稳定档：启用 TE、收紧 TE 快速放行窗口、并发回到 `2`

- 触发原因：
  - 用户目标从“更高帧率”收敛为“30 帧稳定、尽量无撕裂”
  - 本地代码虽然已有 TE 中断、等待点和 `0x35/0x44` 初始化命令，但默认仍处于关闭状态
  - `co5300_panel_wait_te_signal()` 里原本 `6ms` 的快速放行窗口过宽，容易削弱 TE 同步效果
- 修改：
  - `components/co5300_panel/co5300_panel_defaults.h`
    - `CO5300_PANEL_USE_TE_SIGNAL = 1`
    - `CO5300_PANEL_OPTIMIZED_PCLK_HZ = 50MHz`
    - `CO5300_PANEL_MAX_TRANSFER_LINES = 100`
  - `components/lvgl_port/lv_port_config.h`
    - `LV_PORT_FIXED_CHUNK_LINES1 = 512`
    - `LV_PORT_FIXED_CHUNK_LINES2 = 512`
    - `LV_PORT_FIXED_CHUNK_LINES = 100`
    - `LV_PORT_MAX_INFLIGHT_CHUNKS = 2`
  - `components/co5300_panel/co5300_panel.c`
    - 将 TE 快速放行窗口从 `6000us` 收紧到 `2000us`
- 当前判断：
  - 这是一组“30FPS 稳定档”的最小可运行起点，重点在“什么时候发”，而不是继续堆大 chunk 或 inflight
  - `帧首等待 TE + inflight=2` 适合先做真机 A/B；若撕裂仍明显，再继续看 `chunk` 和等待策略
- 已验证：
  - 源码级测试通过：
    - `tests.test_co5300_panel_defaults_source`
    - `tests.test_co5300_te_sync_source`
    - `tests.test_lv_port_chunk_config_source`
    - `tests.test_lv_port_dma_bounce_source`
    - `tests.test_lv_port_module_split_source`
  - `idf.py build` 通过

### 17. TE 已启用但仍有撕裂：优先恢复片内 DMA bounce buffer

- 真机日志证据：
  - `co5300_panel: TE configured (Mode: 0x00)`
  - `co5300_panel: CO5300 init OK (TE enabled, mode: 0x00)`
  - `lv_port: LCD bounce buffer[0] 分配失败，回退到直接发送渲染缓冲`
- 当前判断：
  - 这说明 TE 链路已经真的生效，但 `lv_port` 仍在走“直接发送 PSRAM 渲染缓冲”的退化路径
  - 因此当前撕裂更像不是“没等 TE”，而是“等到了 TE，但 DMA/传输缓冲仍不受控”
- 收敛修改：
  - `components/lvgl_port/lv_port_config.h`
    - `LV_PORT_FIXED_CHUNK_LINES = 64`
  - `components/co5300_panel/co5300_panel_defaults.h`
    - `CO5300_PANEL_MAX_TRANSFER_LINES = 64`
  - 保持不变：
    - `LV_PORT_FIXED_CHUNK_LINES1 = 512`
    - `LV_PORT_FIXED_CHUNK_LINES2 = 512`
    - `LV_PORT_MAX_INFLIGHT_CHUNKS = 2`
    - `CO5300_PANEL_USE_TE_SIGNAL = 1`
    - `CO5300_PANEL_OPTIMIZED_PCLK_HZ = 50MHz`
- 目的：
  - 优先把两块片内 DMA bounce buffer 真正分配出来
  - 若启动日志从“分配失败”变成“LCD bounce buffer: 2 x ... bytes (Internal DMA)”，说明这条假设成立

### 18. `64` 行时第 2 块 bounce buffer 仍失败，继续收敛到 `48`

- 真机日志证据：
  - `LCD bounce buffer[1] 分配失败，回退到直接发送渲染缓冲`
- 当前判断：
  - 这说明当前片内 DMA 内存已经够第 1 块，但仍不够第 2 块
  - 在保持 `inflight=2` 不变的前提下，最小单变量就是继续缩小每块发送缓冲
- 收敛修改：
  - `components/lvgl_port/lv_port_config.h`
    - `LV_PORT_FIXED_CHUNK_LINES = 48`
  - `components/co5300_panel/co5300_panel_defaults.h`
    - `CO5300_PANEL_MAX_TRANSFER_LINES = 48`
- 目的：
  - 把两块片内 DMA bounce buffer 的总需求从约 `105KB` 再往下压
  - 观察启动日志是否转成 `LCD bounce buffer: 2 x ... bytes (Internal DMA)`

### 19. 双 bounce buffer 已成功后，单变量验证 `inflight=1`

- 真机日志证据：
  - `LCD bounce buffer: 2 x 59040 bytes (Internal DMA)`
- 当前判断：
  - 这说明“TE + 双 bounce buffer”链路已经健康
  - 如果屏幕上仍然有撕裂感，最可疑的就不再是内存，而是 `inflight=2` 让两个 chunk 跨到不同扫描时刻
- A/B 修改：
  - `components/lvgl_port/lv_port_config.h`
    - `LV_PORT_MAX_INFLIGHT_CHUNKS = 1`
  - 保持不变：
    - `LV_PORT_FIXED_CHUNK_LINES = 48`
    - `CO5300_PANEL_MAX_TRANSFER_LINES = 48`
    - `CO5300_PANEL_USE_TE_SIGNAL = 1`
    - `CO5300_PANEL_OPTIMIZED_PCLK_HZ = 50MHz`
- 目的：
  - 只验证“剩余撕裂是否主要来自两个 chunk 同时在飞”
  - 若撕裂明显下降，就说明后续该围绕 `inflight` 和 TE 等待策略继续优化

### 20. `inflight=1` 反而更差，说明主问题不是“两个 chunk 同时在飞”

- 真机反馈：
  - `inflight=1` 后“更加撕裂了，而且刷新率感觉变低了”
- 当前判断：
  - 这说明当前链路里，串行化 chunk 会把整帧发送时间进一步拉长
  - TE 已启用、双 bounce buffer 已成功的前提下，单纯降低并发并不能解决剩余撕裂
  - 后续优化重点应从“chunk 并发数量”转到“TE 等待语义是否真的在等待下一次空白期”
- 收敛修改：
  - `components/lvgl_port/lv_port_config.h`
    - `LV_PORT_MAX_INFLIGHT_CHUNKS` 恢复到 `2`
  - `sdkconfig`
    - `CONFIG_LV_DEF_REFR_PERIOD = 33`

### 21. TE 等待策略从“时间窗口猜测”改为“实时电平 + 清空旧 token + 等下一次上升沿”

- 触发证据：
  - 真机日志已经显示：
    - `co5300_panel: TE configured (Mode: 0x00)`
    - `co5300_panel: CO5300 init OK (TE enabled, mode: 0x00)`
    - `lv_port: LCD bounce buffer: 2 x 59040 bytes (Internal DMA)`
  - 但用户仍反馈“还是有撕裂感”，说明问题已不再是 TE 没开或 bounce buffer 没站起来
- 根因判断：
  - 旧版 `co5300_panel_wait_te_signal()` 只靠“距离上次 TE 中断 < 2000us”做快速放行，并直接 `xSemaphoreTake()`。
  - 由于这里使用的是二值信号量，若任务错过上一帧 TE 后 token 仍残留，后续等待会立即消费旧 token，名字上叫“wait TE”，实际上并没有等到“下一次”空白期。
- 修正策略：
  - `components/co5300_panel/co5300_panel.c`
    - 先读 `gpio_get_level(CO5300_PANEL_PIN_TE)`；Mode 1 / `N=0` 下，TE 高电平本身就代表当前仍处于 `V-Porch`
    - 若当前不是高电平，则先 `while (xSemaphoreTake(..., 0) == pdTRUE)` 清空历史旧 token
    - 清空后再次检查实时电平
    - 仍不在空白期时，再真正阻塞等待下一次 TE 上升沿
- 当前判断：
  - 这次修的是 TE 同步语义本身，而不是再调经验参数
  - 若此后撕裂仍明显，下一步应继续看“是否需要 scanline 级等待策略”，而不是回头再怀疑 bounce buffer

### 22. 当前蓝牙分支已移除 `BOUNCE` flush 双路径

- 背景：
  - 用户明确要求当前分支不再保留 `BOUNCE` 路径
  - 因此 `components/lvgl_port` 不再维护“LEGACY/BOUNCE 双实现 + 宏开关”的并行结构
- 当前代码状态：
  - `components/lvgl_port/lv_port_config.h`
    - 已删除 `LV_PORT_FLUSH_MODE_LEGACY`
    - 已删除 `LV_PORT_FLUSH_MODE_BOUNCE`
    - 已删除 `LV_PORT_FLUSH_MODE` 选择宏
  - `components/lvgl_port/lv_port.c`
    - 已删除 `s_flush_done_sem`
    - 已删除 `s_tx_chunk_bufs`
    - 已删除 `s_tx_chunk_buf_size`
    - 已删除 `s_tx_chunk_buf_next`
  - `components/lvgl_port/lv_port_display.c`
    - 已删除 bounce buffer 分配与复制逻辑
    - 只保留直接对 `px_map` 做 `rgb565 swap` 后发送的路径
- 适用边界：
  - 本节描述的是当前 `codex/bluetooth` 分支的现状
  - 文档前文涉及 bounce buffer 的章节属于历史调参记录，代表过去实验结论，不再代表当前分支默认实现

### 23. BLE provisioning 活跃期新增 UI 主循环降载保护

- 触发证据：
  - 当前官方 BLE provisioning 真机日志显示：
    - `network_prov_mgr: Provisioning started with service name : NET_PROV_BLE`
    - 建链后 `protocomm_nimble: mtu update event ... mtu=256`
    - 随后连续出现 `spi_master: setup_dma_priv_buffer()` 与 `Display flush failed: ESP_ERR_NO_MEM`
  - 同期微信小程序已经能完成：
    - `proto-ver`
    - `security0 session`
    - `prov-scan`
  - 这说明主问题已从“协议没打通”收敛成“BLE/Wi-Fi scan 与显示 flush 抢内部 DMA 内存”
- 收敛修改：
  - `main/ui/ui_refresh_policy.c`
    - 新增对 `NETWORK_MANAGER_STATE_PROVISIONING_BLE` 的检测
    - 当前不再把“配网降载”做成顶层交互状态，而是拆成两层：
      - 交互状态：`ACTIVE / STANDBY / FORCE_ACTIVE`
      - 系统节流模式：`NORMAL / PROVISIONING`
    - BLE 配网活跃期：
      - 活跃态最小唤醒间隔抬到 `80ms`
      - STANDBY 态最小唤醒间隔抬到 `500ms`
    - 亮度仍只跟交互状态走，因此配网期间 `30s` 无触摸后仍会进入 STANDBY 并渐暗
    - UI 轮询不再调用会抢 `network_manager` 互斥锁的 `network_manager_get_state()`，
      改为使用无锁快照接口 `network_manager_get_state_cached()`
- 目的：
  - 保持“交互活跃语义”和“系统保护语义”正交，避免一个状态同时承担 dim 与限流两种职责
  - 不改触摸超时语义
  - 在 BLE 配网期间继续利用空闲 dim 和更慢轮询一起减少 `lv_timer_handler() -> flush` 的触发密度
  - 优先验证“显示总线降载后，内部 DMA 私有缓冲申请失败是否明显减少”
- 当前判断：
  - 当前最稳的实现不是把 `PROVISIONING_THROTTLED` 当成顶层状态，而是把它做成独立节流模式
  - 这样既保住了 STANDBY / 常亮的用户语义，也避免 UI 主循环被网络互斥锁拖慢
  - 若真机仍持续报 `ESP_ERR_NO_MEM`，下一步应继续看：
    1. Wi-Fi 管理页上是否还有额外高频对象刷新
    2. 是否需要在 BLE provisioning 期间进一步暂停部分后台 UI 驱动源
    3. 是否需要重新引入更稳的 `Internal + DMA` bounce buffer 路径

## 后续 agent 使用建议

- 后续继续调参前，先读本文，再读 [display-render-touch-transfer-pipeline.md](/D:/esp32S3/111/docs/context/knowledge/project/display-render-touch-transfer-pipeline.md)
- 新的实验记录请遵循：
  - 写清楚改了哪几个宏或函数
  - 写清楚用户真机现象，而不是只写“感觉更好/更差”
  - 明确区分“定位开关”和“准备长期保留的默认配置”
- 若后续实验失败两轮以上，应回到最小复现，不要同时再改 `chunk`、`PCLK`、`TE`、`color format` 多个变量
