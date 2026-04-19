#include "wifi_management_controller.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "network_manager.h"

static const char *TAG = "wifi_mgmt_ui";
static const uint32_t kStatusRefreshMs = 300U;

static lv_ui *s_ui = NULL;
static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_status_title = NULL;
static lv_obj_t *s_status_detail = NULL;
static lv_obj_t *s_retry_saved_btn = NULL;
static lv_obj_t *s_disconnect_btn = NULL;
static lv_obj_t *s_reprovision_btn = NULL;
static lv_obj_t *s_transport_ble_btn = NULL;
static lv_obj_t *s_transport_softap_btn = NULL;
static lv_timer_t *s_status_timer = NULL;

static void wifi_management_controller_refresh(void);
static void wifi_management_controller_ensure_screen_created(void);
static bool wifi_management_controller_get_status(
    network_manager_status_t *status);
static lv_obj_t *wifi_management_controller_create_action_button(
    lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y);
static lv_obj_t *wifi_management_controller_create_transport_button(
    lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y);

/**
 * @brief 处理 Wi-Fi 管理页返回主界面的点击事件。
 * @param[in] e LVGL 事件对象，当前实现未直接读取。
 */
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

/**
 * @brief 处理“Use Saved Wi-Fi”按钮点击。
 *
 * 该按钮语义是再次使用最近一次成功连接的 Wi-Fi 凭据发起连接，
 * 适用于开机后因为环境变化或暂时失败而未连上的场景。
 *
 * @param[in] e LVGL 事件对象，当前实现未直接读取。
 */
static void wifi_management_retry_saved_event_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "request latest saved Wi-Fi");
    (void)network_manager_use_latest_wifi();
    wifi_management_controller_refresh();
}

/**
 * @brief 处理“Disconnect”按钮点击。
 *
 * 该按钮会主动断开当前 Wi-Fi，并暂停自动重连，直到用户再次手动发起
 * “Use Saved Wi-Fi”或“Reprovision”。
 *
 * @param[in] e LVGL 事件对象，当前实现未直接读取。
 */
static void wifi_management_disconnect_event_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "request Wi-Fi disconnect");
    (void)network_manager_disconnect();
    wifi_management_controller_refresh();
}

/**
 * @brief 处理“Reprovision”按钮点击。
 *
 * 该按钮会停止当前 transport，并重新进入用户当前选定的 provisioning
 * transport，用于重新走配网流程。
 *
 * @param[in] e LVGL 事件对象，当前实现未直接读取。
 */
static void wifi_management_reprovision_event_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "request reprovision");
    (void)network_manager_reprovision();
    wifi_management_controller_refresh();
}

/**
 * @brief 处理 BLE transport 选择按钮点击。
 * @param[in] e LVGL 事件对象，当前实现未直接读取。
 */
static void wifi_management_transport_ble_event_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "set provisioning transport: BLE");
    (void)network_manager_set_default_transport(
        NETWORK_MANAGER_PROVISIONING_TRANSPORT_BLE);
    wifi_management_controller_refresh();
}

/**
 * @brief 处理 SoftAP transport 选择按钮点击。
 * @param[in] e LVGL 事件对象，当前实现未直接读取。
 */
static void wifi_management_transport_softap_event_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "set provisioning transport: SoftAP");
    (void)network_manager_set_default_transport(
        NETWORK_MANAGER_PROVISIONING_TRANSPORT_SOFTAP);
    wifi_management_controller_refresh();
}

/**
 * @brief Wi-Fi 管理页状态刷新定时器。
 *
 * 页面不在前台时不刷新，避免后台页面继续消耗无意义的绘制和状态读取。
 *
 * @param[in] timer LVGL 定时器句柄，当前实现未直接读取。
 */
static void wifi_management_status_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_screen == NULL || lv_screen_active() != s_screen)
    {
        return;
    }

    wifi_management_controller_refresh();
}

/**
 * @brief 读取当前网络状态快照。
 *
 * @param[out] status 输出状态结构。
 * @return true 表示读取成功；false 表示状态暂不可用。
 */
static bool wifi_management_controller_get_status(
    network_manager_status_t *status)
{
    if (status == NULL)
    {
        return false;
    }

    memset(status, 0, sizeof(*status));
    if (network_manager_get_status(status) != ESP_OK)
    {
        ESP_LOGW(TAG, "failed to read network manager status");
        return false;
    }

    return true;
}

/**
 * @brief 创建主操作区按钮。
 *
 * @param[in] parent 父对象。
 * @param[in] text 按钮文案。
 * @param[in] x 左上角 X 坐标。
 * @param[in] y 左上角 Y 坐标。
 * @return 创建好的按钮对象。
 */
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

/**
 * @brief 创建 provisioning transport 选择按钮。
 *
 * @param[in] parent 父对象。
 * @param[in] text 按钮文案。
 * @param[in] x 左上角 X 坐标。
 * @param[in] y 左上角 Y 坐标。
 * @return 创建好的按钮对象。
 */
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

/**
 * @brief 根据默认 provisioning transport 刷新底部设置按钮选中态。
 *
 * @param[in] transport 当前默认 provisioning transport。
 */
