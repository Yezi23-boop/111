#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define IMU_SERVICE_SAMPLE_RATE_HZ 50U
#define IMU_SERVICE_WINDOW_FRAME_COUNT 200U

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief IMU 配置服务状态。
     */
    typedef enum
    {
        IMU_SERVICE_STATE_STOPPED = 0, /**< 服务尚未启动。 */
        IMU_SERVICE_STATE_PROBING,     /**< 正在探测当前板上的 IMU。 */
        IMU_SERVICE_STATE_RUNNING,     /**< IMU 已完成统一配置，GPIO21 ISR 已安装。 */
        IMU_SERVICE_STATE_ERROR,       /**< 最近一次探测或配置失败。 */
    } imu_service_state_t;

    /**
     * @brief IMU 服务只读快照。
     *
     * getter 只复制 service task 已缓存的事实，不访问 I2C、不推进状态机。
     */
    typedef struct
    {
        imu_service_state_t state; /**< 当前服务状态。 */
        bool present;              /**< 当前板上的 IMU 是否通过 `WHO_AM_I` 探测。 */
        bool configured;           /**< 是否已完成 `imu_sensor_config()` 统一配置。 */
        uint8_t who_am_i;          /**< 最近一次 `WHO_AM_I`。 */
        uint8_t revision_id;       /**< 最近一次 `REVISION_ID`。 */
        uint8_t accel_fs;          /**< 当前配置的加速度 full-scale 编码。 */
        uint8_t accel_odr;         /**< 当前配置的加速度 ODR 编码。 */
        uint8_t gyro_fs;           /**< 当前配置的陀螺仪 full-scale 编码。 */
        uint8_t gyro_odr;          /**< 当前配置的陀螺仪 ODR 编码。 */
        bool accel_enabled;        /**< true 表示已请求开启加速度计。 */
        bool gyro_enabled;         /**< true 表示已请求开启陀螺仪。 */
        bool int1_isr_installed;   /**< true 表示 service 已安装 ESP32 GPIO 侧 INT1 ISR。 */
        int int1_gpio;             /**< 当前板级 QMI_INT1 对应的 ESP32 GPIO 编号。 */
        int int1_level;            /**< 最近一次记录到的 INT1 GPIO 电平。 */
        uint32_t int1_irq_count;   /**< service task 已处理的 INT1 GPIO 中断次数。 */
        int64_t last_int1_irq_time_us; /**< 最近一次 INT1 GPIO 中断处理时间戳，单位微秒。 */
        bool sampling_active;      /**< true 表示 50Hz 周期采样循环已经运行。 */
        bool sample_window_ready;  /**< true 表示 4 秒 / 200 帧环形缓冲已经填满过。 */
        uint16_t sample_rate_hz;   /**< 当前 service 采样频率，单位 Hz。 */
        uint16_t window_frame_count; /**< 当前 service 窗口帧数。 */
        uint32_t sample_count;     /**< 成功读取并写入环形缓冲的样本数。 */
        uint32_t sample_error_count; /**< 周期采样读取失败次数。 */
        int64_t last_sample_time_us; /**< 最近一次样本时间戳，单位微秒。 */
        int32_t last_sample_interval_us; /**< 最近两次成功样本间隔，单位微秒。 */
        int64_t configured_time_us; /**< 最近一次配置完成时间戳，单位为微秒。 */
        esp_err_t last_error;      /**< 最近一次错误码；正常为 `ESP_OK`。 */
    } imu_service_snapshot_t;

    /**
     * @brief 初始化 IMU 服务内部状态。
     *
     * @return `ESP_OK` 表示初始化完成。
     */
    esp_err_t imu_service_init(void);

    /**
     * @brief 启动 IMU 配置服务。
     *
     * 服务在后台 task 中完成 IMU 探测、统一最大量程配置、ESP32 GPIO21 ISR 安装，
     * 并启动 50Hz 周期采样。第一版不启用芯片侧 WoM/INT 事件源。
     *
     * @return `ESP_OK` 表示 task 已启动或之前已启动；其他错误表示初始化或创建 task 失败。
     */
    esp_err_t imu_service_start(void);

    /**
     * @brief 复制 IMU 服务快照。
     *
     * @param[out] out 输出快照，不能为空。
     * @return `ESP_OK` 表示复制成功；`ESP_ERR_INVALID_ARG` 表示参数为空。
     */
    esp_err_t imu_service_get_snapshot(imu_service_snapshot_t *out);

    /**
     * @brief 将 IMU 服务状态转换为稳定日志文本。
     *
     * @param[in] state 服务状态。
     * @return 静态字符串，不需要调用方释放。
     */
    const char *imu_service_state_text(imu_service_state_t state);

#ifdef __cplusplus
}
#endif
