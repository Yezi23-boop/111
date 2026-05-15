#ifndef POWER_POLICY_H
#define POWER_POLICY_H

#include <stdbool.h>

#include "esp_err.h"

/*
 * 整机资源策略层：
 * - 只把电源快照和运行场景翻译成资源预算；
 * - 不直接读 PMIC 寄存器，不直接操作 LVGL 对象，也不直接启动模型；
 * - 第一阶段接入电源、维护窗口和 UI 活跃度事实，后续再接入 standby 触发源。
 */

#ifdef __cplusplus
extern "C"
{
#endif

    /** 整机资源策略状态。 */
    typedef enum
    {
        POWER_POLICY_STATE_ACTIVE = 0,       /**< 正常交互或普通运行态。 */
        POWER_POLICY_STATE_IDLE_DIM,         /**< UI 已短空闲降亮，但仍保持快速恢复。 */
        POWER_POLICY_STATE_STANDBY,          /**< 长空闲待机态，第一阶段暂不自动进入。 */
        POWER_POLICY_STATE_LOW_BATTERY_WARN, /**< 低电量预警预算。 */
        POWER_POLICY_STATE_CHARGING,         /**< 外部供电或充电预算。 */
        POWER_POLICY_STATE_MAINTENANCE,      /**< 高压维护窗口，第一阶段只保留枚举。 */
    } power_policy_state_t;

    /** power_policy 输出给后台服务和资源 owner 的只读预算。 */
    typedef struct
    {
        power_policy_state_t state;       /**< 当前整机资源状态。 */
        bool danger_detection_allowed;    /**< 是否允许后台危险识别运行。 */
        bool network_sync_allowed;        /**< 是否允许普通后台网络同步。 */
        bool maintenance_allowed;         /**< 是否允许模型验证、OTA、日志导出等维护任务。 */
        bool ui_high_refresh_allowed;     /**< UI 是否允许保持 active 刷新预算。 */
        bool haptic_alert_allowed;        /**< 未来接入震动后，P0 提醒是否允许使用触觉通道。 */
        bool low_battery_warn;            /**< 当前是否处于低电量保护预算。 */
        bool external_power_present;      /**< 当前是否检测到外部供电。 */
    } power_policy_budget_t;

    /**
     * @brief 初始化整机资源策略层。
     * @return ESP_OK 表示初始化成功或已经初始化。
     */
    esp_err_t power_policy_init(void);

    /**
     * @brief 启用整机资源策略层。
     *
     * 第一阶段没有独立任务，只发布初始预算日志；调用方通过
     * `power_policy_get_budget()` 获取最新预算。
     *
     * @return ESP_OK 表示策略层可用。
     */
    esp_err_t power_policy_start(void);

    /**
     * @brief 进入或退出高压维护窗口。
     *
     * 维护窗口用于 OTA、模型替换、模型验证和日志导出等会与 Safety Monitor
     * 争用麦克风、模型 RAM、Flash 或网络吞吐的任务。该接口只记录策略请求，
     * 不直接启动维护任务，也不直接停止具体 runtime；后台 manager 会通过预算
     * 看到危险识别不再允许运行。
     *
     * @param[in] active true 表示进入维护窗口，false 表示退出维护窗口。
     * @param[in] reason 日志原因，可为 NULL。
     * @return ESP_OK 表示状态已记录。
     */
    esp_err_t power_policy_set_maintenance_window(bool active,
                                                  const char *reason);

    /**
     * @brief 获取当前资源预算。
     * @return 当前根据 power_service 快照和 UI 活跃度快照计算出的资源预算。
     */
    power_policy_budget_t power_policy_get_budget(void);

    /**
     * @brief 将资源策略状态转换成日志友好的文本。
     * @param[in] state 资源策略状态。
     * @return 静态字符串。
     */
    const char *power_policy_state_text(power_policy_state_t state);

#ifdef __cplusplus
}
#endif

#endif // POWER_POLICY_H
