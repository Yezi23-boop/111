#include "display_alert_adapter.h"

#include <stdbool.h>

#include "esp_check.h"
#include "esp_log.h"
#include "gui_guider.h"
#include "lvgl.h"

#define TAG "display_alert"

typedef struct
{
    bool initialized;           // 模块初始化状态
    bool suppressed;            // 是否处于抑制模式（不显示覆盖层）
    volatile bool pending_show; // 跨线程 show 请求标记
    volatile bool pending_hide; // 跨线程 hide 请求标记
    lv_obj_t *danger_overlay;   // 顶层红色覆盖对象
} display_alert_state_t;

static display_alert_state_t s_display_alert_state = {
    .initialized = false,
    .pending_show = false,
    .pending_hide = false,
    .danger_overlay = NULL,
};

static void display_alert_ensure_overlay_created(void)
{
    if (s_display_alert_state.danger_overlay != NULL)
    {
        return;
    }

    lv_obj_t *parent = lv_layer_top(); // 覆盖层挂在最顶层，避免被页面组件遮挡
    s_display_alert_state.danger_overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(s_display_alert_state.danger_overlay);
    lv_obj_set_size(s_display_alert_state.danger_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_display_alert_state.danger_overlay,
                              lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_bg_opa(s_display_alert_state.danger_overlay,
                            LV_OPA_COVER, 0);
    lv_obj_add_flag(s_display_alert_state.danger_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void display_alert_apply_show(void)
{
    display_alert_ensure_overlay_created();
    if (s_display_alert_state.danger_overlay == NULL)
    {
        return;
    }

    lv_obj_move_foreground(s_display_alert_state.danger_overlay);
    lv_obj_clear_flag(s_display_alert_state.danger_overlay, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGW(TAG, "danger overlay shown");
}

static void display_alert_apply_hide(void)
{
    if (s_display_alert_state.danger_overlay == NULL)
    {
        return;
    }

    lv_obj_add_flag(s_display_alert_state.danger_overlay, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "danger overlay hidden");
}

esp_err_t display_alert_adapter_init(void)
{
    s_display_alert_state.suppressed = false;
    s_display_alert_state.pending_show = false;
    s_display_alert_state.pending_hide = false;
    s_display_alert_state.initialized = true;
    return ESP_OK;
}

esp_err_t display_alert_adapter_show_danger_overlay(void)
{
    ESP_RETURN_ON_FALSE(s_display_alert_state.initialized, ESP_ERR_INVALID_STATE,
                        TAG, "display alert adapter not initialized");
    s_display_alert_state.pending_show = true;
    s_display_alert_state.pending_hide = false;
    return ESP_OK;
}

esp_err_t display_alert_adapter_hide_danger_overlay(void)
{
    ESP_RETURN_ON_FALSE(s_display_alert_state.initialized, ESP_ERR_INVALID_STATE,
                        TAG, "display alert adapter not initialized");
    s_display_alert_state.pending_hide = true;
    s_display_alert_state.pending_show = false;
    return ESP_OK;
}

esp_err_t display_alert_adapter_set_suppressed(bool suppressed)
{
    ESP_RETURN_ON_FALSE(s_display_alert_state.initialized, ESP_ERR_INVALID_STATE,
                        TAG, "display alert adapter not initialized");

    s_display_alert_state.suppressed = suppressed;
    if (suppressed)
    {
        display_alert_apply_hide();
    }
    return ESP_OK;
}

void display_alert_adapter_process_ui(void)
{
    if (!s_display_alert_state.initialized)
    {
        return;
    }

    if (s_display_alert_state.suppressed)
    {
        if (s_display_alert_state.pending_hide)
        {
            s_display_alert_state.pending_hide = false;
            display_alert_apply_hide();
        }
        return;
    }

    if (s_display_alert_state.pending_show)
    {
        s_display_alert_state.pending_show = false;
        display_alert_apply_show();
    }
    else if (s_display_alert_state.pending_hide)
    {
        s_display_alert_state.pending_hide = false;
        display_alert_apply_hide();
    }
}
