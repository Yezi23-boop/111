#include "danger_detection_view.h"

#include <stdlib.h>

#include "gui_guider.h"

struct danger_detection_view {
    lv_obj_t *screen;
    lv_obj_t *back_btn;
    lv_obj_t *back_label;
    lv_obj_t *content_layer;
    lv_obj_t *status_label;
    lv_obj_t *category_label;
    lv_obj_t *primary_result_label;
    lv_obj_t *scores_card;
    lv_obj_t *horn_title_label;
    lv_obj_t *horn_confidence_label;
    lv_obj_t *siren_title_label;
    lv_obj_t *siren_confidence_label;
    lv_obj_t *alert_layer;
    lv_obj_t *alert_badge;
    lv_obj_t *alert_icon_label;
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

static void danger_detection_view_set_text(lv_obj_t *label, const char *text)
{
    if (label == NULL) {
        return;
    }

    lv_label_set_text(label, text != NULL ? text : "");
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

    view->back_btn = lv_btn_create(view->screen);
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

    view->content_layer = lv_obj_create(view->screen);
    lv_obj_remove_style_all(view->content_layer);
    lv_obj_set_size(view->content_layer, LV_PCT(100), LV_PCT(100));

    view->status_label = lv_label_create(view->content_layer);
    lv_label_set_text(view->status_label, "LISTENING");
    lv_obj_set_style_text_font(view->status_label, &lv_font_montserratMedium_16,
                               0);
    lv_obj_set_style_text_letter_space(view->status_label, 3, 0);
    lv_obj_align(view->status_label, LV_ALIGN_TOP_MID, 0, 46);

    view->category_label = lv_label_create(view->content_layer);
    lv_label_set_text(view->category_label, "CURRENT: NONE");
    lv_obj_set_style_text_font(view->category_label, &lv_font_montserratMedium_12,
                               0);
    lv_obj_set_style_text_letter_space(view->category_label, 2, 0);
    lv_obj_set_style_text_color(view->category_label, lv_color_hex(0x6b7280), 0);
    lv_obj_align_to(view->category_label, view->status_label, LV_ALIGN_OUT_BOTTOM_MID,
                    0, 10);

    view->primary_result_label = lv_label_create(view->content_layer);
    lv_label_set_text(view->primary_result_label, "NONE");
    lv_obj_set_style_text_font(view->primary_result_label,
                               &lv_font_montserratMedium_58, 0);
    lv_obj_set_style_text_letter_space(view->primary_result_label, 3, 0);
    lv_obj_align(view->primary_result_label, LV_ALIGN_CENTER, 0, 28);

    view->scores_card = lv_obj_create(view->content_layer);
    lv_obj_remove_style_all(view->scores_card);
    lv_obj_set_size(view->scores_card, 320, 92);
    lv_obj_align(view->scores_card, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_radius(view->scores_card, 22, 0);
    lv_obj_set_style_bg_color(view->scores_card, lv_color_hex(0xf5f5f5), 0);
    lv_obj_set_style_bg_opa(view->scores_card, LV_OPA_COVER, 0);

    view->horn_title_label = lv_label_create(view->scores_card);
    lv_label_set_text(view->horn_title_label, "HORN");
    lv_obj_set_style_text_font(view->horn_title_label, &lv_font_montserratMedium_12,
                               0);
    lv_obj_set_style_text_letter_space(view->horn_title_label, 2, 0);
    lv_obj_set_style_text_color(view->horn_title_label, lv_color_hex(0x6b7280), 0);
    lv_obj_align(view->horn_title_label, LV_ALIGN_TOP_LEFT, 18, 14);

    view->horn_confidence_label = lv_label_create(view->scores_card);
    lv_label_set_text(view->horn_confidence_label, "--");
    lv_obj_set_style_text_font(view->horn_confidence_label,
                               &lv_font_montserratMedium_27, 0);
    lv_obj_align(view->horn_confidence_label, LV_ALIGN_BOTTOM_LEFT, 18, -10);

    view->siren_title_label = lv_label_create(view->scores_card);
    lv_label_set_text(view->siren_title_label, "SIREN");
    lv_obj_set_style_text_font(view->siren_title_label,
                               &lv_font_montserratMedium_12, 0);
    lv_obj_set_style_text_letter_space(view->siren_title_label, 2, 0);
    lv_obj_set_style_text_color(view->siren_title_label, lv_color_hex(0x6b7280), 0);
    lv_obj_align(view->siren_title_label, LV_ALIGN_TOP_RIGHT, -18, 14);

    view->siren_confidence_label = lv_label_create(view->scores_card);
    lv_label_set_text(view->siren_confidence_label, "--");
    lv_obj_set_style_text_font(view->siren_confidence_label,
                               &lv_font_montserratMedium_27, 0);
    lv_obj_align(view->siren_confidence_label, LV_ALIGN_BOTTOM_RIGHT, -18, -10);

    view->alert_layer = lv_obj_create(view->screen);
    lv_obj_remove_style_all(view->alert_layer);
    lv_obj_set_size(view->alert_layer, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(view->alert_layer, LV_OBJ_FLAG_HIDDEN);

    view->alert_badge = lv_obj_create(view->alert_layer);
    lv_obj_remove_style_all(view->alert_badge);
    lv_obj_set_size(view->alert_badge, 156, 156);
    lv_obj_set_style_radius(view->alert_badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(view->alert_badge, 6, 0);
    lv_obj_set_style_border_color(view->alert_badge, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(view->alert_badge, LV_OPA_TRANSP, 0);
    lv_obj_center(view->alert_badge);

    view->alert_icon_label = lv_label_create(view->alert_badge);
    lv_label_set_text(view->alert_icon_label, "!");
    lv_obj_set_style_text_font(view->alert_icon_label,
                               &lv_font_montserratMedium_58, 0);
    lv_obj_set_style_text_color(view->alert_icon_label, lv_color_white(), 0);
    lv_obj_center(view->alert_icon_label);

    const danger_detection_view_model_t initial_model = {
        .status_text = "LISTENING",
        .category_text = "CURRENT: NONE",
        .primary_result_text = "NONE",
        .horn_confidence_text = "--",
        .siren_confidence_text = "--",
        .alert_visible = false,
    };
    danger_detection_view_apply_model(view, &initial_model);

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

void danger_detection_view_apply_model(
    danger_detection_view_t *view,
    const danger_detection_view_model_t *model)
{
    if (view == NULL || model == NULL) {
        return;
    }

    danger_detection_view_set_text(view->status_label, model->status_text);
    danger_detection_view_set_text(view->category_label, model->category_text);
    danger_detection_view_set_text(view->primary_result_label,
                                   model->primary_result_text);
    danger_detection_view_set_text(view->horn_confidence_label,
                                   model->horn_confidence_text);
    danger_detection_view_set_text(view->siren_confidence_label,
                                   model->siren_confidence_text);

    if (model->alert_visible) {
        lv_obj_set_style_bg_color(view->screen, lv_palette_main(LV_PALETTE_RED),
                                  0);
        lv_obj_set_style_text_color(view->back_label, lv_color_white(), 0);
        lv_obj_add_flag(view->content_layer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(view->alert_layer, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_style_bg_color(view->screen, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_color(view->back_label, lv_color_black(), 0);
        lv_obj_clear_flag(view->content_layer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(view->alert_layer, LV_OBJ_FLAG_HIDDEN);
    }
}
