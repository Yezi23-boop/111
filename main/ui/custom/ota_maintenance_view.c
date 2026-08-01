#include "ui/custom/ota_maintenance_view.h"

#include "esp_log.h"
#include "gui_guider.h"
#include "lvgl.h"
#include "services/ota/ota_service.h"

static const char *TAG = "ota_view";
static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_progress_bar = NULL;
static lv_obj_t *s_progress_label = NULL;
static lv_obj_t *s_primary_label = NULL;

static void ota_view_primary_event(lv_event_t *event)
{
    (void)event;
    ota_service_snapshot_t snapshot = {0};
    if (ota_service_get_snapshot(&snapshot) != ESP_OK)
    {
        return;
    }

    if (snapshot.state == OTA_SERVICE_STATE_IDLE ||
        snapshot.state == OTA_SERVICE_STATE_FAILED ||
        snapshot.state == OTA_SERVICE_STATE_NO_UPDATE)
    {
        (void)ota_service_request_check();
    }
    else if (snapshot.state == OTA_SERVICE_STATE_READY)
    {
        (void)ota_service_request_download();
    }
    else if (snapshot.state == OTA_SERVICE_STATE_STAGED)
    {
        (void)ota_service_request_activate();
    }
}

static void ota_view_cancel_event(lv_event_t *event)
{
    (void)event;
    (void)ota_service_request_cancel();
}

esp_err_t ota_maintenance_view_init(void)
{
    if (s_screen != NULL)
    {
        return ESP_OK;
    }

    s_screen = lv_obj_create(NULL);
    if (s_screen == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_size(s_screen, 410, 502);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x10131A),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_obj_set_pos(title, 40, 42);
    lv_obj_set_size(title, 330, 40);
    lv_label_set_text(title, "系统维护");
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    s_status_label = lv_label_create(s_screen);
    lv_obj_set_pos(s_status_label, 40, 108);
    lv_obj_set_size(s_status_label, 330, 44);
    lv_label_set_text(s_status_label, "等待操作");
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xD7DCE8),
                                LV_PART_MAIN);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);

    s_progress_bar = lv_bar_create(s_screen);
    lv_obj_set_pos(s_progress_bar, 40, 220);
    lv_obj_set_size(s_progress_bar, 330, 18);
    lv_bar_set_range(s_progress_bar, 0, 100);
    lv_bar_set_value(s_progress_bar, 0, LV_ANIM_OFF);

    s_progress_label = lv_label_create(s_screen);
    lv_obj_set_pos(s_progress_label, 40, 250);
    lv_obj_set_size(s_progress_label, 330, 32);
    lv_label_set_text(s_progress_label, "0%");
    lv_obj_set_style_text_color(s_progress_label, lv_color_white(),
                                LV_PART_MAIN);
    lv_obj_set_style_text_align(s_progress_label, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);

    lv_obj_t *prepare = lv_button_create(s_screen);
    lv_obj_set_pos(prepare, 40, 320);
    lv_obj_set_size(prepare, 160, 54);
    lv_obj_add_event_cb(prepare, ota_view_primary_event, LV_EVENT_CLICKED,
                        NULL);
    s_primary_label = lv_label_create(prepare);
    lv_label_set_text(s_primary_label, "检查更新");
    lv_obj_center(s_primary_label);

    lv_obj_t *cancel = lv_button_create(s_screen);
    lv_obj_set_pos(cancel, 210, 320);
    lv_obj_set_size(cancel, 160, 54);
    lv_obj_add_event_cb(cancel, ota_view_cancel_event, LV_EVENT_CLICKED,
                        NULL);
    lv_obj_t *cancel_label = lv_label_create(cancel);
    lv_label_set_text(cancel_label, "取消");
    lv_obj_center(cancel_label);
    return ESP_OK;
}

esp_err_t ota_maintenance_view_open(void)
{
    if (s_screen == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    lv_screen_load(s_screen);
    ESP_LOGI(TAG, "maintenance page opened");
    return ESP_OK;
}

void ota_maintenance_view_poll(void)
{
    if (s_screen == NULL || s_status_label == NULL ||
        s_progress_bar == NULL || s_progress_label == NULL ||
        s_primary_label == NULL)
    {
        return;
    }

    ota_service_snapshot_t snapshot = {0};
    if (ota_service_get_snapshot(&snapshot) != ESP_OK)
    {
        return;
    }

    if (snapshot.update_available && snapshot.state == OTA_SERVICE_STATE_READY)
    {
        lv_label_set_text_fmt(s_status_label, "目标 %s",
                              snapshot.target_version);
    }
    else if (snapshot.state == OTA_SERVICE_STATE_NO_UPDATE)
    {
        lv_label_set_text(s_status_label, "没有可用更新");
    }
    else
    {
        lv_label_set_text(s_status_label,
                          ota_service_state_text(snapshot.state));
    }
    lv_bar_set_value(s_progress_bar, snapshot.progress_percent, LV_ANIM_OFF);
    lv_label_set_text_fmt(s_progress_label, "%u%%",
                          (unsigned int)snapshot.progress_percent);

    if (snapshot.state == OTA_SERVICE_STATE_READY && snapshot.update_available)
    {
        lv_label_set_text(s_primary_label, "下载更新");
    }
    else if (snapshot.state == OTA_SERVICE_STATE_STAGED)
    {
        lv_label_set_text(s_primary_label, "重启安装");
    }
    else
    {
        lv_label_set_text(s_primary_label, "检查更新");
    }
}
