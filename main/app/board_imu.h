#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 终点姿态使用的表盘法向轴。
     *
     * 当前板级样本显示平放时 `AZ` 接近 `-1g`，第一版先按 `-Z`
     * 作为表盘可看方向。若后续换板或 IMU 贴装方向变化，只改板级配置。
     */
    typedef enum
    {
        BOARD_IMU_FACE_AXIS_NEG_Z = 0, /**< `accel_z` 小于阈值时认为表盘朝向可看方向。 */
    } board_imu_face_axis_t;

    /**
     * @brief 当前板的 IMU 布线、安装方向和第一版抬腕阈值。
     *
     * 该结构只保存板级事实和可调阈值，不保存运行时状态。
     * `imu_service` 读取它后组织 WoM/AE 窗口，QMI8658C 驱动只消费地址和寄存器配置。
     */
    typedef struct
    {
        uint8_t qmi_i2c_addr_7bit;       /**< QMI8658C 7-bit I2C 地址，当前原理图为 0x6B。 */
        gpio_num_t qmi_int1_gpio;        /**< QMI_INT1 连接到 ESP32-S3 的 GPIO。 */
        uint8_t wom_threshold_mg;        /**< WoM 阈值，1 mg/LSB。 */
        uint8_t wom_blanking_samples;    /**< WoM 启动后忽略的样本数。 */
        uint8_t wom_accel_fs_code;       /**< CTRL2 bit[6:4]，WoM 使用的加速度量程。 */
        uint8_t wom_accel_odr_code;      /**< CTRL2 bit[3:0]，WoM 使用的加速度 ODR。 */
        uint8_t motion_sample_count;      /**< WoM 后采集的原始六轴帧数。 */
        uint16_t motion_sample_period_ms; /**< 运动窗口和终点姿态采样间隔，单位 ms。 */
        uint8_t motion_accel_fs_code;     /**< CTRL2 bit[6:4]，运动窗口加速度量程。 */
        uint8_t motion_accel_odr_code;    /**< CTRL2 bit[3:0]，运动窗口加速度 ODR。 */
        uint8_t motion_gyro_fs_code;      /**< CTRL3 bit[6:4]，运动窗口陀螺仪量程。 */
        uint8_t motion_gyro_odr_code;     /**< CTRL3 bit[3:0]，运动窗口陀螺仪 ODR。 */
        int32_t accel_lsb_per_g;         /**< 当前加速度量程下 1g 对应的 raw LSB。 */
        uint8_t final_pose_sample_count; /**< 终点姿态稳定性窗口帧数。 */
        int32_t final_norm_min_mg;       /**< 终点 accel norm 下限，单位 mg。 */
        int32_t final_norm_max_mg;       /**< 终点 accel norm 上限，单位 mg。 */
        int32_t final_stability_max_mg;  /**< 终点窗口最大轴向变化阈值，单位 mg。 */
        board_imu_face_axis_t face_axis; /**< 表盘可看方向使用的加速度轴。 */
        int32_t face_axis_threshold_raw; /**< 表盘法向轴 raw 阈值。 */
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
