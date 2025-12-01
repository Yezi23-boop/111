/**
 * @file co5300_panel_defaults.h
 * @brief CO5300 LCD面板默认配置
 */

#pragma once

#include "driver/spi_master.h"
#include "hal/spi_types.h"

/* ========== GPIO引脚定义 ========== */

#define CO5300_PANEL_PIN_PCLK 11 // QSPI时钟
#define CO5300_PANEL_PIN_CS 12   // 片选
#define CO5300_PANEL_PIN_D0 4    // 数据线0 (MOSI)
#define CO5300_PANEL_PIN_D1 5    // 数据线1
#define CO5300_PANEL_PIN_D2 6    // 数据线2 (WP)
#define CO5300_PANEL_PIN_D3 7    // 数据线3 (HOLD)
#define CO5300_PANEL_PIN_RST 8   // 复位
#define CO5300_PANEL_PIN_TE 13   // TE信号（同步信号）

/* ========== SPI配置 ========== */

#define CO5300_PANEL_HOST SPI2_HOST // SPI主机

/* ========== 显示分辨率 ========== */

#define CO5300_PANEL_H_RES 410 // 水平分辨率
#define CO5300_PANEL_V_RES 502 // 垂直分辨率

/* ========== 显示控制 ========== */

#define CO5300_PANEL_DEFAULT_BRIGHTNESS 0xFF // 默认亮度 (0x00~0xFF)
#define CO5300_PANEL_MAX_TRANSFER_LINES 30   // 单次传输最大行数 (增大缓冲区以支持更大块的传输)

/* TE信号配置 */
#define CO5300_PANEL_USE_TE_SIGNAL 0 // 1=启用TE同步, 0=禁用
#define CO5300_PANEL_TE_MODE 0x00    // 0x00=Mode 1 (仅V-Porch, 推荐), 0x01=Mode 2 (V-Porch+H-Porch)

/* ========== 性能优化 ========== */

#define CO5300_PANEL_OPTIMIZED_PCLK_HZ (80 * 1000 * 1000) // SPI时钟80MHz
#define CO5300_PANEL_OPTIMIZED_TRANS_QUEUE_DEPTH 64       // 传输队列深度

/* ========== LCD参数 ========== */

#define CO5300_PANEL_BIT_PER_PIXEL 16 // RGB565格式

/* ========== 配置宏 ========== */

// 优化的QSPI IO配置（提升时钟频率和队列深度）
#define CO5300_PANEL_IO_QSPI_CONFIG_OPTIMIZED(cs, cb, cb_ctx)          \
    {                                                                  \
        .cs_gpio_num = cs,                                             \
        .dc_gpio_num = -1,                                             \
        .spi_mode = 0,                                                 \
        .pclk_hz = CO5300_PANEL_OPTIMIZED_PCLK_HZ,                     \
        .trans_queue_depth = CO5300_PANEL_OPTIMIZED_TRANS_QUEUE_DEPTH, \
        .on_color_trans_done = cb,                                     \
        .user_ctx = cb_ctx,                                            \
        .lcd_cmd_bits = 32,                                            \
        .lcd_param_bits = 8,                                           \
        .flags = {                                                     \
            .quad_mode = true,                                         \
        },                                                             \
    }
