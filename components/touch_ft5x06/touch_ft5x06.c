#include "touch_ft5x06.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c.h"
#include "driver/gpio.h"  // 添加GPIO驱动头文件
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_ft5x06.h"
#include "co5300_panel_defaults.h" // 复用屏尺寸
#include "driver/i2c_master.h"
static const char *TAG = "touch_ft5x06";

static esp_lcd_touch_handle_t s_touch = NULL;

static esp_err_t touch_ft5x06_bus_init(void)
{
    const i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = TOUCH_FT5X06_SDA_GPIO,
        // 打开内部上拉，增强稳定性（若外部有强上拉可改为 DISABLE）
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = TOUCH_FT5X06_SCL_GPIO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = TOUCH_FT5X06_I2C_HZ,
        .clk_flags = 0,
    };
    esp_err_t err = i2c_param_config(TOUCH_FT5X06_I2C_NUM, &i2c_conf);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        return err;
    err = i2c_driver_install(TOUCH_FT5X06_I2C_NUM, i2c_conf.mode, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        return err;
    return ESP_OK;
}

esp_err_t touch_ft5x06_init(void)
{
    if (s_touch)
        return ESP_OK;
    ESP_RETURN_ON_ERROR(touch_ft5x06_bus_init(), TAG, "i2c bus init failed");

    // 预配置INT引脚内部上拉，确保FT3168信号稳定性
    // 注意：这是在官方驱动配置之前的额外保障措施
    const gpio_config_t int_gpio_pre_config = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = BIT64(TOUCH_FT5X06_INT_GPIO),
        .pull_up_en = GPIO_PULLUP_ENABLE,     // 启用内部上拉
        .pull_down_en = GPIO_PULLDOWN_DISABLE, // 禁用内部下拉
        .intr_type = GPIO_INTR_DISABLE        // 暂时禁用中断，由官方驱动后续配置
    };
    ESP_RETURN_ON_ERROR(gpio_config(&int_gpio_pre_config), TAG, "INT GPIO pre-config failed");

    esp_lcd_panel_io_handle_t tp_io = NULL;
    const esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)TOUCH_FT5X06_I2C_NUM, &tp_io_cfg, &tp_io), TAG, "new io i2c failed");

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = CO5300_PANEL_H_RES,
        .y_max = CO5300_PANEL_V_RES,
        .rst_gpio_num = TOUCH_FT5X06_RST_GPIO,
        .int_gpio_num = TOUCH_FT5X06_INT_GPIO,
        .levels = {.reset = 0, .interrupt = 0},
        .flags = {.swap_xy = 0, .mirror_x = 0, .mirror_y = 0},
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_ft5x06(tp_io, &tp_cfg, &s_touch), TAG, "new ft5x06 failed");
    return ESP_OK;
}

esp_err_t touch_ft5x06_get_handle(esp_lcd_touch_handle_t *out_touch)
{
    if (!out_touch)
        return ESP_ERR_INVALID_ARG;
    if (!s_touch)
        return ESP_ERR_INVALID_STATE;
    *out_touch = s_touch;
    return ESP_OK;
}
