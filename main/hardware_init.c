#include "hardware_init.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "simple_wifi_sta.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "audio_app.h"
#include "sd_manager.h"
#include "audio_codec.h"
#include "i2c_manager.h"

static const char *TAG = "HARDWARE_INIT";

// 内部使用的事件组
static EventGroupHandle_t s_wifi_ev_handle = NULL;
#define WIFI_CONNECT_BIT BIT0

/**
 * @brief WiFi事件回调函数
 * @param ev WiFi事件类型
 */
static void wifi_event_handler(WIFI_EV_e ev)
{
    if (ev == WIFI_CONNECTED) // 如果WiFi连接成功
    {
        ESP_LOGI(TAG, "WiFi Connected Event Received");
        // 设置事件组中的WiFi连接位，通知等待任务
        if (s_wifi_ev_handle != NULL)
        {
            xEventGroupSetBits(s_wifi_ev_handle, WIFI_CONNECT_BIT);
        }
    }
    else if (ev == WIFI_DISCONNECTED)
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
 * @details 初始化NVS、WiFi、SPIFFS、SD卡、I2C总线和音频编解码器，并阻塞等待WiFi连接成功
 * @return esp_err_t ESP_OK: 初始化成功且WiFi已连接; 其他: 初始化失败
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

    // 2. 初始化音频SPIFFS (录音/播放需要)
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
    else
    {
        // SD卡初始化成功后，打印目录内容进行调试
        ESP_LOGI(TAG, "Listing SD Card root directory:");
        sd_manager_list_dir("/sdcard");
        ESP_LOGI(TAG, "Listing /sdcard/mp3 directory:");
        sd_manager_list_dir("/sdcard/mp3");
    }

    // 4. 初始化音频编解码器
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

    // 5. 扫描I2C总线
    ESP_LOGI(TAG, "Scanning I2C Bus...");
    i2c_manager_scan();

    // 6. 创建事件组
    s_wifi_ev_handle = xEventGroupCreate();
    if (s_wifi_ev_handle == NULL)
    {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }

    // 7. WiFi初始化
    ESP_LOGI(TAG, "Initializing WiFi...");
    ret = wifi_sta_init(wifi_event_handler);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 8. 等待连接
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_ev_handle,
        WIFI_CONNECT_BIT,
        pdTRUE,       // 退出时清除位
        pdFALSE,      // 等待任意位
        portMAX_DELAY // 永久等待
    );

    if (bits & WIFI_CONNECT_BIT)
    {
        ESP_LOGI(TAG, "Hardware init complete: WiFi Connected");
        return ESP_OK;
    }
    else
    {
        ESP_LOGE(TAG, "Hardware init failed: WiFi Timeout");
        return ESP_FAIL;
    }
}
