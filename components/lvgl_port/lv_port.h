
/**
 * @file lv_port.h
 * @brief LVGL移植层头文件
 * @details 定义LVGL在ESP32平台的初始化接口（简约命名）
 */

#ifndef _LV_PORT_H_
#define _LV_PORT_H_

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// 初始化函数
void lv_port_init_small(void); // 片内

#endif