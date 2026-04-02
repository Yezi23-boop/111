/**
 * @file lv_port_input.c
 * @brief LVGL 触摸输入链路实现
 */

#include "esp_log.h"
#include "lv_port_internal.h"
#include "touch_ft5x06.h"

static void lv_port_indev_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;

    uint16_t x[1] = {0};
    uint16_t y[1] = {0};
    uint8_t point_num = 0;
    esp_err_t ret = touch_ft5x06_read_points(x, y, &point_num, 1);

    if (ret == ESP_OK && point_num > 0) {
        s_last_x = x[0];
        s_last_y = y[0];
        data->point.x = s_last_x;
        data->point.y = s_last_y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->point.x = s_last_x;
        data->point.y = s_last_y;
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void lv_port_indev_init(void)
{
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lv_port_indev_read);

    ESP_LOGI(LV_PORT_TAG, "LVGL 9.3 输入设备初始化完成");
}

void lv_port_touch_init(void)
{
    if (touch_ft5x06_init() == ESP_OK) {
        if (touch_ft5x06_get_handle(&s_touch) == ESP_OK) {
            ESP_LOGI(LV_PORT_TAG, "FT5x06 触摸初始化完成");
        } else {
            ESP_LOGE(LV_PORT_TAG, "获取触摸句柄失败");
            s_touch = NULL;
        }
    } else {
        ESP_LOGE(LV_PORT_TAG, "FT5x06 触摸初始化失败");
        s_touch = NULL;
    }
}
