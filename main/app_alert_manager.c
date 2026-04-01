#include "app_alert_manager.h"

#include <stdbool.h>
#include <string.h>

#include "audio_alert_player.h"
#include "display_alert_adapter.h"
#include "esp_check.h"
#include "esp_log.h"

#define TAG "app_alert_manager"

typedef struct {
    bool initialized;
    bool active;
    app_alert_request_t active_request;
} app_alert_manager_state_t;

static app_alert_manager_state_t s_alert_manager_state = {
    .initialized = false,
    .active = false,
    .active_request = {
        .source = APP_ALERT_SOURCE_NONE,
        .severity = APP_ALERT_SEVERITY_NONE,
        .label = APP_ALERT_LABEL_NONE,
    },
};

static const char *app_alert_label_to_zh(app_alert_label_t label)
{
    switch (label) {
        case APP_ALERT_LABEL_HORN:
            return "喇叭";
        case APP_ALERT_LABEL_SIREN:
            return "警笛";
        case APP_ALERT_LABEL_NONE:
        default:
            return "无";
    }
}

esp_err_t app_alert_manager_init(void)
{
    esp_err_t ret;

    if (s_alert_manager_state.initialized) {
        return ESP_OK;
    }

    ret = audio_alert_player_init();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = display_alert_adapter_init();
    if (ret != ESP_OK) {
        return ret;
    }

    memset(&s_alert_manager_state.active_request, 0,
           sizeof(s_alert_manager_state.active_request));
    s_alert_manager_state.initialized = true;
    return ESP_OK;
}

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

    const bool same_source_active =
        s_alert_manager_state.active &&
        s_alert_manager_state.active_request.source == request->source;

    s_alert_manager_state.active_request = *request;

    if (same_source_active) {
        ESP_LOGI(TAG, "更新危险告警 类别=%s",
                 app_alert_label_to_zh(request->label));
        return ESP_OK;
    }

    esp_err_t ret = display_alert_adapter_show_danger_overlay();
    if (ret != ESP_OK) {
        return ret;
    }

    s_alert_manager_state.active = true;
    ret = audio_alert_player_play_warning_once();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "warning audio playback start failed: %s",
                 esp_err_to_name(ret));
    }

    ESP_LOGW(TAG, "进入危险告警 类别=%s", app_alert_label_to_zh(request->label));
    return ESP_OK;
}

esp_err_t app_alert_manager_clear(app_alert_source_t source)
{
    ESP_RETURN_ON_FALSE(s_alert_manager_state.initialized, ESP_ERR_INVALID_STATE,
                        TAG, "app alert manager not initialized");

    if (!s_alert_manager_state.active ||
        s_alert_manager_state.active_request.source != source) {
        return ESP_OK;
    }

    esp_err_t ret = display_alert_adapter_hide_danger_overlay();
    if (ret != ESP_OK) {
        return ret;
    }

    memset(&s_alert_manager_state.active_request, 0,
           sizeof(s_alert_manager_state.active_request));
    s_alert_manager_state.active = false;
    ESP_LOGI(TAG, "退出危险告警");
    return ESP_OK;
}
