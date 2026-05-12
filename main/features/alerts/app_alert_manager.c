#include "app_alert_manager.h"

#include <stdbool.h>
#include <string.h>

#include "audio_alert_player.h"
#include "display_alert_adapter.h"
#include "esp_check.h"
#include "esp_log.h"

#define TAG "app_alert_manager"

/*
 * 告警管理器实现说明：
 * - 负责把“危险声音触发”这类业务事件转换成统一告警表现；
 * - 当前表现包含屏幕红色覆盖层和单次提示音；
 * - 通过来源去重，避免同一来源连续上报时反复重启动画或音频。
 */

typedef struct
{
    bool initialized;                   /**< 管理器是否已初始化。 */
    bool active;                        /**< 当前是否存在活动告警。 */
    bool traffic_audio_overlay_enabled; /**< 是否允许危险音触发屏幕红色覆盖层。 */
    app_alert_request_t active_request; /**< 当前活动告警的来源与标签。 */
} app_alert_manager_state_t;

static app_alert_manager_state_t s_alert_manager_state = {
    .initialized = false,
    .active = false,
    .traffic_audio_overlay_enabled = true,
    .active_request = {
        .source = APP_ALERT_SOURCE_NONE,
        .severity = APP_ALERT_SEVERITY_NONE,
        .label = APP_ALERT_LABEL_NONE,
    },
};

/**
 * @brief 将告警标签转换成中文日志文本。
 * @param[in] label 告警标签。
 * @return 适合日志打印的中文名称。
 */
static const char *app_alert_label_to_zh(app_alert_label_t label)
{
    switch (label)
    {
    case APP_ALERT_LABEL_HORN:
        return "喇叭";
    case APP_ALERT_LABEL_SIREN:
        return "警笛";
    case APP_ALERT_LABEL_DANGER:
        return "危险声音";
    case APP_ALERT_LABEL_NONE:
    default:
        return "无";
    }
}

/**
 * @brief 初始化应用级告警管理器。
 * @return `ESP_OK` 表示初始化成功或已初始化；其他错误表示依赖模块初始化失败。
 */
esp_err_t app_alert_manager_init(void)
{
    esp_err_t ret;

    if (s_alert_manager_state.initialized)
    {
        return ESP_OK;
    }

    ret = audio_alert_player_init();
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = display_alert_adapter_init();
    if (ret != ESP_OK)
    {
        return ret;
    }

    memset(&s_alert_manager_state.active_request, 0,
           sizeof(s_alert_manager_state.active_request));
    s_alert_manager_state.traffic_audio_overlay_enabled = true;
    s_alert_manager_state.initialized = true;
    return ESP_OK;
}

/**
 * @brief 上报一次告警请求。
 * @param[in] request 告警请求，必须包含来源和严重级别。
 * @return 参数非法或依赖未初始化时返回错误。
 *
 * 同一来源重复上报时只更新标签，不重新触发音频和覆盖层，避免提示抖动。
 */
esp_err_t app_alert_manager_raise(const app_alert_request_t *request)
{
    ESP_RETURN_ON_FALSE(request != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "alert request is required");
    ESP_RETURN_ON_FALSE(request->source != APP_ALERT_SOURCE_NONE,
                        ESP_ERR_INVALID_ARG, TAG, "alert source is required");
    ESP_RETURN_ON_FALSE(request->severity != APP_ALERT_SEVERITY_NONE,
                        ESP_ERR_INVALID_ARG, TAG,
                        "alert severity is required");
    ESP_RETURN_ON_FALSE(s_alert_manager_state.initialized, ESP_ERR_INVALID_STATE,
                        TAG, "app alert manager not initialized");

    /* 同一来源重复上报时只更新标签，避免重复拉起 UI 动画和提示音。 */
    const bool same_source_active =
        s_alert_manager_state.active &&
        s_alert_manager_state.active_request.source == request->source;
    /* 允许单独屏蔽 traffic_audio 覆盖层，但仍保留告警状态机和提示音行为。 */
    const bool overlay_enabled =
        !(request->source == APP_ALERT_SOURCE_TRAFFIC_AUDIO &&
          !s_alert_manager_state.traffic_audio_overlay_enabled);

    s_alert_manager_state.active_request = *request;

    if (same_source_active)
    {
        ESP_LOGI(TAG, "更新危险告警 类别=%s",
                 app_alert_label_to_zh(request->label));
        return ESP_OK;
    }

    esp_err_t ret = ESP_OK;
    if (overlay_enabled)
    {
        ret = display_alert_adapter_show_danger_overlay();
        if (ret != ESP_OK)
        {
            return ret;
        }
    }

    s_alert_manager_state.active = true;
    ret = audio_alert_player_play_warning_once();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "warning audio playback start failed: %s",
                 esp_err_to_name(ret));
    }

    ESP_LOGW(TAG, "进入危险告警 类别=%s", app_alert_label_to_zh(request->label));
    return ESP_OK;
}

/**
 * @brief 清除指定来源对应的活动告警。
 * @param[in] source 告警来源。
 * @return 若当前活动告警并非该来源，则静默返回 `ESP_OK`。
 */
esp_err_t app_alert_manager_clear(app_alert_source_t source)
{
    ESP_RETURN_ON_FALSE(s_alert_manager_state.initialized, ESP_ERR_INVALID_STATE,
                        TAG, "app alert manager not initialized");

    if (!s_alert_manager_state.active ||
        s_alert_manager_state.active_request.source != source)
    {
        return ESP_OK;
    }

    const bool overlay_enabled =
        !(source == APP_ALERT_SOURCE_TRAFFIC_AUDIO &&
          !s_alert_manager_state.traffic_audio_overlay_enabled);
    esp_err_t ret = ESP_OK;
    if (overlay_enabled)
    {
        ret = display_alert_adapter_hide_danger_overlay();
        if (ret != ESP_OK)
        {
            return ret;
        }
    }

    memset(&s_alert_manager_state.active_request, 0,
           sizeof(s_alert_manager_state.active_request));
    s_alert_manager_state.active = false;
    ESP_LOGI(TAG, "退出危险告警");
    return ESP_OK;
}

/**
 * @brief 设置 traffic_audio 来源的屏幕覆盖层开关。
 * @param[in] enabled true 表示允许显示危险覆盖层。
 * @return `ESP_OK` 表示设置成功；其他错误表示管理器尚未初始化。
 */
esp_err_t app_alert_manager_set_traffic_audio_overlay_enabled(bool enabled)
{
    ESP_RETURN_ON_FALSE(s_alert_manager_state.initialized, ESP_ERR_INVALID_STATE,
                        TAG, "app alert manager not initialized");

    s_alert_manager_state.traffic_audio_overlay_enabled = enabled;
    if (!enabled && s_alert_manager_state.active &&
        s_alert_manager_state.active_request.source ==
            APP_ALERT_SOURCE_TRAFFIC_AUDIO)
    {
        (void)display_alert_adapter_hide_danger_overlay();
    }
    return ESP_OK;
}
