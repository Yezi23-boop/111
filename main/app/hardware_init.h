#ifndef HARDWARE_INIT_H
#define HARDWARE_INIT_H

#include "esp_err.h"

/**
 * @brief 硬件层统一初始化
 * @details
 * 按正式主链路顺序初始化基础资源：
 * - NVS
 * - 音频资源与 SD
 * - audio codec
 * - board power
 * - 按键与配网入口
 *
 * 注意：本函数不阻塞等待联网成功，联网由 network_service 在后台推进。
 * @return esp_err_t ESP_OK: 基础硬件初始化成功; 其他: 初始化失败
 */
esp_err_t hardware_init(void);

#endif // HARDWARE_INIT_H
