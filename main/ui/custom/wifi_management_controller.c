#include "wifi_management_controller.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "services/network_service.h"

static const char *TAG = "wifi_mgmt_ui";
static const uint32_t kStatusRefreshMs = 300U;

static lv_ui *s_ui = NULL;
static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_status_title = NULL;
static lv_obj_t *s_status_detail = NULL;
static lv_obj_t *s_retry_saved_btn = NULL;
static lv_obj_t *s_disconnect_btn = NULL;
static lv_obj_t *s_reprovision_btn = NULL;
static lv_obj_t *s_transport_auto_btn = NULL;
static lv_obj_t *s_transport_ble_btn = NULL;
static lv_obj_t *s_transport_ap_btn = NULL;
static lv_timer_t *s_status_timer = NULL;

static void wifi_management_controller_refresh(void);
static void wifi_management_controller_ensure_screen_created(void);
static lv_obj_t *wifi_management_controller_create_action_button(
    lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y);
static lv_obj_t *wifi_management_controller_create_transport_button(
    lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y);

static void wifi_management_back_event_cb(lv_event_t *e)
{
    (void)e;

    if (s_ui == NULL || s_ui->screen_main == NULL)
    {
        return;
    }

    lv_screen_load_anim(s_ui->screen_main, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0,
                        true);
}

static void wifi_management_retry_saved_event_cb(lv_event_t *e)
{
    (void)e;
    (void)network_service_request_connect_with_saved_credentials();
    wifi_management_controller_refresh();
}

static void wifi_management_disconnect_event_cb(lv_event_t *e)
{
    (void)e;
    (void)network_service_request_disconnect();
    wifi_management_controller_refresh();
}

static void wifi_management_reprovision_event_cb(lv_event_t *e)
{
    (void)e;
    (void)network_service_request_reprovision();
    wifi_management_controller_refresh();
}

static void wifi_management_transport_auto_event_cb(lv_event_t *e)
{
    (void)e;
    (void)network_service_set_default_provision_transport(
        NETWORK_SERVICE_PROVISION_TRANSPORT_AUTO);
    wifi_management_controller_refresh();
}

static void wifi_management_transport_ble_event_cb(lv_event_t *e)
{
    (void)e;
    (void)network_service_set_default_provision_transport(
        NETWORK_SERVICE_PROVISION_TRANSPORT_BLE);
    wifi_management_controller_refresh();
}

static void wifi_management_transport_ap_event_cb(lv_event_t *e)
{
    (void)e;
    (void)network_service_set_default_provision_transport(
        NETWORK_SERVICE_PROVISION_TRANSPORT_AP);
    wifi_management_controller_refresh();
}

static void wifi_management_status_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_screen == NULL || lv_screen_active() != s_screen)
    {
        return;
    }

    wifi_management_controller_refresh();
}

static lv_obj_t *wifi_management_controller_create_action_button(
    lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, 320, 52);
    lv_obj_set_style_radius(button, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x1f6feb),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(label);
    return button;
}

static lv_obj_t *wifi_management_controller_create_transport_button(
    lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, 94, 42);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_radius(button, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x2a2f3a),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x1f6feb),
                              LV_PART_MAIN | LV_STATE_CHECKED);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(label);
    return button;
}

