/**
 * @file i2c_manager.c
 * @brief I2C总线统一管理实现
 */

#include "i2c_manager.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c.h"

static const char *TAG = "i2c_manager";

static bool s_legacy_ready = false;
static const i2c_port_t s_i2c_port = I2C_MANAGER_PORT;

/**
 * @brief 初始化I2C总线管理器
 */
esp_err_t i2c_manager_init(void)
{
    // 如果总线已初始化,直接返回成功
    if (s_legacy_ready) {
        return ESP_OK;
    }

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MANAGER_SDA_GPIO,
        .scl_io_num = I2C_MANAGER_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_DISABLE,
        .scl_pullup_en = GPIO_PULLUP_DISABLE,
        .master.clk_speed = I2C_MANAGER_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(s_i2c_port, &conf), TAG, "legacy param config failed");
    ESP_RETURN_ON_ERROR(i2c_driver_install(s_i2c_port, conf.mode, 0, 0, 0), TAG, "legacy driver install failed");
    s_legacy_ready = true;

    ESP_LOGI(TAG, "I2C initialized (legacy) SCL:%d SDA:%d Freq:%d",
             I2C_MANAGER_SCL_GPIO, I2C_MANAGER_SDA_GPIO, I2C_MANAGER_FREQ_HZ);

    return ESP_OK;
}

 

/**
 * @brief 反初始化I2C总线管理器
 */
esp_err_t i2c_manager_deinit(void)
{
    if (!s_legacy_ready) {
        return ESP_OK;
    }

    i2c_driver_delete(s_i2c_port);
    s_legacy_ready = false;
    return ESP_OK;
}

/**
 * @brief 扫描I2C总线上的所有设备
 */
esp_err_t i2c_manager_scan(void)
{
    if (!s_legacy_ready) {
        ESP_LOGE(TAG, "I2C not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "扫描I2C总线 (0x03-0x77)...");
    int found_count = 0;

    for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(s_i2c_port, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "  发现设备: 0x%02X", addr);
            found_count++;
            if (addr == 0x18) {
                ESP_LOGI(TAG, "    -> ES8311 DAC");
            } else if (addr == 0x38) {
                ESP_LOGI(TAG, "    -> FT3168/FT5x06 Touch");
            } else if (addr == 0x40) {
                ESP_LOGI(TAG, "    -> ES7210 ADC");
            }
        }
    }

    ESP_LOGI(TAG, "扫描完成, 共发现 %d 个设备", found_count);

    if (found_count == 0)
    {
        ESP_LOGW(TAG, "未发现任何I2C设备, 请检查:");
        ESP_LOGW(TAG, "  1. SCL/SDA连接");
        ESP_LOGW(TAG, "  2. 设备供电");
        ESP_LOGW(TAG, "  3. 上拉电阻");
    }

    return ESP_OK;

}

i2c_port_t i2c_manager_get_port(void)
{
    if (!s_legacy_ready) {
        ESP_LOGW(TAG, "I2C legacy driver not initialized");
    }
    return s_i2c_port;
}
