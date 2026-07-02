#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 DS2413 马达控制通路，并立即将马达置为关闭状态。
 *
 * 当前板卡连接关系：
 * - 1-Wire IO 使用 GPIO18，外部 R22 4.7k 上拉；
 * - DS2413 family code 使用板测值 0xBA；
 * - PIOA 控制 Q1 基极，`release` 打开马达，`pull-low` 关闭马达；
 * - PIOB 当前未使用，始终保持 release。
 *
 * @return ESP_OK 表示 DS2413 已找到且马达已写入关闭状态。
 */
esp_err_t board_ds2413_motor_init(void);

/**
 * @brief 设置马达开关状态。
 *
 * @param[in] enabled true 表示释放 PIOA 打开马达，false 表示拉低 PIOA 关闭马达。
 * @return ESP_OK 表示 DS2413 写入并回读校验成功。
 */
esp_err_t board_ds2413_motor_set_enabled(bool enabled);

/**
 * @brief 输出一次马达脉冲，结束后保证马达关闭。
 *
 * @param[in] on_ms 马达开启时长，单位 ms。
 * @return ESP_OK 表示脉冲输出完成并成功关闭马达。
 */
esp_err_t board_ds2413_motor_pulse(uint32_t on_ms);

#ifdef __cplusplus
}
#endif
