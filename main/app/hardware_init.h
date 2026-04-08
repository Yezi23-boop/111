#ifndef HARDWARE_INIT_H
#define HARDWARE_INIT_H

#include "esp_err.h"

/**
 * @brief 硬件层统一初始化
 * @details 初始化NVS、WiFi组件、SPIFFS、SD卡、I2C总线和音频编解码器，但不阻塞等待WiFi连接成功
 * @return esp_err_t ESP_OK: 基础硬件初始化成功; 其他: 初始化失败
 */
esp_err_t hardware_init(void);

#endif // HARDWARE_INIT_H
