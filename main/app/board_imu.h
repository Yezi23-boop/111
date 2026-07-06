#pragma once

#include <stdint.h>

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 当前板级 IMU 安装方向中的表盘法向轴。
     *
     * 当前板级样本显示平放时 `accel_z` 接近 `-1g`。该枚举只描述
     * IMU 在板上的物理安装方向，不描述抬腕算法阈值。
     */
    typedef enum
    {
        BOARD_IMU_FACE_AXIS_NEG_Z = 0, /**< 表盘法向对应 QMI8658C 寄存器坐标系的 `-Z`。 */
    } board_imu_face_axis_t;

    /**
     * @brief 当前板的 IMU 布线和安装方向。
     *
     * 该结构只保存硬件事实，不保存运行时状态或抬腕策略参数。
     * QMI8658C driver 只消费 I2C 地址；`imu_service` 负责自己的运行 profile。
     */
    typedef struct
    {
        uint8_t qmi_i2c_addr_7bit;       /**< QMI8658C 7-bit I2C 地址，当前原理图为 0x6B。 */
        gpio_num_t qmi_int1_gpio;        /**< QMI_INT1 连接到 ESP32-S3 的 GPIO。 */
        board_imu_face_axis_t face_axis; /**< 表盘法向对应的 QMI8658C 芯片坐标轴。 */
    } board_imu_config_t;

    /**
     * @brief 返回当前板级 IMU 配置。
     *
     * @return 静态只读配置指针，调用方不得修改。
     */
    const board_imu_config_t *board_imu_get_config(void);

#ifdef __cplusplus
}
#endif
