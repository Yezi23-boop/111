#ifndef HARDWARE_INIT_H
#define HARDWARE_INIT_H

#include "esp_err.h"

/**
 * @brief 硬件层统一初始化
 * @details 初始化NVS、WiFi、SPIFFS、SD卡、I2C总线和音频编解码器，并阻塞等待WiFi连接成功
 * @return esp_err_t ESP_OK: 初始化成功且WiFi已连接; 其他: 初始化失败
 */
esp_err_t hardware_init(void);

#endif // HARDWARE_INIT_H
