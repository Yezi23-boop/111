#include "wifi_management_controller.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "network_manager.h"

LV_FONT_DECLARE(lv_font_montserrat_lxgw_lv_font_wifi_subset_16_16_4);
LV_FONT_DECLARE(lv_font_montserrat_lxgw_lv_font_wifi_subset_24_24_4);

static const char *TAG = "wifi_mgmt_ui";
static const uint32_t kStatusRefreshMs = 300U;
static const lv_coord_t kWifiBackButtonX = 286;
static const lv_coord_t kWifiBackButtonY = 26;
static const lv_coord_t kWifiBackButtonWidth = 96;
static const lv_coord_t kWifiBackButtonHeight = 48;

static lv_ui *s_ui = NULL;
static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_status_title = NULL;
static lv_obj_t *s_status_detail = NULL;
static lv_obj_t *s_retry_saved_btn = NULL;
static lv_obj_t *s_disconnect_btn = NULL;
static lv_obj_t *s_ble_provision_btn = NULL;
static lv_obj_t *s_softap_provision_btn = NULL;
static lv_timer_t *s_status_timer = NULL;

static void wifi_management_controller_refresh(void);
static void wifi_management_controller_ensure_screen_created(void);
static void wifi_management_controller_reset_screen_refs(void);
static bool wifi_management_controller_get_status(
    network_manager_status_t *status);
static lv_obj_t *wifi_management_controller_create_action_button(
    lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y);
static void wifi_management_controller_set_locked(lv_obj_t *obj, bool locked);
static void wifi_management_controller_refresh_action_lock_state(
    const network_manager_status_t *status);
static bool wifi_management_controller_is_screen_alive(void);
static void wifi_management_controller_show_inline_message(
    const char *title, const char *detail);

/**
 * @brief 在 Wi-Fi 管理页对象被删除时清空缓存指针。
 *
 * 当前页面通过静态全局指针缓存 `screen / label / button`，方便二次打开时复用。
 * 但返回主界面时 `lv_screen_load_anim(..., true)` 会删除旧 screen；如果这里不在
 * `LV_EVENT_DELETE` 回调里同步清空缓存，下一次再次打开页面时就会把已经释放的
 * `lv_obj_t *` 当成活对象继续调用 `lv_obj_add_state()`，最终在 `lv_style_get_prop()`
 * 里因悬空对象触发 `LoadProhibited`。
 *
 * @param[in] e LVGL 事件对象，当前只用于表明删除事件已到达。
 */
static void wifi_management_screen_delete_event_cb(lv_event_t *e)
{
    (void)e;
    wifi_management_controller_reset_screen_refs();
}

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
 * @brief 处理“BLE Provision”按钮点击。
 *
 * 该按钮是小程序 BLE 配网的唯一显式入口。主界面蓝牙按钮只负责
 * BLE 总开关，不会自动启动官方 provisioning 广播。
 *
 * @param[in] e LVGL 事件对象，当前实现未直接读取。
 */
static void wifi_management_ble_provision_event_cb(lv_event_t *e)
{
    (void)e;
    esp_err_t ret = ESP_OK;

    ESP_LOGI(TAG, "request BLE provisioning from Wi-Fi page");
    ret = network_manager_start_ble_provisioning();
    if (ret == ESP_ERR_INVALID_STATE)
    {
        wifi_management_controller_show_inline_message(
            "Bluetooth Off", "Turn on Bluetooth from the main switch first");
        return;
    }
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "start BLE provisioning failed: %s",
                 esp_err_to_name(ret));
        wifi_management_controller_show_inline_message(
            "BLE Failed", "Could not start BLE provisioning");
        return;
    }

    wifi_management_controller_refresh();
}

/**
 * @brief 处理“AP Web Fallback”按钮点击。
 *
 * 该按钮保留原有 AP 网页配网兜底能力，和 BLE 小程序配网互为备选入口。
 *
 * @param[in] e LVGL 事件对象，当前实现未直接读取。
 */
