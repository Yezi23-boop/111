#pragma once

#include "esp_err.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * 初始化 FT5x06 触摸，并提供原生 touch 句柄
     */
    esp_err_t touch_ft5x06_init(void);

    /** 获取底层 touch 句柄（I2C IO 与 touch） */
    esp_err_t touch_ft5x06_get_handle(esp_lcd_touch_handle_t *out_touch);

#define TOUCH_FT5X06_I2C_NUM I2C_NUM_0
#define TOUCH_FT5X06_SCL_GPIO 14
#define TOUCH_FT5X06_SDA_GPIO 15
#define TOUCH_FT5X06_INT_GPIO 38
#define TOUCH_FT5X06_RST_GPIO 9
#define TOUCH_FT5X06_I2C_HZ 400000

#ifdef __cplusplus
}
#endif
