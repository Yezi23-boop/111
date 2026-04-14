#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_panel_io.h"

/*
 * CO5300 面板适配层：
 * - 负责封装面板初始化和运行期控制能力；
 * - 对上层（LVGL、业务模块）暴露稳定的最小接口；
 * - 集中管理 TE 同步、亮度控制与传输完成回调。
 */

// 引入默认配置，供条件编译宏（如 CO5300_PANEL_USE_TE_SIGNAL）使用。
#include "co5300_panel_defaults.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 初始化 CO5300 面板
     * @return
     * - ESP_OK: 初始化成功
     * - 其他错误码: SPI/IO/面板命令初始化失败
     */
    esp_err_t co5300_panel_init(void);

    // 前向声明，避免在头文件暴露完整底层结构定义。
    struct esp_lcd_panel_io_t;
    struct esp_lcd_panel_t;

    /**
     * @brief 获取底层 panel/io 句柄
     * @param io 输出参数，可为 NULL；返回面板 IO 句柄（不转移所有权）
     * @param panel 输出参数，可为 NULL；返回 panel 句柄（不转移所有权）
     */
    esp_err_t co5300_panel_get_raw(struct esp_lcd_panel_io_t **io,
                                   struct esp_lcd_panel_t **panel);

#if CO5300_PANEL_USE_TE_SIGNAL
    /**
     * @brief 等待下一次 TE 信号
     * @param timeout_ms 超时时间（毫秒）；0 表示无限等待
     * @return
     * - ESP_OK: 成功等到 TE
     * - ESP_ERR_TIMEOUT: 超时
     * - ESP_ERR_INVALID_STATE: TE 未启用或未初始化
     */
    esp_err_t co5300_panel_wait_te_signal(uint32_t timeout_ms);
#endif

    /**
     * @brief 注册颜色传输完成回调
     * @param cbs 回调函数集合，通常只使用 on_color_trans_done
     * @param user_ctx 回调上下文指针，生命周期需覆盖回调使用期
     */
    esp_err_t co5300_panel_register_color_done_callback(
        const esp_lcd_panel_io_callbacks_t *cbs,
        void *user_ctx);

    /**
     * @brief 设置亮度寄存器值
     * @param value 亮度寄存器原始值，范围 0~255
     */
    esp_err_t co5300_panel_set_brightness(uint8_t value);

    /**
     * @brief 获取当前缓存的亮度寄存器值（0~255）
     */
    uint8_t co5300_panel_get_brightness(void);

    /**
     * @brief 按百分比设置亮度
     * @param percent 亮度百分比（0~100，超出会被钳位）
     */
    esp_err_t co5300_panel_set_brightness_percent(uint8_t percent);

    /**
     * @brief 获取当前亮度百分比（0~100）
     */
    uint8_t co5300_panel_get_brightness_percent(void);

#ifdef __cplusplus
}
#endif
