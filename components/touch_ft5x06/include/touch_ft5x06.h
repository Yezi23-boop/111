#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * 初始化 FT5x06 触摸控制器
     */
    esp_err_t touch_ft5x06_init(void);

    /**
     * 读取触摸点坐标
     * @param x 输出X坐标数组
     * @param y 输出Y坐标数组
     * @param num_points 输出触摸点数量
     * @param max_points 最大读取点数
     */
    esp_err_t touch_ft5x06_read_points(uint16_t *x, uint16_t *y, uint8_t *num_points, uint8_t max_points);

    /**
     * 获取触摸控制器句柄
     */
    esp_err_t touch_ft5x06_get_handle(void **out_handle);

#define TOUCH_FT5X06_I2C_NUM I2C_NUM_0
#define TOUCH_FT5X06_SCL_GPIO 14
#define TOUCH_FT5X06_SDA_GPIO 15
#define TOUCH_FT5X06_INT_GPIO 38
#define TOUCH_FT5X06_RST_GPIO 9
#define TOUCH_FT5X06_I2C_HZ 400000

#ifdef __cplusplus
}
#endif
