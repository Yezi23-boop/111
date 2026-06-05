#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "imu_motion.h"
#include "qmi8658c.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief IMU 软件抬腕服务状态。
     */
    typedef enum
    {
        IMU_SERVICE_STATE_STOPPED = 0, /**< 服务尚未启动。 */
        IMU_SERVICE_STATE_PROBING,     /**< 正在探测 QMI8658C。 */
        IMU_SERVICE_STATE_RUNNING,     /**< 正在等待 WoM 或处理短时 AE 窗口。 */
        IMU_SERVICE_STATE_ERROR,       /**< 最近一次探测、配置或采样失败。 */
    } imu_service_state_t;

    /** @brief WoM 触发后的软件抬腕确认结果。 */
    typedef enum
    {
        IMU_SERVICE_RAISE_REASON_NONE = 0,       /**< 尚未完成过运动窗口确认。 */
        IMU_SERVICE_RAISE_REASON_PASS,           /**< 软件运动规则和终点姿态均通过。 */
        IMU_SERVICE_RAISE_REASON_MOTION_REJECT,  /**< 原始加速度软件运动规则未通过。 */
        IMU_SERVICE_RAISE_REASON_FINAL_NORM,     /**< 终点加速度 norm 偏离 1g。 */
        IMU_SERVICE_RAISE_REASON_FINAL_UNSTABLE, /**< 终点加速度仍在明显变化。 */
        IMU_SERVICE_RAISE_REASON_FINAL_POSE      /**< 终点表盘法向不满足看表姿态。 */
    } imu_service_raise_reason_t;

    /**
     * @brief IMU 服务只读快照。
     *
     * getter 只复制 service task 已缓存的事实，不访问 I2C、不推进状态机。
     */
    typedef struct
    {
        imu_service_state_t state;          /**< 当前服务状态。 */
        bool present;                       /**< QMI8658C 是否通过 `WHO_AM_I` 探测。 */
        uint8_t who_am_i;                   /**< 最近一次 `WHO_AM_I`。 */
        uint8_t revision_id;                /**< 最近一次 `REVISION_ID`。 */
        bool int1_path_usable;               /**< true 表示 STATUSINT.INT1 与 GPIO21 电平一致。 */
        bool poll_fallback_active;           /**< true 表示 INT1 通路故障，正在短周期轮询 WoM。 */
        uint32_t sample_count;              /**< 成功读取的样本数。 */
        uint32_t wom_irq_count;             /**< GPIO21 收到的 QMI_INT1 边沿数。 */
        uint32_t wom_poll_event_count;      /**< INT1 故障降级轮询确认的 WoM 事件数。 */
        uint32_t wom_event_count;           /**< `STATUS1 bit2` 确认的 WoM 事件数。 */
        uint32_t spurious_irq_count;        /**< 未读到 WoM 位的中断/通知次数。 */
        uint32_t motion_window_count;        /**< WoM 后原始六轴运动窗口次数。 */
        uint32_t raise_detected_count;       /**< 软件规则确认通过的抬腕候选次数。 */
        bool last_raise_detected;            /**< 最近一次窗口是否判定为抬腕候选。 */
        imu_service_raise_reason_t last_raise_reason; /**< 最近一次窗口结果原因。 */
        uint32_t last_motion_sample_count;   /**< 最近一次窗口成功读取的原始六轴帧数。 */
        imu_motion_reason_t last_motion_reason; /**< 最近一次软件运动规则原因。 */
        int16_t last_motion_roll_delta_degrees; /**< 最近一次软件规则的 roll 变化。 */
        bool last_motion_pass;               /**< 最近一次软件运动规则是否通过。 */
        bool last_final_pose_pass;          /**< 最近一次终点姿态是否通过。 */
        int32_t last_final_accel_norm_mg;   /**< 最近一次终点加速度 norm，单位 mg。 */
        int32_t last_final_accel_stability_mg; /**< 最近一次终点稳定性，单位 mg。 */
        qmi8658c_raw_sample_t last_final_raw; /**< 最近一次终点姿态样本。 */
        int64_t last_sample_time_us;        /**< 最近一次样本时间戳，单位为微秒。 */
        int64_t last_wom_time_us;           /**< 最近一次确认 WoM 事件时间戳，单位为微秒。 */
        qmi8658c_raw_sample_t last_raw;     /**< 最近一次 QMI8658C 原始样本。 */
        qmi8658c_status1_t last_status1;    /**< 最近一次 STATUS1 解析结果。 */
        esp_err_t last_error;               /**< 最近一次错误码；正常为 `ESP_OK`。 */
    } imu_service_snapshot_t;

    /**
     * @brief 初始化 IMU 服务内部状态。
     *
     * @return `ESP_OK` 表示初始化完成。
     */
    esp_err_t imu_service_init(void);

    /**
     * @brief 启动 IMU 软件抬腕采样服务。
     *
     * 服务在后台 task 中完成 QMI8658C 探测、WoM 配置、`QMI_INT1(GPIO21)` 事件处理，
     * 并在 WoM 后临时开启原始六轴采样，由 `imu_motion` 做软件抬腕候选确认。
     * 第一版只输出日志和 snapshot，不直接点亮屏幕，也不启用 ESP sleep wakeup。
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

    /**
     * @brief 将软件抬腕确认原因转换为稳定日志文本。
     *
     * @param[in] reason 确认原因。
     * @return 静态字符串，不需要调用方释放。
     */
    const char *imu_service_raise_reason_text(imu_service_raise_reason_t reason);

#ifdef __cplusplus
}
#endif
