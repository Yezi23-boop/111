#include "apple_watch_s5_preview.h"

#include <stdio.h>

#define PREVIEW_W 410
#define PREVIEW_H 502

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, int32_t x, int32_t y,
                            int32_t w, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, w);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    return label;
}

static lv_obj_t *make_card(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                           uint32_t color, uint8_t opa)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(card, opa, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    return card;
}

static lv_obj_t *make_activity_ring(lv_obj_t *parent, int32_t x, int32_t y,
                                    int32_t size, uint32_t color, int32_t value)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_pos(arc, x, y);
    lv_obj_set_size(arc, size, size);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, value);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x232326), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, 255, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_INDICATOR);
    return arc;
}

static void make_round_button(lv_obj_t *parent, int32_t x, int32_t y, int32_t size,
                              uint32_t color, const char *text)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, size, size);
    lv_obj_set_style_radius(btn, size / 2, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(btn, 245, 0);
    lv_obj_set_style_shadow_width(btn, 18, 0);
    lv_obj_set_style_shadow_opa(btn, 75, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_pad_all(btn, 0, 0);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
    lv_obj_center(label);
}

static void make_metric_card(lv_obj_t *parent, int32_t x, int32_t y, int32_t w,
                             uint32_t accent, const char *title, const char *value,
                             const char *detail)
{
    lv_obj_t *card = make_card(parent, x, y, w, 112, 0x16171c, 255);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x07080b), 0);

    lv_obj_t *dot = lv_obj_create(card);
    lv_obj_set_pos(dot, 16, 18);
    lv_obj_set_size(dot, 12, 12);
    lv_obj_set_style_radius(dot, 6, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(accent), 0);
    lv_obj_set_style_border_width(dot, 0, 0);

    make_label(card, title, 34, 12, w - 48, &lv_font_montserrat_14, 0xa6a8b0);
    make_label(card, value, 16, 38, w - 32, &lv_font_montserrat_24, 0xffffff);
    make_label(card, detail, 18, 76, w - 36, &lv_font_montserrat_14, 0x8d9099);
}

lv_obj_t *agent_preview_apple_watch_s5_create(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, PREVIEW_W, PREVIEW_H);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_radius(root, 42, 0);
    lv_obj_set_style_clip_corner(root, true, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_grad_color(root, lv_color_hex(0x141015), 0);
    lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_pad_all(root, 0, 0);

    make_label(root, "10:09", 28, 22, 170, &lv_font_montserrat_48, 0xffffff);
    make_label(root, "Tue 7 May", 31, 77, 140, &lv_font_montserrat_16, 0x9da0a8);

    lv_obj_t *battery = make_card(root, 318, 32, 58, 28, 0x15161a, 255);
    lv_obj_set_style_radius(battery, 14, 0);
    make_label(battery, "82%", 12, 6, 40, &lv_font_montserrat_14, 0x75ff8f);

    lv_obj_t *hero = make_card(root, 24, 118, 362, 150, 0x0b0c10, 255);
    lv_obj_set_style_bg_grad_dir(hero, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_grad_color(hero, lv_color_hex(0x27111a), 0);

    make_activity_ring(hero, 22, 24, 102, 0xff2d55, 88);
    make_activity_ring(hero, 37, 39, 72, 0x32d74b, 66);
    make_activity_ring(hero, 52, 54, 42, 0x64d2ff, 42);
    make_label(hero, "Activity", 146, 26, 150, &lv_font_montserrat_22, 0xffffff);
    make_label(hero, "Move 520/600", 147, 62, 160, &lv_font_montserrat_16, 0xff6b86);
    make_label(hero, "Exercise 24m", 147, 88, 160, &lv_font_montserrat_16, 0x6cf77d);
    make_label(hero, "Stand 7/12", 147, 114, 160, &lv_font_montserrat_16, 0x7fdcff);

    make_metric_card(root, 24, 286, 172, 0xff453a, "Heart", "76 bpm", "Resting 62");
    make_metric_card(root, 214, 286, 172, 0x0a84ff, "Weather", "24 C", "Cloudy");

    lv_obj_t *dock = make_card(root, 24, 418, 362, 62, 0x121318, 245);
    lv_obj_set_style_radius(dock, 31, 0);
    make_round_button(dock, 19, 11, 40, 0xff9f0a, "M");
    make_round_button(dock, 88, 11, 40, 0x32d74b, "A");
    make_round_button(dock, 157, 11, 40, 0xbf5af2, "S");
    make_round_button(dock, 226, 11, 40, 0x64d2ff, "W");
    make_round_button(dock, 295, 11, 40, 0xff375f, "H");

    return root;
}

void setup_scr_agent_preview_apple_watch_s5(lv_obj_t **screen)
{
    if (screen == NULL) {
        return;
    }

    *screen = lv_obj_create(NULL);
    lv_obj_set_size(*screen, PREVIEW_W, PREVIEW_H);
    lv_obj_set_scrollbar_mode(*screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(*screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(*screen, 0, 0);
    lv_obj_set_style_pad_all(*screen, 0, 0);

    agent_preview_apple_watch_s5_create(*screen);
    lv_obj_update_layout(*screen);
}
