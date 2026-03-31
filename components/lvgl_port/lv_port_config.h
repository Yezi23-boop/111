#pragma once

#define LCD_WIDTH 410  // LCD宽度(像素)
#define LCD_HEIGHT 502 // LCD高度(像素)

#define LV_PORT_FIXED_CHUNK_LINES1 502 // 片内ram (建议20-60行，太大内存不够)
#define LV_PORT_FIXED_CHUNK_LINES2 502 // 片外ram
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
 * 当前 AI 对话链路启用后，I2S/AFE 会显著增加片内 DMA 可用内存压力。
 * 如果分块过大，SPI 面板刷新在申请 priv TX buffer 时会失败，表现为：
 * `setup_dma_priv_buffer(): Failed to allocate priv TX buffer`
 * 因此这里先收紧到 10 行，优先保证实验链路下的显示稳定性。
 */
#define LV_PORT_FIXED_CHUNK_LINES 10 // 固定传输行数（优先降低 DMA 临时缓冲压力）