static void wifi_management_controller_refresh_transport_buttons(
    network_service_provision_transport_t transport)
{
    lv_obj_t *buttons[] = {
        s_transport_auto_btn,
        s_transport_ble_btn,
        s_transport_ap_btn,
    };

    for (size_t index = 0; index < sizeof(buttons) / sizeof(buttons[0]); ++index)
    {
        if (buttons[index] == NULL)
        {
            continue;
        }

        if ((int)index == (int)transport)
        {
            lv_obj_add_state(buttons[index], LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_remove_state(buttons[index], LV_STATE_CHECKED);
        }
    }
}

static void wifi_management_controller_refresh(void)
{
    network_service_wifi_status_t status = {0};
    char detail[96] = {0};

    if (s_status_title == NULL || s_status_detail == NULL)
    {
        return;
    }

    (void)network_service_get_wifi_status(&status);
    wifi_management_controller_refresh_transport_buttons(
        status.default_transport);

    if (status.wifi_connected)
    {
        lv_label_set_text(s_status_title, "已连接");
        if (status.ip[0] != '\0')
        {
            snprintf(detail, sizeof(detail), "当前 IP：%s", status.ip);
        }
        else
        {
            snprintf(detail, sizeof(detail), "Wi-Fi 已连接");
        }
    }
    else if (status.provisioning_active)
    {
        lv_label_set_text(s_status_title, "正在重新配网");
        snprintf(detail, sizeof(detail), "默认配网方式：%s",
                 status.ap_active ? "AP" : "BLE");
    }
    else if (status.user_disconnect_latched)
    {
        lv_label_set_text(s_status_title, "已断开");
        snprintf(detail, sizeof(detail), "自动重连已暂停");
    }
    else if (status.has_credentials)
    {
        lv_label_set_text(s_status_title, "未连接");
        snprintf(detail, sizeof(detail),
                 "点击“进入配网”再次使用已保存凭据联网");
    }
    else
    {
        lv_label_set_text(s_status_title, "未连接");
        snprintf(detail, sizeof(detail), "点击“重连”进入新的配网流程");
    }

    lv_label_set_text(s_status_detail, detail);
}

static void wifi_management_controller_ensure_screen_created(void)
{
    if (s_screen != NULL)
    {
        return;
    }

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x0f172a),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, "Wi-Fi 管理");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title, &lv_font_montserratMedium_27,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(title, 24, 20);

    lv_obj_t *back_btn = lv_btn_create(s_screen);
    lv_obj_set_size(back_btn, 88, 40);
    lv_obj_set_pos(back_btn, 298, 20);
    lv_obj_set_style_radius(back_btn, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(back_btn, wifi_management_back_event_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "返回主页");
    lv_obj_center(back_label);

    s_status_title = lv_label_create(s_screen);
    lv_label_set_text(s_status_title, "未连接");
    lv_obj_set_pos(s_status_title, 24, 86);
    lv_obj_set_style_text_color(s_status_title, lv_color_hex(0xffffff),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_status_title, &lv_font_montserratMedium_27,
                               LV_PART_MAIN | LV_STATE_DEFAULT);

    s_status_detail = lv_label_create(s_screen);
    lv_obj_set_size(s_status_detail, 360, 42);
    lv_obj_set_pos(s_status_detail, 24, 124);
    lv_label_set_long_mode(s_status_detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_status_detail, lv_color_hex(0xcbd5e1),
                                LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *action_title = lv_label_create(s_screen);
    lv_label_set_text(action_title, "主操作区");
    lv_obj_set_pos(action_title, 24, 182);
    lv_obj_set_style_text_color(action_title, lv_color_hex(0x93c5fd),
                                LV_PART_MAIN | LV_STATE_DEFAULT);

    s_retry_saved_btn = wifi_management_controller_create_action_button(
        s_screen, "进入配网", 24, 214);
    lv_obj_add_event_cb(s_retry_saved_btn, wifi_management_retry_saved_event_cb,
                        LV_EVENT_CLICKED, NULL);

    s_disconnect_btn = wifi_management_controller_create_action_button(
        s_screen, "断开联网", 24, 276);
    lv_obj_add_event_cb(s_disconnect_btn, wifi_management_disconnect_event_cb,
                        LV_EVENT_CLICKED, NULL);

    s_reprovision_btn = wifi_management_controller_create_action_button(
        s_screen, "重连", 24, 338);
    lv_obj_add_event_cb(s_reprovision_btn, wifi_management_reprovision_event_cb,
                        LV_EVENT_CLICKED, NULL);

    lv_obj_t *settings_title = lv_label_create(s_screen);
    lv_label_set_text(settings_title, "默认配网方式");
    lv_obj_set_pos(settings_title, 24, 410);
    lv_obj_set_style_text_color(settings_title, lv_color_hex(0x93c5fd),
                                LV_PART_MAIN | LV_STATE_DEFAULT);

    s_transport_auto_btn = wifi_management_controller_create_transport_button(
        s_screen, "AUTO", 24, 442);
    s_transport_ble_btn = wifi_management_controller_create_transport_button(
        s_screen, "BLE", 132, 442);
    s_transport_ap_btn = wifi_management_controller_create_transport_button(
        s_screen, "AP", 240, 442);

    lv_obj_add_event_cb(s_transport_auto_btn,
                        wifi_management_transport_auto_event_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_transport_ble_btn,
                        wifi_management_transport_ble_event_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_transport_ap_btn, wifi_management_transport_ap_event_cb,
                        LV_EVENT_CLICKED, NULL);

    if (s_status_timer == NULL)
    {
        s_status_timer =
            lv_timer_create(wifi_management_status_timer_cb, kStatusRefreshMs,
                            NULL);
    }

    wifi_management_controller_refresh();
    ESP_LOGI(TAG, "Wi-Fi 管理页已创建");
}

void wifi_management_controller_init(lv_ui *ui)
{
    s_ui = ui;
}

void wifi_management_controller_open(void)
{
    wifi_management_controller_ensure_screen_created();
    if (s_screen == NULL)
    {
        return;
    }

    wifi_management_controller_refresh();
    lv_screen_load_anim(s_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}
