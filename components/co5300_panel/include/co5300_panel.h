#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include "esp_lcd_panel_io.h"
// 增量改动：包含默认配置文件以支持条件编译
// 原因：TE信号相关的条件编译需要CO5300_PANEL_USE_TE_SIGNAL宏定义
#include "co5300_panel_defaults.h"
#ifdef __cplusplus
extern "C"
{
#endif

    // 单一初始化接口：内部自管句柄
    esp_err_t co5300_panel_init(void);

    // 提供底层句柄（只读用途），便于其它组件集成（如 LVGL）。
    // 如未初始化或句柄无效，返回 ESP_ERR_INVALID_STATE。
    struct esp_lcd_panel_io_t; // fwd decl to avoid leaking headers
    struct esp_lcd_panel_t;    // fwd decl
    esp_err_t co5300_panel_get_raw(struct esp_lcd_panel_io_t **io, struct esp_lcd_panel_t **panel);

    // 增量改动：重新添加TE同步相关接口
    // 原因：
    // - 用户要求启用TE信号同步功能，需要提供等待TE信号的API
    // - TE同步API允许LVGL在合适的时机进行显示刷新，避免撕裂现象
    // - 使用条件编译确保只在启用TE时提供相关接口
#if CO5300_PANEL_USE_TE_SIGNAL
    /**
     * @brief 等待TE信号
     * 
     * 阻塞等待下一个TE信号到达，用于同步显示刷新
     * 
     * @param timeout_ms 超时时间（毫秒），0表示无限等待
     * @return 
     *     - ESP_OK: 成功等到TE信号
     *     - ESP_ERR_TIMEOUT: 等待超时
     *     - ESP_ERR_INVALID_STATE: TE功能未启用或未初始化
     */
    esp_err_t co5300_panel_wait_te_signal(uint32_t timeout_ms);
#endif

    // 注册传输完成回调：用于在颜色数据传输完成时回调
    // 回调类型与结构来自 ESP-IDF 的 esp_lcd_panel_io.h
    // user_ctx 可传入 LVGL 的 disp_drv 指针以便回调中调用 lv_disp_flush_ready
    esp_err_t co5300_panel_register_color_done_callback(const esp_lcd_panel_io_callbacks_t *cbs, void *user_ctx);

#ifdef __cplusplus
}
#endif
