#include "main_dropdown_controller.h"

#include "esp_log.h"
#include "services/network_service.h"
#include "wifi_management_controller.h"

static const char *TAG = "main_dropdown";
static const uint32_t kStatusSyncPeriodMs = 250U;
static const uint32_t kToastDurationMs = 1800U;

static lv_ui *s_ui = NULL;
static lv_timer_t *s_status_sync_timer = NULL;
static lv_timer_t *s_toast_timer = NULL;
static lv_obj_t *s_toast_label = NULL;
static bool s_last_wifi_checked = false;
static bool s_last_wifi_checked_valid = false;
static bool s_last_bluetooth_checked = false;
static bool s_last_bluetooth_checked_valid = false;

static lv_obj_t *main_dropdown_controller_get_wifi_button(void);
static lv_obj_t *main_dropdown_controller_get_bluetooth_button(void);
static bool main_dropdown_controller_is_main_screen_active(void);
static void main_dropdown_controller_sync_wifi_button(void);
static void main_dropdown_controller_sync_bluetooth_button(void);
static void main_dropdown_controller_status_sync_timer_cb(lv_timer_t *timer);
static void main_dropdown_controller_toast_timer_cb(lv_timer_t *timer);
static void main_dropdown_controller_hide_toast(void);
static void main_dropdown_controller_show_toast(const char *text);

static lv_obj_t *main_dropdown_controller_get_wifi_button(void)
{
    if (s_ui == NULL)
    {
        return NULL;
    }

    return s_ui->screen_main_Wifi;
}

static lv_obj_t *main_dropdown_controller_get_bluetooth_button(void)
{
    if (s_ui == NULL)
    {
        return NULL;
    }

    return s_ui->screen_main_Bluetooth;
}

static bool main_dropdown_controller_is_main_screen_active(void)
{
    return s_ui != NULL && s_ui->screen_main != NULL &&
           lv_screen_active() == s_ui->screen_main;
}

static void main_dropdown_controller_sync_wifi_button(void)
{
    lv_obj_t *button = main_dropdown_controller_get_wifi_button();
    const bool connected = network_service_is_wifi_connected();

    if (button == NULL)
    {
        return;
    }

    if (!s_last_wifi_checked_valid || s_last_wifi_checked != connected)
    {
        ESP_LOGI(TAG, "sync WiFi button: checked=%d connected=%d",
                 connected ? 1 : 0, connected ? 1 : 0);
        s_last_wifi_checked = connected;
        s_last_wifi_checked_valid = true;
    }

    if (connected)
    {
        lv_obj_add_state(button, LV_STATE_CHECKED);
        return;
    }

    lv_obj_remove_state(button, LV_STATE_CHECKED);
}

static void main_dropdown_controller_sync_bluetooth_button(void)
{
    lv_obj_t *button = main_dropdown_controller_get_bluetooth_button();
    const bool ble_active = network_service_is_ble_active();
    const bool ble_enabled = network_service_is_ble_enabled();
    const bool checked = ble_active;

    if (button == NULL)
    {
        return;
    }

    if (!s_last_bluetooth_checked_valid || s_last_bluetooth_checked != checked)
    {
        ESP_LOGI(TAG, "sync Bluetooth button: checked=%d ble_active=%d ble_enabled=%d",
                 checked ? 1 : 0, ble_active ? 1 : 0, ble_enabled ? 1 : 0);
        s_last_bluetooth_checked = checked;
        s_last_bluetooth_checked_valid = true;
    }

    if (checked)
    {
        lv_obj_add_state(button, LV_STATE_CHECKED);
        return;
    }

    lv_obj_remove_state(button, LV_STATE_CHECKED);
}

static void main_dropdown_controller_status_sync_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (!main_dropdown_controller_is_main_screen_active())
    {
        main_dropdown_controller_hide_toast();
        return;
    }

    main_dropdown_controller_sync_wifi_button();
    main_dropdown_controller_sync_bluetooth_button();
}

static void main_dropdown_controller_toast_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    main_dropdown_controller_hide_toast();
}

