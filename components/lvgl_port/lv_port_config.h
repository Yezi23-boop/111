#pragma once

/*
 * LVGL 端口层静态配置
 * - 这里放与显示尺寸、缓冲策略、颜色字节序相关的编译期参数。
 * - 修改后通常需要重新编译并观察刷新性能与内存占用。
 */

#define LCD_WIDTH 410  // LCD 水平分辨率（像素）
#define LCD_HEIGHT 502 // LCD 垂直分辨率（像素）

// lv_port_disp_init_small 使用的缓冲行数（通常用于片内/DMA 优先策略）
#define LV_PORT_FIXED_CHUNK_LINES1 512
// lv_port_disp_init_single 使用的缓冲行数（通常用于 PSRAM 大块缓冲策略）
#define LV_PORT_FIXED_CHUNK_LINES2 512
// 显示路径选择开关：非 0 使用 small 路径，0 使用 single 路径
#define LV_PORT_FIXED_CHUNK_LINES23 0
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
 * 固定按 30 行切块，优先保持发送路径简单可控，便于继续围绕分块大小和面板时序做 A/B。
 */
#define LV_PORT_FIXED_CHUNK_LINES 30 // 固定传输行数