static void wifi_management_controller_refresh_transport_buttons(
    network_manager_provisioning_transport_t transport)
{
    lv_obj_t *buttons[] = {
        s_transport_ble_btn,
        s_transport_softap_btn,
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

/**
 * @brief 刷新 Wi-Fi 管理页状态文本与设置态。
 *
 * 页面顶部状态区只表达用户可理解的联网语义，不直接暴露底层 driver
 * 细节。这里统一从 `network_manager` 读取状态，避免 UI 直接依赖旧的
 * `network_service` 兼容层。
 */
static void wifi_management_controller_refresh(void)
{
    network_manager_status_t status = {0};
    char detail[96] = {0};

    if (s_status_title == NULL || s_status_detail == NULL)
    {
        return;
    }

    if (!wifi_management_controller_get_status(&status))
    {
        lv_label_set_text(s_status_title, "Error");
        lv_label_set_text(s_status_detail, "Network manager status unavailable");
        return;
    }

    wifi_management_controller_refresh_transport_buttons(status.default_transport);

    if (status.wifi_connected)
    {
        lv_label_set_text(s_status_title, "Connected");
        if (status.ip[0] != '\0')
        {
            snprintf(detail, sizeof(detail), "IP: %s", status.ip);
        }
        else
        {
            snprintf(detail, sizeof(detail), "Wi-Fi connected");
        }
    }
    else if (status.state == NETWORK_MANAGER_STATE_CONNECTING_LATEST)
    {
        lv_label_set_text(s_status_title, "Connecting");
        snprintf(detail, sizeof(detail), "Trying the latest saved Wi-Fi");
    }
    else if (status.state == NETWORK_MANAGER_STATE_PROVISIONING_BLE)
    {
        lv_label_set_text(s_status_title, "Provisioning");
        snprintf(detail, sizeof(detail), "Current transport: BLE");
    }
    else if (status.state == NETWORK_MANAGER_STATE_PROVISIONING_SOFTAP)
    {
        lv_label_set_text(s_status_title, "Provisioning");
        snprintf(detail, sizeof(detail), "Current transport: SoftAP");
    }
    else if (status.state == NETWORK_MANAGER_STATE_DISCONNECTED_BY_USER)
    {
        lv_label_set_text(s_status_title, "Disconnected");
        snprintf(detail, sizeof(detail), "Auto reconnect is paused");
    }
    else if (status.state == NETWORK_MANAGER_STATE_ERROR)
    {
        lv_label_set_text(s_status_title, "Error");
        snprintf(detail, sizeof(detail),
                 "Tap 'Use Saved Wi-Fi' or 'Reprovision'");
    }
    else
    {
        lv_label_set_text(s_status_title, "Offline");
        snprintf(detail, sizeof(detail),
                 "Tap 'Use Saved Wi-Fi' or 'Reprovision'");
    }

    lv_label_set_text(s_status_detail, detail);
}

/**
 * @brief 按需创建 Wi-Fi 管理页。
 *
 * 当前继续沿用 hand-written LVGL 页面，不要求用户在 GUI Guider 里新增
 * 页面结构，只在现有工程内生成一张全新的管理页。
 */
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
    lv_label_set_text(title, "Wi-Fi Manager");
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
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);

    s_status_title = lv_label_create(s_screen);
    lv_label_set_text(s_status_title, "Offline");
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
    lv_label_set_text(action_title, "Actions");
    lv_obj_set_pos(action_title, 24, 182);
    lv_obj_set_style_text_color(action_title, lv_color_hex(0x93c5fd),
                                LV_PART_MAIN | LV_STATE_DEFAULT);

    s_retry_saved_btn = wifi_management_controller_create_action_button(
        s_screen, "Use Saved Wi-Fi", 24, 214);
    lv_obj_add_event_cb(s_retry_saved_btn, wifi_management_retry_saved_event_cb,
                        LV_EVENT_CLICKED, NULL);

    s_disconnect_btn = wifi_management_controller_create_action_button(
        s_screen, "Disconnect", 24, 276);
    lv_obj_add_event_cb(s_disconnect_btn, wifi_management_disconnect_event_cb,
                        LV_EVENT_CLICKED, NULL);

    s_reprovision_btn = wifi_management_controller_create_action_button(
        s_screen, "Reprovision", 24, 338);
    lv_obj_add_event_cb(s_reprovision_btn, wifi_management_reprovision_event_cb,
                        LV_EVENT_CLICKED, NULL);

    lv_obj_t *settings_title = lv_label_create(s_screen);
    lv_label_set_text(settings_title, "Provisioning Transport");
    lv_obj_set_pos(settings_title, 24, 410);
    lv_obj_set_style_text_color(settings_title, lv_color_hex(0x93c5fd),
                                LV_PART_MAIN | LV_STATE_DEFAULT);

    s_transport_ble_btn = wifi_management_controller_create_transport_button(
        s_screen, "BLE", 24, 442);
    s_transport_softap_btn = wifi_management_controller_create_transport_button(
        s_screen, "SoftAP", 132, 442);

    lv_obj_add_event_cb(s_transport_ble_btn,
                        wifi_management_transport_ble_event_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_transport_softap_btn,
                        wifi_management_transport_softap_event_cb,
                        LV_EVENT_CLICKED, NULL);

    if (s_status_timer == NULL)
    {
        s_status_timer =
            lv_timer_create(wifi_management_status_timer_cb, kStatusRefreshMs,
                            NULL);
    }

    wifi_management_controller_refresh();
    ESP_LOGI(TAG, "Wi-Fi management screen created");
}

/**
 * @brief 初始化 Wi-Fi 管理页控制器。
 * @param[in] ui 当前 UI 句柄。
 */
void wifi_management_controller_init(lv_ui *ui)
{
    s_ui = ui;
}

/**
 * @brief 打开 Wi-Fi 管理页。
 *
 * 首次打开时会先创建页面，之后复用同一张页面并在进入前刷新状态。
 */
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