static void main_dropdown_controller_hide_toast(void)
{
    if (s_toast_label != NULL)
    {
        lv_obj_add_flag(s_toast_label, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_toast_timer != NULL)
    {
        lv_timer_pause(s_toast_timer);
    }
}

static void main_dropdown_controller_show_toast(const char *text)
{
    if (text == NULL || text[0] == '\0')
    {
        return;
    }

    ESP_LOGI(TAG, "show BLE toast: %s", text);

    if (s_toast_label == NULL)
    {
        s_toast_label = lv_label_create(lv_layer_top());
        lv_obj_set_width(s_toast_label, 280);
        lv_label_set_long_mode(s_toast_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_radius(s_toast_label, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(s_toast_label, LV_OPA_90, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(s_toast_label, lv_color_hex(0x20242b),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(s_toast_label, lv_color_hex(0xffffff),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(s_toast_label, LV_TEXT_ALIGN_CENTER,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_top(s_toast_label, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_bottom(s_toast_label, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_left(s_toast_label, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_right(s_toast_label, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_flag(s_toast_label, LV_OBJ_FLAG_HIDDEN);
    }

    lv_label_set_text(s_toast_label, text);
    lv_obj_remove_flag(s_toast_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_toast_label, LV_ALIGN_BOTTOM_MID, 0, -56);
    lv_obj_move_foreground(s_toast_label);

    if (s_toast_timer == NULL)
    {
        s_toast_timer = lv_timer_create(main_dropdown_controller_toast_timer_cb,
                                        kToastDurationMs, NULL);
        if (s_toast_timer != NULL)
        {
            lv_timer_set_repeat_count(s_toast_timer, 1);
            lv_timer_set_auto_delete(s_toast_timer, false);
        }
    }

    if (s_toast_timer != NULL)
    {
        lv_timer_set_period(s_toast_timer, kToastDurationMs);
        lv_timer_set_repeat_count(s_toast_timer, 1);
        lv_timer_resume(s_toast_timer);
        lv_timer_reset(s_toast_timer);
    }
}

void main_dropdown_controller_bind(lv_ui *ui)
{
    if (ui == NULL)
    {
        return;
    }

    s_ui = ui;
    wifi_management_controller_init(ui);
    ESP_LOGI(TAG, "bind BLE dropdown controller");
    ESP_LOGI(TAG, "bind main dropdown controller");
    if (s_status_sync_timer == NULL)
    {
        s_status_sync_timer = lv_timer_create(
            main_dropdown_controller_status_sync_timer_cb, kStatusSyncPeriodMs,
            NULL);
    }

    if (s_ui->screen_main_Bluetooth != NULL)
    {
        lv_obj_add_flag(s_ui->screen_main_Bluetooth, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui->screen_main_Bluetooth_label != NULL)
    {
        lv_obj_add_flag(s_ui->screen_main_Bluetooth_label, LV_OBJ_FLAG_HIDDEN);
    }

    main_dropdown_controller_sync_wifi_button();
    main_dropdown_controller_sync_bluetooth_button();
}

void main_dropdown_controller_handle_wifi_click(void)
{
    ESP_LOGI(TAG, "WiFi button clicked");
    wifi_management_controller_open();
}

void main_dropdown_controller_handle_bluetooth_click(void)
{
    const bool ble_active = network_service_is_ble_active();
    const bool ble_enabled = network_service_is_ble_enabled();
    esp_err_t ret = ESP_OK;

    ESP_LOGI(TAG, "Bluetooth button clicked: ble_active=%d ble_enabled=%d",
             ble_active ? 1 : 0, ble_enabled ? 1 : 0);

    if (ble_active)
    {
        ret = network_service_set_ble_enabled(false);
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "disable BLE provisioning failed: %s",
                     esp_err_to_name(ret));
        }
        main_dropdown_controller_sync_bluetooth_button();
        return;
    }

    ret = network_service_set_ble_enabled(true);
    if (ret == ESP_ERR_INVALID_STATE)
    {
        main_dropdown_controller_show_toast("当前仅无凭据时允许 BLE 配网");
    }
    else if (ret != ESP_OK)
    {
        main_dropdown_controller_show_toast("BLE 配网启动失败");
        ESP_LOGW(TAG, "enable BLE provisioning failed: %s",
                 esp_err_to_name(ret));
    }

    main_dropdown_controller_sync_bluetooth_button();
}
