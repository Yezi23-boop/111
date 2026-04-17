#include "display_alert_adapter.h"

#include <stdbool.h>

#include "esp_check.h"
#include "esp_log.h"
#include "gui_guider.h"
#include "lvgl.h"

#define TAG "display_alert"

/*
 * 显示告警适配实现说明：
 * - 覆盖层对象按需惰性创建，避免未触发告警时占用多余对象；
 * - 跨线程只写 pending 标志，不直接碰 LVGL；
 * - 真正的 show/hide 动作统一在 `display_alert_adapter_process_ui()` 中执行。
 */

typedef struct
{
    bool initialized;           /**< 模块初始化状态。 */
    bool suppressed;            /**< 抑制标志；为 true 时即使收到 show 请求也不实际显示。 */
    volatile bool pending_show; /**< 跨线程 show 请求标记，业务线程写入，UI 线程清除。 */
    volatile bool pending_hide; /**< 跨线程 hide 请求标记，业务线程写入，UI 线程清除。 */
    lv_obj_t *danger_overlay;   /**< 顶层红色覆盖对象，仅 UI 线程负责创建和操作。 */
} display_alert_state_t;

static display_alert_state_t s_display_alert_state = {
    .initialized = false,
    .pending_show = false,
    .pending_hide = false,
    .danger_overlay = NULL,
};

/**
 * @brief 确保危险覆盖层对象已创建。
 *
 * 覆盖层挂在 LVGL top layer，而不是某个具体页面上，
 * 这样页面切换时也不会被普通组件遮挡或误删。
 *
 * @return 无返回值。
 *
 * @note 仅允许在 UI 线程调用。
 */
static void display_alert_ensure_overlay_created(void)
{
    if (s_display_alert_state.danger_overlay != NULL)
    {
        return;
    }

    lv_obj_t *parent = lv_layer_top();
    s_display_alert_state.danger_overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(s_display_alert_state.danger_overlay);
    lv_obj_set_size(s_display_alert_state.danger_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_display_alert_state.danger_overlay,
                              lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_bg_opa(s_display_alert_state.danger_overlay,
                            LV_OPA_COVER, 0);
    lv_obj_add_flag(s_display_alert_state.danger_overlay, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief 在 UI 线程中真正显示危险覆盖层。
 * @return 无返回值。
 *
 * @note 仅允许在 UI 线程调用。
 */
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

/**
 * @brief 在 UI 线程中真正隐藏危险覆盖层。
 * @return 无返回值。
 *
 * @note 仅允许在 UI 线程调用。
 */
static void display_alert_apply_hide(void)
{
    if (s_display_alert_state.danger_overlay == NULL)
    {
        return;
    }

    lv_obj_add_flag(s_display_alert_state.danger_overlay, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "danger overlay hidden");
}

/**
 * @brief 初始化显示告警适配层。
 *
 * 初始化阶段不立刻创建覆盖层对象，是为了避免无告警场景下增加额外 LVGL 对象和样式开销。
 *
 * @return `ESP_OK` 表示初始化成功。
 */
esp_err_t display_alert_adapter_init(void)
{
    s_display_alert_state.suppressed = false;
    s_display_alert_state.pending_show = false;
    s_display_alert_state.pending_hide = false;
    s_display_alert_state.initialized = true;
    return ESP_OK;
}

/**
 * @brief 请求显示危险覆盖层。
 * @return `ESP_OK` 表示请求已受理；
 *         `ESP_ERR_INVALID_STATE` 表示模块尚未初始化。
 *
 * @note 该函数本身不直接触碰 LVGL，对象显示会延迟到 UI 线程执行。
 */
esp_err_t display_alert_adapter_show_danger_overlay(void)
{
    ESP_RETURN_ON_FALSE(s_display_alert_state.initialized, ESP_ERR_INVALID_STATE,
                        TAG, "display alert adapter not initialized");
    s_display_alert_state.pending_show = true;
    s_display_alert_state.pending_hide = false;
    return ESP_OK;
}

/**
 * @brief 请求隐藏危险覆盖层。
 * @return `ESP_OK` 表示请求已受理；
 *         `ESP_ERR_INVALID_STATE` 表示模块尚未初始化。
 */
esp_err_t display_alert_adapter_hide_danger_overlay(void)
{
    ESP_RETURN_ON_FALSE(s_display_alert_state.initialized, ESP_ERR_INVALID_STATE,
                        TAG, "display alert adapter not initialized");
    s_display_alert_state.pending_hide = true;
    s_display_alert_state.pending_show = false;
    return ESP_OK;
}

/**
 * @brief 设置显示抑制开关。
 * @param[in] suppressed true 表示即使收到 show 请求也不实际显示覆盖层。
 * @return `ESP_OK` 表示设置成功；
 *         `ESP_ERR_INVALID_STATE` 表示模块尚未初始化。
 *
 * @note 切换到抑制态时会立即尝试隐藏当前覆盖层，避免旧告警残留在屏幕上。
 */
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

/**
 * @brief 在 LVGL 线程中处理待执行的显示/隐藏请求。
 *
 * 该函数应由 UI 主循环周期调用，保证所有 LVGL 对象操作都在同一线程发生。
 *
 * @return 无返回值。
 *
 * @note 若处于抑制模式，函数只会消费 hide 请求，不会执行 show 请求。
 */
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
