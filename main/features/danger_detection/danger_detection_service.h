#ifndef DANGER_DETECTION_SERVICE_H
#define DANGER_DETECTION_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * 危险声音检测服务：
 * - 封装交通声音运行时与后处理告警回调；
 * - 对 UI 暴露统一状态快照，而不是底层推理链路细节；
 * - 负责把 horn / siren 等稳定标签转换成应用级告警动作；
 * - 支持两种推理后端：Edge Impulse (traffic_inference) 和 ESP-DL (espdl_inference)。
 */

#ifdef __cplusplus
extern "C"
{
#endif

    /* 危险检测服务生命周期状态。 */
    typedef enum
    {
        DANGER_DETECTION_STATE_IDLE = 0,
        DANGER_DETECTION_STATE_STARTING,
        DANGER_DETECTION_STATE_RUNNING,
        DANGER_DETECTION_STATE_STOPPING,
        DANGER_DETECTION_STATE_ERROR,
    } danger_detection_state_t;

    /* 对外暴露的稳定危险标签。 */
    typedef enum
    {
        DANGER_DETECTION_LABEL_NONE = 0,
        DANGER_DETECTION_LABEL_HORN,  // 喇叭类稳定标签
        DANGER_DETECTION_LABEL_SIREN, // 警笛类稳定标签
        DANGER_DETECTION_LABEL_DANGER,// 通用危险标签（ESP-DL 二分类模式）
    } danger_detection_label_t;

    /* 推理后端类型。 */
    typedef enum
    {
        DANGER_DETECTION_BACKEND_EDGE_IMPULSE = 0, /**< Edge Impulse TFLite 3 分类。 */
        DANGER_DETECTION_BACKEND_ESPDL_DUAL = 1,   /**< ESP-DL 双模型并行推理。 */
    } danger_detection_backend_t;

    typedef struct
    {
        danger_detection_state_t state;               /**< 服务当前状态。 */
        danger_detection_label_t stable_label;        /**< 后处理稳定标签。 */
        danger_detection_label_t last_detected_label; /**< 最近一次触发的标签。 */
        float last_detected_confidence;               /**< 最近一次触发的置信度。 */
        float horn_confidence;                        /**< 当前帧 horn 分数。 */
        float siren_confidence;                       /**< 当前帧 siren 分数。 */
        float danger_confidence;                      /**< ESP-DL danger 概率。 */
        uint32_t alert_sequence;                      /**< 告警触发序号，单调递增。 */
        esp_err_t last_error;                         /**< 最近一次错误码。 */
        bool danger_overlay_active;                   /**< UI 危险覆盖层当前是否激活。 */
        danger_detection_backend_t active_backend;    /**< 当前活跃的推理后端。 */
    } danger_detection_snapshot_t;

    /**
     * @brief 初始化危险检测服务。
     * @return `ESP_OK` 表示初始化成功或已初始化；其他错误表示依赖模块初始化失败。
     */
    esp_err_t danger_detection_service_init(void);

    /**
     * @brief 启动危险检测运行时（默认使用 Edge Impulse 后端）。
     * @return `ESP_OK` 表示已成功启动或之前已在运行；其他错误表示启动失败。
     */
    esp_err_t danger_detection_service_start(void);

    /**
     * @brief 使用指定后端启动危险检测运行时。
     *
     * @param[in] backend 推理后端类型。
     * @return `ESP_OK` 表示已成功启动；其他错误表示启动失败。
     */
    esp_err_t danger_detection_service_start_with_backend(
        danger_detection_backend_t backend);

    /**
     * @brief 停止危险检测运行时。
     * @param[in] timeout_ms 等待底层运行时停止的超时，单位为毫秒；传 `0` 使用默认值。
     * @return `ESP_OK` 表示停止成功；其他错误表示停止或清理回调失败。
     */
    esp_err_t danger_detection_service_stop(uint32_t timeout_ms);

    /**
     * @brief 获取当前危险检测快照。
     * @return 当前服务状态、稳定标签和实时分数的组合快照。
     */
    danger_detection_snapshot_t danger_detection_service_get_snapshot(void);

#ifdef __cplusplus
}
#endif

#endif // DANGER_DETECTION_SERVICE_H
