#include "danger_detection_view.h"

#include <stdlib.h>

#include "gui_guider.h"

struct danger_detection_view {
    lv_obj_t *screen;
    lv_obj_t *top_bar;
    lv_obj_t *back_btn;
    lv_obj_t *back_label;
    danger_detection_view_action_cb_t back_action_cb;
    void *user_data;
};

static void danger_detection_view_back_event(lv_event_t *e)
{
    danger_detection_view_t *view =
        (danger_detection_view_t *)lv_event_get_user_data(e);
    if (view == NULL || view->back_action_cb == NULL) {
        return;
    }

    view->back_action_cb(view->user_data);
}

danger_detection_view_t *danger_detection_view_create(
    const danger_detection_view_config_t *config)
{
    danger_detection_view_t *view =
        (danger_detection_view_t *)calloc(1, sizeof(danger_detection_view_t));
    if (view == NULL) {
        return NULL;
    }

    view->back_action_cb = config != NULL ? config->back_action_cb : NULL;
    view->user_data = config != NULL ? config->user_data : NULL;

    view->screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(view->screen);
    lv_obj_set_style_bg_color(view->screen, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(view->screen, LV_OPA_COVER, 0);

    view->top_bar = lv_obj_create(view->screen);
    lv_obj_remove_style_all(view->top_bar);
    lv_obj_set_size(view->top_bar, LV_PCT(100), 60);
    lv_obj_align(view->top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(view->top_bar, LV_OPA_TRANSP, 0);

    view->back_btn = lv_btn_create(view->top_bar);
    lv_obj_remove_style_all(view->back_btn);
    lv_obj_set_size(view->back_btn, 116, 52);
    lv_obj_align(view->back_btn, LV_ALIGN_TOP_LEFT, 10, 4);
    lv_obj_set_style_bg_opa(view->back_btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(view->back_btn, danger_detection_view_back_event,
                        LV_EVENT_CLICKED, view);

    view->back_label = lv_label_create(view->back_btn);
    lv_label_set_text(view->back_label, "<");
    lv_obj_set_style_text_font(view->back_label, &lv_font_montserratMedium_27,
                               0);
    lv_obj_center(view->back_label);

    danger_detection_view_set_visual_state(
        view, DANGER_DETECTION_VIEW_VISUAL_STATE_IDLE);

    return view;
}

void danger_detection_view_destroy(danger_detection_view_t *view)
{
    if (view == NULL) {
        return;
    }

    if (view->screen != NULL) {
        lv_obj_delete(view->screen);
    }
    free(view);
}

lv_obj_t *danger_detection_view_get_screen(danger_detection_view_t *view)
{
    if (view == NULL) {
        return NULL;
    }
    return view->screen;
}

void danger_detection_view_set_visual_state(
    danger_detection_view_t *view,
    danger_detection_view_visual_state_t state)
{
    if (view == NULL) {
        return;
    }

    if (state == DANGER_DETECTION_VIEW_VISUAL_STATE_ALERT) {
        lv_obj_set_style_bg_color(view->screen, lv_palette_main(LV_PALETTE_RED),
                                  0);
        lv_obj_set_style_text_color(view->back_label, lv_color_white(), 0);
        lv_obj_set_style_bg_color(view->top_bar, lv_palette_main(LV_PALETTE_RED),
                                  0);
    } else {
        lv_obj_set_style_bg_color(view->screen, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_color(view->back_label, lv_color_black(), 0);
        lv_obj_set_style_bg_color(view->top_bar, lv_color_hex(0xffffff), 0);
    }
}
