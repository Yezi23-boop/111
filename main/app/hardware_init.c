#include "hardware_init.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "wifi_provision.h"
#include "features/audio/audio_app.h"
#include "sd_manager.h"
#include "audio_codec.h"
#include "board_power.h"
#include "i2c_manager.h"
#include "button_gpio.h"
#include "driver/gpio.h"
#include "iot_button.h"
#include "services/network_service.h"
static const char *TAG = "HARDWARE_INIT";
/* @brief BOOT按钮的GPIO引脚号
 *
 * ESP32-S3开发板上的BOOT按钮默认连接到GPIO10
 * 按下时GPIO10变为低电平，松开时为高电平（需要上拉电阻）
 */
#define BUTTON_GPIO_NUM GPIO_NUM_10

static void board_power_log_boot_snapshot(const board_power_state_t *state)
{
    if (state == NULL)
    {
        ESP_LOGW(TAG, "Board power boot snapshot unavailable");
        return;
    }

    if (state->battery_data_valid)
    {
        ESP_LOGI(TAG,
                 "Board power boot snapshot: available=%d stale=%d ext=%d bat=%d chg=%d dchg=%d vbat=%umV vsys=%umV soc=%u%%",
                 state->available, state->snapshot_stale,
                 state->external_power_present, state->battery_present,
                 state->charging, state->discharging, state->battery_mv,
                 state->system_mv, state->battery_percent);
        return;
    }

    ESP_LOGI(TAG,
             "Board power boot snapshot: available=%d stale=%d ext=%d bat=%d chg=%d dchg=%d vbat=%umV vsys=%umV soc=unknown",
             state->available, state->snapshot_stale,
             state->external_power_present, state->battery_present,
             state->charging, state->discharging, state->battery_mv,
             state->system_mv);
}
static void button_single_click_cb(void *arg, void *data)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "按键单击！启动BLE配网模式...");
    ESP_LOGI(TAG, "========================================");

    network_service_request_ble();
}

static void button_long_press_start_cb(void *arg, void *data)
{
    ESP_LOGI(TAG, "BUTTON_LONG_PRESS_START");
}

static void button_triple_click_cb(void *arg, void *usr_data)
{
    char *msg = (char *)usr_data;
    esp_err_t ret = ESP_OK;

    ESP_LOGI(TAG, "BUTTON_TRIPLE_CLICK: %s", msg);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "按键三连击！启动AP配网模式...");
    ESP_LOGI(TAG, "========================================");

    ret = wifi_provision_start_apcfg();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "启动AP配网失败: %s", esp_err_to_name(ret));
    }
}

static void button_init(void)
{
    // long_press_time/short_press_time 由 button 组件以毫秒解释。
    button_config_t gpio_btn_cfg = {
        .long_press_time = 1500,
        .short_press_time = 180,
    };

    button_gpio_config_t gpio_cfg = {
        .gpio_num = BUTTON_GPIO_NUM, // 物理按键 GPIO
        .active_level = 1,           // 高电平触发按键按下
        .enable_power_save = true,   // 允许按钮驱动低功耗策略
        .disable_pull = false,       // 保留内部上下拉配置能力
    };

    button_handle_t gpio_btn_handle = NULL;
    esp_err_t err = iot_button_new_gpio_device(&gpio_btn_cfg, &gpio_cfg, &gpio_btn_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Button create failed");
    }

    // 定义用户数据（注意：确保该数据在回调执行时依然有效，通常使用全局变量或静态变量）
    static char *user_msg = "Hello from Triple Click!";

    // 多击参数：三连击用于显式切到 AP 配网。
    button_event_args_t args = {
        .multiple_clicks.clicks = 3,
    };

    iot_button_register_cb(gpio_btn_handle, BUTTON_SINGLE_CLICK, NULL, button_single_click_cb, NULL);
    iot_button_register_cb(gpio_btn_handle, BUTTON_LONG_PRESS_START, NULL, button_long_press_start_cb, NULL);

    // 注册三连击事件，使用所有参数
    iot_button_register_cb(gpio_btn_handle, BUTTON_MULTIPLE_CLICK, &args, button_triple_click_cb, user_msg);
}
/**
 * @brief WiFi事件回调函数
 * @param ev WiFi事件类型
 */

static void wifi_provision_cb(wifi_provision_state_t state)
{
    if (state == WIFI_PROVISION_STATE_CONNECTED)
    {
        ESP_LOGI(TAG, "WiFi Connected Event Received");
    }
    else if (state == WIFI_PROVISION_STATE_DISCONNECTED)
    {
        ESP_LOGW(TAG, "WiFi Disconnected");
    }
}
/**
 * @brief NVS闪存初始化
 * @return esp_err_t 初始化结果
 */
static esp_err_t hardware_nvs_init(void)
{
    // 首次初始化失败若由分页或版本差异导致，先擦除后重建 NVS。
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS Flash init failed, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

/**
 * @brief 硬件层统一初始化
 * @details 初始化NVS、WiFi组件、SPIFFS、SD卡、I2C总线和音频编解码器，但不阻塞等待WiFi连接成功
 * @return esp_err_t ESP_OK: 基础硬件初始化成功; 其他: 初始化失败
 */
esp_err_t hardware_init(void)
{
    esp_err_t ret;

    // 1. NVS初始化
    ESP_LOGI(TAG, "Initializing NVS...");
    ret = hardware_nvs_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. 初始化音频应用资源（录音/播放与文件路径准备）。
    ESP_LOGI(TAG, "Initializing Audio SPIFFS...");
    ret = audio_app_init(); // 假设这里包含了 audio_spiffs_init 类似的功能，根据上下文推断
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Audio SPIFFS init failed: %s", esp_err_to_name(ret));
        // 非致命错误，继续
    }

    // 3. 初始化SD卡
    ESP_LOGI(TAG, "Initializing SD Card...");
    ret = sd_manager_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SD Card init failed: %s", esp_err_to_name(ret));
        // 非致命错误，继续
    }
    // else
    // {
    //     // SD卡初始化成功后，打印目录内容进行调试
    //     ESP_LOGI(TAG, "Listing SD Card root directory:");
    //     sd_manager_list_dir("/sdcard");
    //     ESP_LOGI(TAG, "Listing /sdcard/mp3 directory:");
    //     sd_manager_list_dir("/sdcard/mp3");
    // }

    // 4. 初始化音频编解码器（I2C 控制面 + I2S 数据面）。
    ESP_LOGI(TAG, "Initializing Audio Codec...");
    ret = audio_codec_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Audio Codec init failed: %d", ret);
    }
    else
    {
        ESP_LOGI(TAG, "Audio system initialized successfully");
        audio_codec_set_volume(60);
    }

    // 5. 初始化板级电源观测（AXP2101），用于电量状态发布。
    ESP_LOGI(TAG, "Initializing Board Power...");
    ret = board_power_init();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Board power init failed: %s", esp_err_to_name(ret));
    }
    else
    {
        board_power_state_t state = {0};
        ret = board_power_refresh(&state);
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "Board power boot snapshot refresh failed: %s", esp_err_to_name(ret));
        }
        else
        {
            board_power_log_boot_snapshot(&state);
        }
    }

    // // 5. 扫描I2C总线
    // ESP_LOGI(TAG, "Scanning I2C Bus...");
    // i2c_manager_scan();

    // 6. 初始化配网入口与 Wi-Fi 管理。
    ESP_LOGI(TAG, "Initializing WiFi...");
    button_init();
    ret = wifi_provision_init(wifi_provision_cb);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "WiFi provision init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Hardware init complete: background network startup required");
    return ESP_OK;
}
