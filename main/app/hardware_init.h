#ifndef HARDWARE_INIT_H
#define HARDWARE_INIT_H

#include "esp_err.h"

/*
 * 硬件初始化入口：
 * - 负责把主工程依赖的基础外设和服务入口按正确顺序拉起；
 * - 该层只关心“系统是否具备继续运行的基础条件”，不等待联网成功；
 * - 启动后更高层的后台状态机再分别推进网络、聊天等业务能力。
 */

/**
 * @brief 按主链路顺序初始化基础硬件能力。
 *
 * 当前初始化顺序依次覆盖：
 * - NVS
 * - 音频资源与 SD
 * - audio codec
 * - board power
 * - 按键与配网入口
 *
 * @return `ESP_OK` 表示基础硬件初始化成功；
 *         其他错误表示关键基础能力初始化失败。
 *
 * @note 本函数不阻塞等待联网成功；联网建立由 `network_service` 在后台状态机中推进。
 */
esp_err_t hardware_init(void);

#endif // HARDWARE_INIT_H
