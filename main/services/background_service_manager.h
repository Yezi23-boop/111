#ifndef BACKGROUND_SERVICE_MANAGER_H
#define BACKGROUND_SERVICE_MANAGER_H

#include <stdbool.h>

#include "esp_err.h"
#include "services/power_policy.h"

/*
 * 后台功能开关层：
 * - 区分“用户允许后台运行”和“当前资源策略允许运行”；
 * - 第一阶段只管理危险识别后台能力；
 * - 不直接抢麦克风、不改模型阈值、不直接操作 LVGL。
 */

#ifdef __cplusplus
extern "C"
{
#endif

    /** 后台服务管理器对外快照。 */
    typedef struct
    {
        bool started;                    /**< 管理器后台任务是否已启动。 */
        bool danger_enabled_by_user;     /**< 用户是否允许危险识别后台运行。 */
        bool danger_allowed_by_policy;   /**< 当前 power_policy 是否允许危险识别运行。 */
        bool danger_runtime_running;     /**< Safety Monitor session 最近一次确认的运行状态。 */
        bool danger_blocked_by_foreground_audio; /**< 前台录音/语音是否正在占用麦克风。 */
        power_policy_state_t policy_state; /**< 最近一次使用的整机策略状态。 */
        esp_err_t last_error;            /**< 最近一次 session 启动、停止或恢复错误码。 */
    } background_service_manager_snapshot_t;

    /**
     * @brief 初始化后台服务管理器。
     * @return ESP_OK 表示初始化成功或已初始化。
     */
    esp_err_t background_service_manager_init(void);

    /**
     * @brief 启动后台服务管理器。
     *
     * 第一阶段会按用户开关和 power_policy 预算启动危险识别后台监听。
     *
     * @return ESP_OK 表示任务已启动或之前已启动。
     */
    esp_err_t background_service_manager_start(void);

    /**
     * @brief 设置危险识别后台功能用户开关。
     * @param[in] enabled true 表示用户允许后台危险识别运行。
     * @return ESP_OK 表示设置成功；其他错误表示底层启动或停止失败。
     */
    esp_err_t background_service_manager_set_danger_detection_enabled(
        bool enabled);

    /**
     * @brief 通知后台管理器前台音频会话是否正在占用麦克风。
     *
     * 前台录音/语音属于比 Safety Monitor 更高优先级的临时会话。该标志为 true
     * 时，后台管理器会暂停危险识别 runtime；恢复为 false 后，再按用户开关和
     * power_policy 预算决定是否重启。
     *
     * @param[in] active true 表示前台音频正在占用麦克风。
     * @param[in] reason 日志原因，可为 NULL。
     * @return ESP_OK 表示状态已记录且策略已同步；其他错误表示停止/恢复失败。
     */
    esp_err_t background_service_manager_set_foreground_audio_active(
        bool active, const char *reason);

    /**
     * @brief 获取后台服务管理器快照。
     * @return 当前后台功能开关、策略许可和最近错误。
     */
    background_service_manager_snapshot_t
    background_service_manager_get_snapshot(void);

#ifdef __cplusplus
}
#endif

#endif // BACKGROUND_SERVICE_MANAGER_H