static void wifi_management_softap_provision_event_cb(lv_event_t *e)
{
    (void)e;
    esp_err_t ret = ESP_OK;

    ESP_LOGI(TAG, "request SoftAP provisioning from Wi-Fi page");
    ret = network_manager_start_softap_provisioning();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "start SoftAP provisioning failed: %s",
                 esp_err_to_name(ret));
        wifi_management_controller_show_inline_message(
            "AP Failed", "Could not start AP web fallback");
        return;
    }

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

    if (!wifi_management_controller_is_screen_alive() ||
        lv_screen_active() != s_screen)
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
    lv_obj_set_size(button, 340, 56);
    lv_obj_set_style_radius(button, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    lv_obj_set_style_bg_color(button, lv_color_hex(0x1c1c1e), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x2c2c2e), LV_PART_MAIN | LV_STATE_PRESSED);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_lxgw_lv_font_wifi_subset_16_16_4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 20, 0);
    return button;
}

static lv_obj_t *wifi_management_controller_create_bento_button(
    lv_obj_t *parent, const char *title, const char *subtitle, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, 166, 80);
    lv_obj_set_style_radius(button, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    lv_obj_set_style_bg_color(button, lv_color_hex(0x1c1c1e), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x2c2c2e), LV_PART_MAIN | LV_STATE_PRESSED);

    lv_obj_t *title_lbl = lv_label_create(button);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_lxgw_lv_font_wifi_subset_16_16_4, LV_PART_MAIN);
    lv_obj_align(title_lbl, LV_ALIGN_TOP_LEFT, 8, 12);

    lv_obj_t *sub_lbl = lv_label_create(button);
    lv_label_set_text(sub_lbl, subtitle);
    lv_obj_set_style_text_color(sub_lbl, lv_color_hex(0x8E8E93), LV_PART_MAIN);
    lv_obj_set_style_text_font(sub_lbl, &lv_font_montserrat_lxgw_lv_font_wifi_subset_16_16_4, LV_PART_MAIN);
    lv_obj_align(sub_lbl, LV_ALIGN_BOTTOM_LEFT, 8, -12);

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
/**
 * @brief 判断当前缓存的 Wi-Fi 管理页 screen 是否仍然有效。
 *
 * @return true 表示 `s_screen` 仍是 LVGL 有效对象；false 表示 screen 为空或已被删除。
 */
static bool wifi_management_controller_is_screen_alive(void)
{
    return s_screen != NULL && lv_obj_is_valid(s_screen);
}

/**
 * @brief 清空当前页面相关的所有对象缓存。
 *
 * 这里不会删除对象本身，而是在 LVGL 已经进入删除流程后把 hand-written 控制层的静态
 * 指针全部置空，确保下一次打开页面时重新创建一套全新的 screen 和子控件。
 *
 * @return 无返回值。
 */
static void wifi_management_controller_reset_screen_refs(void)
{
    s_screen = NULL;
    s_status_title = NULL;
    s_status_detail = NULL;
    s_retry_saved_btn = NULL;
    s_disconnect_btn = NULL;
    s_ble_provision_btn = NULL;
    s_softap_provision_btn = NULL;
}

/**
 * @brief 按是否锁定刷新单个按钮的交互态。
 *
 * 当前 Wi-Fi 管理页使用该辅助函数统一收口“配网进行中”的禁用逻辑，避免多个按钮
 * 各自散落地直接操作 `LV_STATE_DISABLED`。
 *
 * @param[in] obj 目标按钮对象，可为 `NULL`。
 * @param[in] locked true 表示禁用交互；false 表示恢复可点击。
 */
