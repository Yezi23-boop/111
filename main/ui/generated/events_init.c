/*
* Copyright 2025 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

#include "events_init.h"
#include <stdio.h>
#include "lvgl.h"

#if LV_USE_GUIDER_SIMULATOR && LV_USE_FREEMASTER
#include "freemaster_client.h"
#endif

#include "custom.h" // 包含自定义函数的头文件
static int32_t status_bar_y_pos;
static int32_t start_y;
static bool is_dragging;
static bool status_top=0;

static void screen_main_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_SCREEN_LOAD_START:
    {

        break;
    }
    default:
        break;
    }
}

static void screen_main_cont_1_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_CLICKED:
    {
        lv_obj_set_y(guider_ui.screen_main_Dropdown_menu, -200);
        break;
    }
    default:
        break;
    }
}

static void screen_main_option_5_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_CLICKED:
    {
        ui_load_scr_animation(&guider_ui, &guider_ui.screen_time, guider_ui.screen_time_del, &guider_ui.screen_main_del, setup_scr_screen_time, LV_SCR_LOAD_ANIM_FADE_ON, 300, 300, false, true);
        break;
    }
    default:
        break;
    }
}

static void screen_main_option_7_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_CLICKED:
    {
        ui_load_scr_animation(&guider_ui, &guider_ui.screen_wallpaper, guider_ui.screen_wallpaper_del, &guider_ui.screen_main_del, setup_scr_screen_wallpaper, LV_SCR_LOAD_ANIM_FADE_ON, 300, 300, false, true);
        break;
    }
    default:
        break;
    }
}

static void screen_main_top_grab_area_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_PRESSED:
    {
        // 获取触摸点坐标
        lv_point_t p;
        lv_indev_get_point(lv_indev_get_act(), &p);

        int32_t status_bar_height = lv_obj_get_height(guider_ui.screen_main_top_grab_area);
        if (p.x >= 0 && p.x <= 410 && p.y >= 0 && p.y <= status_bar_height)
        {
            start_y = p.y;
            is_dragging = true;
        }
        break;
        break;
    }
    case LV_EVENT_PRESSING:
    {
        // 获取触摸点坐标
        lv_point_t p;
        lv_indev_get_point(lv_indev_get_act(), &p);

        if (is_dragging)
        {
            // 计算拖动的距离（当前Y - 开始Y）
            int32_t drag_distance = p.y - start_y;
            int32_t status_bar_height = lv_obj_get_height(guider_ui.screen_main_Dropdown_menu);

            // 只有向下拖动（drag_distance > 0）才响应
            if (drag_distance > 0)
            {
                // 更新状态栏位置：不超过完全显示（0）和完全隐藏（-状态栏高度）的范围
                status_bar_y_pos = LV_MAX(-status_bar_height + drag_distance, -status_bar_height); // 最低不低于-状态栏高度（完全隐藏）
                status_bar_y_pos = LV_MIN(status_bar_y_pos, 0);      // 最高不超过0（完全显示）

                // 实时更新状态栏的Y坐标
                lv_obj_set_pos(guider_ui.screen_main_Dropdown_menu, 0, status_bar_y_pos);
            }
        }
        break;
    }
    case LV_EVENT_RELEASED:
    {
        // 获取触摸点坐标
        lv_point_t p;
        lv_indev_get_point(lv_indev_get_act(), &p);
        if (is_dragging)
        {
            is_dragging = false;
            int32_t status_bar_height = lv_obj_get_height(guider_ui.screen_main_Dropdown_menu);
            int32_t half_height = status_bar_height / 2;

            // 吸附效果：如果拖动超过一半，则完全显示；否则，完全隐藏
            if (status_bar_y_pos > -half_height)
            {
                // 完全显示状态栏的动画
                lv_anim_t anim;
                lv_anim_init(&anim);
                lv_anim_set_var(&anim, guider_ui.screen_main_Dropdown_menu);
                lv_anim_set_values(&anim, status_bar_y_pos, 0);
                lv_anim_set_time(&anim, 300);
                lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
                lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_obj_set_y);
                lv_anim_start(&anim);

                status_top = 1;
                status_bar_y_pos = 0;
            }
            else
            {
                // 隐藏状态栏的动画
                lv_anim_t anim;
                lv_anim_init(&anim);
                lv_anim_set_var(&anim, guider_ui.screen_main_Dropdown_menu);
                lv_anim_set_values(&anim, status_bar_y_pos, -status_bar_height);
                lv_anim_set_time(&anim, 300);
                lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
                lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_obj_set_y);
                lv_anim_start(&anim);

                status_top = 0;
                status_bar_y_pos = -status_bar_height;
            }
        }
        break;
    }
    default:
        break;
    }
}

static void screen_main_Brightness_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_VALUE_CHANGED:
    {
        // 增量改动：修复LVGL 9.2 API兼容性问题
        // 原因：
        // - LVGL 8.x使用lv_img_set_pivot和lv_img_set_angle
        // - LVGL 9.2改为lv_image_set_pivot和lv_image_set_rotation
        // - 角度单位也从0.1度改为度数
        
        // 获取滑动条的当前值
        int32_t slider_value = lv_slider_get_value(guider_ui.screen_main_Brightness);

        // 计算旋转角度：将0-100的值映射到0-360度
        int32_t rotation_angle = (slider_value * 360) / 100;

        // 设置图片旋转中心（图片中心）- 使用LVGL 9.2 API
        lv_image_set_pivot(guider_ui.screen_main_img_1, 23, 23); // 图片大小为46x46，中心点为23,23

        // 设置旋转角度 - 使用LVGL 9.2 API（直接使用度数，不需要乘以10）
        lv_image_set_rotation(guider_ui.screen_main_img_1, rotation_angle);
        break;
    }
    default:
        break;
    }
}

void events_init_screen_main (lv_ui *ui)
{
    lv_obj_add_event_cb(ui->screen_main, screen_main_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->screen_main_cont_1, screen_main_cont_1_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->screen_main_option_5, screen_main_option_5_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->screen_main_option_7, screen_main_option_7_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->screen_main_top_grab_area, screen_main_top_grab_area_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->screen_main_Brightness, screen_main_Brightness_event_handler, LV_EVENT_ALL, ui);
}

static void screen_wallpaper_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_SCREEN_LOAD_START:
    {
        setup_horizontal_scroll(guider_ui.screen_wallpaper_cont_1);

        break;
    }
    default:
        break;
    }
}

static void screen_wallpaper_img_1_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_LONG_PRESSED_REPEAT:
    {
        ui_load_scr_animation(&guider_ui, &guider_ui.screen_main, guider_ui.screen_main_del, &guider_ui.screen_wallpaper_del, setup_scr_screen_main, LV_SCR_LOAD_ANIM_FADE_ON, 300, 300, true, true);
        lv_obj_set_style_bg_img_src(guider_ui.screen_main_cont_1, &_1_RGB565A8_410x502,LV_PART_MAIN | LV_STATE_DEFAULT);
        break;
    }
    default:
        break;
    }
}

static void screen_wallpaper_img_2_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_LONG_PRESSED_REPEAT:
    {
        ui_load_scr_animation(&guider_ui, &guider_ui.screen_main, guider_ui.screen_main_del, &guider_ui.screen_wallpaper_del, setup_scr_screen_main, LV_SCR_LOAD_ANIM_FADE_ON, 300, 300, true, true);
        lv_obj_set_style_bg_img_src(guider_ui.screen_main_cont_1, &_2_RGB565A8_410x502,LV_PART_MAIN | LV_STATE_DEFAULT);
        break;
    }
    default:
        break;
    }
}

static void screen_wallpaper_img_3_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_LONG_PRESSED_REPEAT:
    {
        ui_load_scr_animation(&guider_ui, &guider_ui.screen_main, guider_ui.screen_main_del, &guider_ui.screen_wallpaper_del, setup_scr_screen_main, LV_SCR_LOAD_ANIM_FADE_ON, 300, 300, true, true);
        lv_obj_set_style_bg_img_src(guider_ui.screen_main_cont_1, &_3_RGB565A8_410x502,LV_PART_MAIN | LV_STATE_DEFAULT);
        break;
    }
    default:
        break;
    }
}

static void screen_wallpaper_img_4_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_LONG_PRESSED_REPEAT:
    {
        ui_load_scr_animation(&guider_ui, &guider_ui.screen_main, guider_ui.screen_main_del, &guider_ui.screen_wallpaper_del, setup_scr_screen_main, LV_SCR_LOAD_ANIM_FADE_ON, 300, 300, true, true);
        lv_obj_set_style_bg_img_src(guider_ui.screen_main_cont_1, &_4_RGB565A8_410x502,LV_PART_MAIN | LV_STATE_DEFAULT);
        break;
    }
    default:
        break;
    }
}

void events_init_screen_wallpaper (lv_ui *ui)
{
    lv_obj_add_event_cb(ui->screen_wallpaper, screen_wallpaper_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->screen_wallpaper_img_1, screen_wallpaper_img_1_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->screen_wallpaper_img_2, screen_wallpaper_img_2_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->screen_wallpaper_img_3, screen_wallpaper_img_3_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->screen_wallpaper_img_4, screen_wallpaper_img_4_event_handler, LV_EVENT_ALL, ui);
}

static void screen_time_imgbtn_1_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_CLICKED:
    {
        ui_load_scr_animation(&guider_ui, &guider_ui.screen_main, guider_ui.screen_main_del, &guider_ui.screen_time_del, setup_scr_screen_main, LV_SCR_LOAD_ANIM_FADE_ON, 300, 300, true, true);
        break;
    }
    default:
        break;
    }
}

void events_init_screen_time (lv_ui *ui)
{
    lv_obj_add_event_cb(ui->screen_time_imgbtn_1, screen_time_imgbtn_1_event_handler, LV_EVENT_ALL, ui);
}


void events_init(lv_ui *ui)
{

}
