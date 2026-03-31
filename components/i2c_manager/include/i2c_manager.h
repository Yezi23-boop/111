#pragma once

#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_idf_version.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
#include "driver/i2c_master.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define I2C_MANAGER_PORT I2C_NUM_0
#define I2C_MANAGER_SCL_GPIO 14
#define I2C_MANAGER_SDA_GPIO 15
#define I2C_MANAGER_FREQ_HZ 400000

esp_err_t i2c_manager_init(void);
i2c_port_t i2c_manager_get_port(void);
esp_err_t i2c_manager_deinit(void);
esp_err_t i2c_manager_scan(void);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
i2c_master_bus_handle_t i2c_manager_get_bus_handle(void);
#endif

#ifdef __cplusplus
}
#endif
