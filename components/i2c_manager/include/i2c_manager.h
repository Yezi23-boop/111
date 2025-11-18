/**
 * @file i2c_manager.h
 * @brief I2C总线统一管理接口
 *
 * 提供共享的I2C总线,供多个组件(触摸屏、音频codec等)复用
 */

#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

// I2C总线配置参数
#define I2C_MANAGER_PORT I2C_NUM_0 // I2C端口号
#define I2C_MANAGER_SCL_GPIO 14    // SCL引脚(GPIO14)
#define I2C_MANAGER_SDA_GPIO 15    // SDA引脚(GPIO15)
#define I2C_MANAGER_FREQ_HZ 100000 // I2C时钟频率(100kHz,降低以适应长走线和多设备)

    /**
     * @brief 初始化I2C总线管理器
     * @note 只会初始化一次,重复调用会直接返回成功
     * @return ESP_OK:成功, 其他:失败
     */
    esp_err_t i2c_manager_init(void);

    /**
     * @brief 获取I2C总线句柄
     * @note 调用前必须先调用i2c_manager_init()初始化
     * @return I2C总线句柄,如果未初始化则返回NULL
     */
    i2c_master_bus_handle_t i2c_manager_get_bus(void);

    /**
     * @brief 反初始化I2C总线管理器
     * @note 会删除I2C总线,所有使用该总线的设备必须先移除
     * @return ESP_OK:成功, 其他:失败
     */
    esp_err_t i2c_manager_deinit(void);

    /**
     * @brief 扫描I2C总线上的所有设备
     * @note 扫描范围: 0x03-0x77 (跳过保留地址)
     * @return ESP_OK:成功, 其他:失败
     */
    esp_err_t i2c_manager_scan(void);

#ifdef __cplusplus
}
#endif
