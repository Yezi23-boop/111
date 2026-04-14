#ifndef DANGER_DETECTION_SERVICE_H
#define DANGER_DETECTION_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        DANGER_DETECTION_STATE_IDLE = 0,
        DANGER_DETECTION_STATE_STARTING,
        DANGER_DETECTION_STATE_RUNNING,
        DANGER_DETECTION_STATE_STOPPING,
        DANGER_DETECTION_STATE_ERROR,
    } danger_detection_state_t;

    typedef enum
    {
        DANGER_DETECTION_LABEL_NONE = 0,
        DANGER_DETECTION_LABEL_HORN,  // 喇叭类稳定标签
        DANGER_DETECTION_LABEL_SIREN, // 警笛类稳定标签
    } danger_detection_label_t;

    typedef struct
    {
        danger_detection_state_t state;               // 服务当前状态
        danger_detection_label_t stable_label;        // 后处理稳定标签
        danger_detection_label_t last_detected_label; // 最近一次触发标签
        float last_detected_confidence;               // 最近触发的置信度
        float horn_confidence;                        // 当前帧 horn 分数
        float siren_confidence;                       // 当前帧 siren 分数
        uint32_t alert_sequence;                      // 告警触发序号（单调递增）
        esp_err_t last_error;                         // 最近错误码
        bool danger_overlay_active;                   // UI 危险覆盖层是否激活
    } danger_detection_snapshot_t;

    esp_err_t danger_detection_service_init(void);
    esp_err_t danger_detection_service_start(void);
    esp_err_t danger_detection_service_stop(uint32_t timeout_ms);
    danger_detection_snapshot_t danger_detection_service_get_snapshot(void);

#ifdef __cplusplus
}
#endif

#endif // DANGER_DETECTION_SERVICE_H
