#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define IMU_MOTION_HISTORY_SIZE 8U
#define IMU_MOTION_STATS_WINDOW 2U

    /**
     * @brief IMU 软件抬腕判定的拒绝或触发原因。
     *
     * 第一版用于串口日志和阈值调试，避免只看到 `true/false` 而无法判断
     * 是姿态不对、运动太剧烈，还是历史窗口尚未填满。
     */
    typedef enum
    {
        IMU_MOTION_REASON_WARMUP = 0,       /**< 历史窗口未满，暂不判定。 */
        IMU_MOTION_REASON_X_TILT,           /**< X 轴倾斜过大，不像正常抬腕轨迹。 */
        IMU_MOTION_REASON_UNSTABLE,         /**< 当前窗口方差过大，可能是甩动或抖动。 */
        IMU_MOTION_REASON_Y_ORIENTATION,    /**< Y 轴方向未进入抬腕候选姿态。 */
        IMU_MOTION_REASON_ROLL_TOO_SMALL,   /**< roll 变化未达到抬腕阈值。 */
        IMU_MOTION_REASON_RAISE_DETECTED,   /**< 满足软件抬腕候选条件。 */
    } imu_motion_reason_t;

    /**
     * @brief 归一化后的三轴加速度样本。
     *
     * `x/y/z` 使用 InfiniTime 风格的 binary milli-g 量纲：`1g = 1024`。
     * 传感器原始量程换算和轴映射由上层 service 完成。
     */
    typedef struct
    {
        int16_t x; /**< X 轴归一化加速度，单位为 1/1024 g。 */
        int16_t y; /**< Y 轴归一化加速度，单位为 1/1024 g。 */
        int16_t z; /**< Z 轴归一化加速度，单位为 1/1024 g。 */
    } imu_motion_sample_t;

    /**
     * @brief 软件抬腕判定阈值。
     *
     * 默认值参考 InfiniTime 的软件 `RaiseWrist` 判定，并保持同一加速度量纲。
     * 由于本板 QMI8658C 安装方向可能不同，后续应先通过日志确认轴映射再收敛阈值。
     */
    typedef struct
    {
        int16_t x_abs_threshold;       /**< 允许的 X 轴均值绝对值上限，单位为 1/1024 g。 */
        int16_t y_max_threshold;       /**< Y 轴均值必须小于等于该值，单位为 1/1024 g。 */
        int16_t y_unstable_threshold;  /**< Y 轴过低时额外检查 Z 轴方差，单位为 1/1024 g。 */
        uint32_t variance_threshold;   /**< 当前窗口方差上限，单位为 `(1/1024 g)^2`。 */
        int16_t roll_threshold_degrees; /**< 触发抬腕所需 roll 变化，单位为度。 */
    } imu_motion_config_t;

    /**
     * @brief 一次软件运动判定结果。
     */
    typedef struct
    {
        bool raise_detected;             /**< true 表示当前窗口满足软件抬腕候选条件。 */
        imu_motion_reason_t reason;      /**< 本次判定的触发或拒绝原因。 */
        int16_t x_mean;                  /**< 当前统计窗口 X 均值，单位为 1/1024 g。 */
        int16_t y_mean;                  /**< 当前统计窗口 Y 均值，单位为 1/1024 g。 */
        int16_t z_mean;                  /**< 当前统计窗口 Z 均值，单位为 1/1024 g。 */
        int16_t prev_y_mean;             /**< 历史统计窗口 Y 均值，单位为 1/1024 g。 */
        int16_t prev_z_mean;             /**< 历史统计窗口 Z 均值，单位为 1/1024 g。 */
        uint32_t x_variance;             /**< 当前统计窗口 X 方差。 */
        uint32_t y_variance;             /**< 当前统计窗口 Y 方差。 */
        uint32_t z_variance;             /**< 当前统计窗口 Z 方差。 */
        int16_t roll_delta_degrees;      /**< 当前窗口相对历史窗口的 roll 变化，单位为度。 */
        uint32_t samples_seen;           /**< 算法状态累计接收的样本数。 */
    } imu_motion_result_t;

    /**
     * @brief 软件运动识别状态。
     *
     * 该结构由调用方静态持有，不包含动态内存和同步原语。跨任务访问时由上层 owner
     * 负责互斥；算法组件本身保持纯状态机。
     */
    typedef struct
    {
        imu_motion_config_t config;                         /**< 当前阈值配置。 */
        imu_motion_sample_t history[IMU_MOTION_HISTORY_SIZE]; /**< 最新样本环形历史。 */
        uint8_t head;                                       /**< 最新样本写入位置。 */
        uint8_t count;                                      /**< 已填充历史长度，最大为 `IMU_MOTION_HISTORY_SIZE`。 */
        uint32_t samples_seen;                              /**< 累计样本计数。 */
    } imu_motion_state_t;

    /**
     * @brief 获取默认软件抬腕阈值。
     *
     * @return 默认阈值配置，量纲为 `1g = 1024`。
     */
    imu_motion_config_t imu_motion_default_config(void);

    /**
     * @brief 初始化软件运动识别状态。
     *
     * @param[out] state 算法状态，不能为空。
     * @param[in] config 阈值配置；传入 NULL 时使用默认配置。
     */
    void imu_motion_init(imu_motion_state_t *state,
                         const imu_motion_config_t *config);

    /**
     * @brief 输入一个归一化加速度样本并更新抬腕判定。
     *
     * @param[in,out] state 算法状态，不能为空。
     * @param[in] sample 归一化三轴加速度样本，不能为空。
     * @param[out] result 本次判定结果，不能为空。
     * @return true 表示当前样本触发软件抬腕候选。
     */
    bool imu_motion_update(imu_motion_state_t *state,
                           const imu_motion_sample_t *sample,
                           imu_motion_result_t *result);

    /**
     * @brief 将判定原因转换为稳定日志文本。
     *
     * @param[in] reason 判定原因。
     * @return 静态字符串，不需要调用方释放。
     */
    const char *imu_motion_reason_text(imu_motion_reason_t reason);

#ifdef __cplusplus
}
#endif
