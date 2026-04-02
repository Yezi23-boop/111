#pragma once

#define LCD_WIDTH 410  // LCD宽度(像素)
#define LCD_HEIGHT 502 // LCD高度(像素)

#define LV_PORT_FIXED_CHUNK_LINES1 512 // LVGL 片内渲染缓冲高度（滑动优先档：尽量减少 tile 边界带来的切割感）
#define LV_PORT_FIXED_CHUNK_LINES2 512 // LVGL 片外渲染缓冲高度（滑动优先档：接近整屏，减少手势滚动时的分片感）
#define LV_PORT_FIXED_CHUNK_LINES23 0  // 控制ram开关
/**
 * @brief 字节交换配置
 * @details 用于处理RGB565格式的字节序问题
 *
 * 设置为1时：启用字节交换，适用于需要交换高低字节的显示设备
 * 设置为0时：禁用字节交换，使用原始字节序
 *
 * CO5300面板使用QSPI接口，根据实际显示效果调整此配置
 */
#define LV_PORT_BYTE_SWAP_ENABLE 1

/* ========== 简化传输优化配置 ========== */

/**
 * @brief 启用基础分块传输处理
 * @details 只启用基本的分块传输和DMA同步，不使用复杂的动态管理
 *
 * 设置为1时：启用分块传输和DMA同步
 * 设置为0时：使用标准传输模式
 */
#define LV_PORT_CHUNKED_TRANSFER_ENABLE 1

/**
 * @brief 固定传输块大小配置
 * @details 使用固定的传输块大小，简单稳定
 *
 * 在 CO5300 `PCLK <= 50MHz` 的硬件上限下，全屏真 60FPS 几乎没有总线余量，
 * 当前优先目标改为“减少每帧块数 + 保持交互顺滑”，而不是继续按错误的 80MHz 假设调参。
 * 当前屏幕 410x502/RGB565 一帧约 411,640 字节，
 * flush chunk 设为 48 行时，全屏约 11 块，块数进一步增加，但更有机会拿到 2 个片内 DMA bounce buffer，
 * 从而避免退化到“直接发送 PSRAM 渲染缓冲”并放大撕裂感。
 */
#define LV_PORT_FIXED_CHUNK_LINES 48 // 固定传输行数（30FPS 稳定档：优先恢复 inflight=2 的片内 DMA bounce buffer）

/**
 * @brief 允许同时在 SPI 队列中的 chunk 数量
 * @details
 * - 设为 1：最稳，但刷新率更低
 * - 设为 2：当前 30FPS 稳定档，优先保留双 bounce buffer 吞吐
 * - 设为 1：已验证会进一步拉长整帧发送时间，导致刷新率下降且撕裂更明显
 * - 继续增大时，可能再次触发条纹、错块或内部 DMA 私有缓冲压力问题
 */
#define LV_PORT_MAX_INFLIGHT_CHUNKS 2
