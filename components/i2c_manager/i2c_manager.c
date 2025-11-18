/**
 * @file i2c_manager.c
 * @brief I2C总线统一管理实现
 */

#include "i2c_manager.h" // I2C管理器头文件
#include "esp_log.h"     // ESP日志系统
#include "esp_check.h"   // ESP错误检查宏

static const char *TAG = "i2c_manager"; // 日志标签

// 全局I2C总线句柄(可供多个组件共享)
static i2c_master_bus_handle_t s_i2c_bus = NULL;

/**
 * @brief 初始化I2C总线管理器
 */
esp_err_t i2c_manager_init(void)
{
    // 如果总线已初始化,直接返回成功
    if (s_i2c_bus)
    {
        ESP_LOGW(TAG, "I2C bus already initialized");
        return ESP_OK;
    }

    // I2C主机总线配置
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_MANAGER_PORT,       // I2C端口号(I2C_NUM_0)
        .sda_io_num = I2C_MANAGER_SDA_GPIO, // SDA引脚(GPIO15)
        .scl_io_num = I2C_MANAGER_SCL_GPIO, // SCL引脚(GPIO14)
        .clk_source = I2C_CLK_SRC_DEFAULT,  // 使用默认时钟源
        .glitch_ignore_cnt = 7,             // 毛刺过滤计数(7个时钟周期)
        .flags = {
            .enable_internal_pullup = false, // 禁用内部上拉,使用外部上拉电阻
        },
    };

    // 创建I2C主机总线
    ESP_RETURN_ON_ERROR(
        i2c_new_master_bus(&bus_cfg, &s_i2c_bus),
        TAG,
        "Failed to create I2C master bus");

    ESP_LOGI(TAG, "I2C bus initialized (SCL: GPIO%d, SDA: GPIO%d, Freq: %dHz)",
             I2C_MANAGER_SCL_GPIO, I2C_MANAGER_SDA_GPIO, I2C_MANAGER_FREQ_HZ);

    return ESP_OK;
}

/**
 * @brief 获取I2C总线句柄
 */
i2c_master_bus_handle_t i2c_manager_get_bus(void)
{
    // 如果未初始化,记录警告日志
    if (!s_i2c_bus)
    {
        ESP_LOGW(TAG, "I2C bus not initialized, call i2c_manager_init() first");
    }
    return s_i2c_bus; // 返回总线句柄(可能为NULL)
}

/**
 * @brief 反初始化I2C总线管理器
 */
esp_err_t i2c_manager_deinit(void)
{
    // 如果总线未初始化,直接返回成功
    if (!s_i2c_bus)
    {
        return ESP_OK;
    }

    // 删除I2C总线
    esp_err_t ret = i2c_del_master_bus(s_i2c_bus);
    if (ret == ESP_OK)
    {
        s_i2c_bus = NULL; // 清空句柄
        ESP_LOGI(TAG, "I2C bus deinitialized");
    }
    else
    {
        ESP_LOGE(TAG, "Failed to delete I2C bus: %s", esp_err_to_name(ret));
    }

    return ret;
}

/**
 * @brief 扫描I2C总线上的所有设备
 */
esp_err_t i2c_manager_scan(void)
{
    // 确保I2C总线已初始化
    if (!s_i2c_bus)
    {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "扫描I2C总线 (0x03-0x77)...");
    int found_count = 0;

    // 扫描有效的I2C地址范围
    for (uint8_t addr = 0x03; addr <= 0x77; addr++)
    {
        // 尝试探测设备 (50ms超时)
        esp_err_t ret = i2c_master_probe(s_i2c_bus, addr, 50);

        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "  发现设备: 0x%02X", addr);
            found_count++;

            // 标注已知设备
            if (addr == 0x18)
            {
                ESP_LOGI(TAG, "    -> ES8311 DAC");
            }
            else if (addr == 0x38)
            {
                ESP_LOGI(TAG, "    -> FT3168/FT5x06 Touch");
            }
            else if (addr == 0x40)
            {
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