static void wifi_management_controller_set_locked(lv_obj_t *obj, bool locked)
{
    if (obj == NULL || !lv_obj_is_valid(obj))
    {
        return;
    }

    if (locked)
    {
        lv_obj_add_state(obj, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_remove_state(obj, LV_STATE_DISABLED);
    }
}

/**
 * @brief 根据当前网络快照刷新页面操作锁定态。
 *
 * BLE / SoftAP provisioning 期间，重复点击两个配网入口会把当前会话直接
 * stop -> start 掉，导致用户自己把小程序连接或 AP 门户抖断。因此这里
 * 在任一 provisioning 会话进行中锁住两个入口。
 *
 * 其余操作暂保持原样，避免扩大本轮改动范围。
 *
 * @param[in] status 当前 `network_manager` 快照。
 */
static void wifi_management_controller_refresh_action_lock_state(
    const network_manager_status_t *status)
{
    if (status == NULL)
    {
        return;
    }

    const bool provisioning_locked =
        status->ble_active ||
        (status->state == NETWORK_MANAGER_STATE_PROVISIONING_BLE) ||
        (status->state == NETWORK_MANAGER_STATE_PROVISIONING_SOFTAP);

    wifi_management_controller_set_locked(s_ble_provision_btn,
                                          provisioning_locked);
    wifi_management_controller_set_locked(s_softap_provision_btn,
                                          provisioning_locked);
}

/**
 * @brief 在状态区显示一次内联提示。
 *
 * Wi-Fi 管理页没有额外 toast 层，这里复用标题和详情区域表达点击失败原因，
 * 避免用户点“BLE 配网”后无反馈。
 *
 * @param[in] title 提示标题。
 * @param[in] detail 提示详情。
 */
static void wifi_management_controller_show_inline_message(
    const char *title, const char *detail)
{
    if (s_status_title == NULL || s_status_detail == NULL ||
        !lv_obj_is_valid(s_status_title) || !lv_obj_is_valid(s_status_detail))
    {
        return;
    }

    lv_label_set_text(s_status_title, title != NULL ? title : "错误");
    lv_label_set_text(s_status_detail, detail != NULL ? detail : "");
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

    if (!wifi_management_controller_is_screen_alive() ||
        s_status_title == NULL || s_status_detail == NULL ||
        !lv_obj_is_valid(s_status_title) || !lv_obj_is_valid(s_status_detail))
    {
        return;
    }

    if (!wifi_management_controller_get_status(&status))
    {
        lv_label_set_text(s_status_title, "错误");
        lv_label_set_text(s_status_detail, "当前通道错误");
        return;
    }

    wifi_management_controller_refresh_action_lock_state(&status);

    if (status.ble_active && status.wifi_connected)
    {
        lv_label_set_text(s_status_title, "已连接 + 配网中");
        lv_obj_set_style_text_color(s_status_title, lv_color_hex(0x32D74B), LV_PART_MAIN);
        if (status.ip[0] != '\0')
        {
            snprintf(detail, sizeof(detail), "IP: %s, 蓝牙活跃中", status.ip);
        }
        else
        {
            snprintf(detail, sizeof(detail), "蓝牙活跃中");
        }
    }
    else if (status.ble_active)
    {
        lv_label_set_text(s_status_title, "蓝牙配网");
        lv_obj_set_style_text_color(s_status_title, lv_color_hex(0x0A84FF), LV_PART_MAIN);
        snprintf(detail, sizeof(detail), "请打开微信小程序");
    }
    else if (status.wifi_connected)
    {
        lv_label_set_text(s_status_title, "已连接");
        lv_obj_set_style_text_color(s_status_title, lv_color_hex(0x32D74B), LV_PART_MAIN);
        if (status.ip[0] != '\0')
        {
            snprintf(detail, sizeof(detail), "IP: %s", status.ip);
        }
        else
        {
            snprintf(detail, sizeof(detail), "Wi-Fi 已连接");
        }
    }
    else if (status.state == NETWORK_MANAGER_STATE_CONNECTING_LATEST)
    {
        lv_label_set_text(s_status_title, "连接中");
        lv_obj_set_style_text_color(s_status_title, lv_color_hex(0x0A84FF), LV_PART_MAIN);
        snprintf(detail, sizeof(detail), "正在尝试保存的 Wi-Fi");
    }
    else if (status.state == NETWORK_MANAGER_STATE_PROVISIONING_BLE ||
             status.state == NETWORK_MANAGER_STATE_PROVISIONING_SOFTAP)
    {
        lv_label_set_text(s_status_title, "配网中");
        lv_obj_set_style_text_color(s_status_title, lv_color_hex(0x0A84FF), LV_PART_MAIN);
        snprintf(detail, sizeof(detail), "当前通道: %s", 
                 status.state == NETWORK_MANAGER_STATE_PROVISIONING_BLE ? "蓝牙" : "AP");
    }
    else if (status.state == NETWORK_MANAGER_STATE_DISCONNECTED_BY_USER)
    {
        lv_label_set_text(s_status_title, "未连接");
        lv_obj_set_style_text_color(s_status_title, lv_color_hex(0x8E8E93), LV_PART_MAIN);
        snprintf(detail, sizeof(detail), "已暂停自动重连");
    }
    else if (status.state == NETWORK_MANAGER_STATE_ERROR)
    {
        lv_label_set_text(s_status_title, "连接失败");
        lv_obj_set_style_text_color(s_status_title, lv_color_hex(0xFF453A), LV_PART_MAIN);
        snprintf(detail, sizeof(detail), "请重新配网");
    }
    else
    {
        lv_label_set_text(s_status_title, "未连接");
        lv_obj_set_style_text_color(s_status_title, lv_color_hex(0x8E8E93), LV_PART_MAIN);
        snprintf(detail, sizeof(detail), "请在下方添加网络");
    }

    lv_label_set_text(s_status_detail, detail);

    // Dynamic visibility based on connection state
    if (s_retry_saved_btn != NULL && s_disconnect_btn != NULL)
    {
        if (status.wifi_connected || status.state == NETWORK_MANAGER_STATE_CONNECTING_LATEST)
        {
            lv_obj_add_flag(s_retry_saved_btn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_disconnect_btn, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_clear_flag(s_retry_saved_btn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_disconnect_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/**
 * @brief 按需创建 Wi-Fi 管理页。
 *
 * 当前继续沿用 hand-written LVGL 页面，不要求用户在 GUI Guider 里新增
 * 页面结构，只在现有工程内生成一张全新的管理页。
 */
static void wifi_management_controller_ensure_screen_created(void)
{
    if (wifi_management_controller_is_screen_alive())
    {
        return;
    }

    wifi_management_controller_reset_screen_refs();

    s_screen = lv_obj_create(NULL);
    // True OLED Black background
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(s_screen, wifi_management_screen_delete_event_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, "Wi-Fi");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(title, 24, 20);

    lv_obj_t *back_btn = lv_btn_create(s_screen);
    lv_obj_set_size(back_btn, 80, 48);
    lv_obj_set_pos(back_btn, 300, 10);
    lv_obj_set_style_radius(back_btn, 24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x1c1c1e), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(back_btn, wifi_management_back_event_cb, LV_EVENT_CLICKED, NULL);
    
    s_status_title = lv_label_create(s_screen);
    lv_label_set_text(s_status_title, "未连接");
    lv_obj_set_style_text_color(s_status_title, lv_color_hex(0x8E8E93), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_status_title, &lv_font_montserrat_lxgw_lv_font_wifi_subset_24_24_4, LV_PART_MAIN);
    lv_obj_set_pos(s_status_title, 24, 60);

    s_status_detail = lv_label_create(s_screen);
    lv_obj_set_width(s_status_detail, 320);
    lv_label_set_long_mode(s_status_detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_status_detail, lv_color_hex(0x8E8E93), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_status_detail, &lv_font_montserrat_lxgw_lv_font_wifi_subset_16_16_4, LV_PART_MAIN);
    lv_obj_set_pos(s_status_detail, 24, 88);

    s_ble_provision_btn = wifi_management_controller_create_bento_button(s_screen, "小程序配网", "蓝牙通道", 24, 282);
    lv_obj_add_event_cb(s_ble_provision_btn, wifi_management_ble_provision_event_cb, LV_EVENT_CLICKED, NULL);

    s_softap_provision_btn = wifi_management_controller_create_bento_button(s_screen, "网页配网", "AP 热点", 198, 282);
    lv_obj_add_event_cb(s_softap_provision_btn, wifi_management_softap_provision_event_cb, LV_EVENT_CLICKED, NULL);

    s_retry_saved_btn = wifi_management_controller_create_action_button(s_screen, "重试已保存网络", 24, 370);
    lv_obj_add_event_cb(s_retry_saved_btn, wifi_management_retry_saved_event_cb, LV_EVENT_CLICKED, NULL);

    s_disconnect_btn = wifi_management_controller_create_action_button(s_screen, "断开连接", 24, 434);
    lv_obj_add_event_cb(s_disconnect_btn, wifi_management_disconnect_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *disconnect_label = lv_obj_get_child(s_disconnect_btn, 0);
    if(disconnect_label != NULL) {
        lv_obj_set_style_text_color(disconnect_label, lv_color_hex(0xFF453A), LV_PART_MAIN);
    }

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
