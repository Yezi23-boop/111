#pragma once

#define LCD_WIDTH 410   // LCD宽度(像素)
#define LCD_HEIGHT 502  // LCD高度(像素)

#define LV_PORT_FIXED_CHUNK_LINES1 20    // 固定传输行数（平衡性能和稳定性）
#define LV_PORT_FIXED_CHUNK_LINES2 240    // 固定传输行数（平衡性能和稳定性）
#define LV_PORT_FIXED_CHUNK_LINES23 0    // 固定传输行数（平衡性能和稳定性）
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
 */
#define LV_PORT_FIXED_CHUNK_LINES 30    // 固定传输行数（平衡性能和稳定性）

