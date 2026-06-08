#ifndef MEMORY_WATCH_VIEW_H
#define MEMORY_WATCH_VIEW_H

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct memory_watch_view memory_watch_view_t;

/**
 * @brief 页面动作回调。
 *
 * 回调只表达用户意图，不能在 view 层直接访问网络、录音或 Hermes。
 */
typedef void (*memory_watch_view_action_cb_t)(void *user_data);

/**
 * @brief Hermes 手表页面回调配置。
 */
typedef struct
{
    memory_watch_view_action_cb_t back_cb;
    memory_watch_view_action_cb_t press_start_cb;
    memory_watch_view_action_cb_t release_send_cb;
    memory_watch_view_action_cb_t slide_cancel_cb;
    memory_watch_view_action_cb_t cancel_waiting_cb;
    memory_watch_view_action_cb_t cancel_clarification_cb;
    void *user_data;
} memory_watch_view_config_t;

/**
 * @brief Hermes 手表页面的只读渲染模型。
 */
typedef struct
{
    const char *top_status_text;
    const char *state_text;
    const char *user_text;
    const char *reply_text;
    const char *voice_button_text;
    bool voice_button_enabled;
    bool cancel_visible;
    bool cancel_is_clarification;
} memory_watch_view_model_t;

/**
 * @brief 创建独立 Hermes 手表页面。
 *
 * @param[in] config 页面动作回调配置，可以为 NULL。
 * @return 页面实例；内存不足时返回 NULL。
 */
memory_watch_view_t *memory_watch_view_create(
    const memory_watch_view_config_t *config);

/**
 * @brief 销毁 Hermes 手表页面。
 * @param[in] view 页面实例，可以为 NULL。
 */
void memory_watch_view_destroy(memory_watch_view_t *view);

/**
 * @brief 获取页面根 screen。
 * @param[in] view 页面实例。
 * @return LVGL screen 对象；view 为空时返回 NULL。
 */
lv_obj_t *memory_watch_view_get_screen(const memory_watch_view_t *view);

/**
 * @brief 应用 service 快照转换后的页面模型。
 *
 * 该函数只更新 LVGL 对象，不推进业务状态。
 */
void memory_watch_view_apply_model(memory_watch_view_t *view,
                                   const memory_watch_view_model_t *model);

#ifdef __cplusplus
}
#endif

#endif // MEMORY_WATCH_VIEW_H
