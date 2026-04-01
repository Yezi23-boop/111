---
id: lvgl-display-tuning-log
tags: project, lvgl, display, co5300, tuning, debugging
summary: 按时间记录 LVGL 9.3 显示异常排查中的每轮参数修改、实测现象与当前结论，供后续 agent 直接复用，避免重复调参。
last_reviewed: 2026-04-01
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
- `main/lvgl_task.c` 与正常基线提交 `aa165525fddae2eb711da144c90539e33e0b67c` 的核心逻辑一致，目前不认为它是决定性根因

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
  - `LV_PORT_FIXED_CHUNK_LINES = 128`
  - `LV_PORT_CHUNKED_TRANSFER_ENABLE = 1`
  - `LV_PORT_MAX_INFLIGHT_CHUNKS = 2`
- `components/co5300_panel/co5300_panel_defaults.h`
  - `CO5300_PANEL_MAX_TRANSFER_LINES = 128`
  - `CO5300_PANEL_OPTIMIZED_PCLK_HZ = 50MHz`
- 这意味着：
  - LVGL render tile 已接近整屏，优先减少滑动画面的 tile 分片感
  - 面板 flush 仍按 `128` 行发送，全屏约 `4` 块，优先减少块管理开销
  - 当前最值得优化的是“主观顺滑度和局部交互 FPS”，而不是继续用错误的 80MHz 前提追全屏 60 帧

## 50MHz 上限下的推荐起点

- 若目标是“在真实硬件边界内尽量提帧”，当前建议以这组参数为起点：
  - `CONFIG_LV_DEF_REFR_PERIOD = 16`
  - `LV_PORT_FIXED_CHUNK_LINES1 = 512`
  - `LV_PORT_FIXED_CHUNK_LINES2 = 512`
  - `LV_PORT_FIXED_CHUNK_LINES = 128`
  - `LV_PORT_MAX_INFLIGHT_CHUNKS = 2`
  - `CO5300_PANEL_MAX_TRANSFER_LINES = 128`
  - `CO5300_PANEL_OPTIMIZED_PCLK_HZ = 50MHz`
- 这组的意义是：
  - 把单帧 flush 块数压到约 `4` 块
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

### 15. 按真实硬件上限收敛：`PCLK = 50MHz`，flush chunk 提到 `128`

- 触发原因：
  - 用户确认：`CO5300` 当前最高 `PCLK = 50MHz`
  - 继续沿用 `80MHz` 的 60FPS 尝试档会把后续所有提帧讨论建立在错误前提上
- 当前收敛配置：
  - `components/lvgl_port/lv_port_config.h`
    - `LV_PORT_FIXED_CHUNK_LINES1 = 512`
    - `LV_PORT_FIXED_CHUNK_LINES2 = 512`
    - `LV_PORT_FIXED_CHUNK_LINES = 128`
    - `LV_PORT_MAX_INFLIGHT_CHUNKS = 2`
  - `components/co5300_panel/co5300_panel_defaults.h`
    - `CO5300_PANEL_MAX_TRANSFER_LINES = 128`
    - `CO5300_PANEL_OPTIMIZED_PCLK_HZ = 50MHz`
    - `CO5300_PANEL_OPTIMIZED_TRANS_QUEUE_DEPTH = 8`
  - `sdkconfig`
    - `CONFIG_LV_DEF_REFR_PERIOD = 16`
    - `CONFIG_LV_DRAW_SW_CIRCLE_CACHE_SIZE = 4`
    - `CONFIG_LV_DRAW_SW_SHADOW_CACHE_SIZE = 4`
- 当前判断：
  - 这是一组“50MHz 上限下的交互顺滑档”，不是“全屏 60FPS 档”
  - 接下来如果继续追帧，优先看 `flush chunk`、`refr period` 和 UI 重绘面积，而不是再虚构更高的总线时钟

## 后续 agent 使用建议

- 后续继续调参前，先读本文，再读 [display-render-touch-transfer-pipeline.md](/D:/esp32S3/111/docs/context/knowledge/project/display-render-touch-transfer-pipeline.md)
- 新的实验记录请遵循：
  - 写清楚改了哪几个宏或函数
  - 写清楚用户真机现象，而不是只写“感觉更好/更差”
  - 明确区分“定位开关”和“准备长期保留的默认配置”
- 若后续实验失败两轮以上，应回到最小复现，不要同时再改 `chunk`、`PCLK`、`TE`、`color format` 多个变量
