#include "hardware_init.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "features/audio/audio_app.h"
#include "sd_manager.h"
#include "audio_codec.h"
#include "board_power.h"
#include "resource_fs.h"
#include "i2c_manager.h"
#include "button_gpio.h"
#include "driver/gpio.h"
#include "iot_button.h"

/*
 * 硬件初始化实现说明：
 * - 这里收敛主工程对 NVS、音频、存储、PMIC 和按键的依赖顺序；
 * - 目标是把“开机后必须具备的基础能力”一次性准备好；
 * - 真正的联网状态推进交给 `network_service`，避免初始化阶段长时间阻塞。
 */

static const char *TAG = "HARDWARE_INIT";
/* 当前板型上 BOOT 键接在 GPIO10；该值决定配网入口监听的物理按键。 */
#define BUTTON_GPIO_NUM GPIO_NUM_10

/**
 * @brief 打印开机时采集到的第一份板级电源快照。
 * @param[in] state 电源状态快照，可为 NULL。
 * @return 无返回值。
 *
 * 该日志主要用于确认 PMIC 是否已正常工作，以及 UI 初始电量展示是否有可信来源。
 */
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

/**
 * @brief 长按开始回调。
 * @param[in] arg 未使用。
 * @param[in] data 未使用。
 * @return 无返回值。
 *
 * 当前仅保留日志，用于后续扩展长按配网或恢复出厂等动作。
 */
static void button_long_press_start_cb(void *arg, void *data)
{
    ESP_LOGI(TAG, "BUTTON_LONG_PRESS_START");
}

/**
 * @brief 初始化板载 BOOT 按键。
 * @return 无返回值。
 *
 * 当前版本不再把 BOOT 键作为配网入口，避免和 UI 蓝牙开关形成双入口竞争。
 * 暂时仅保留驱动初始化和长按日志挂点，后续若要加恢复出厂等动作可继续复用。
 */
static void button_init(void)
{
    /* 这些阈值由 button 组件按毫秒解释，直接决定单击/长按判定灵敏度。 */
    button_config_t gpio_btn_cfg = {
        .long_press_time = 1500,
        .short_press_time = 180,
    };

    button_gpio_config_t gpio_cfg = {
        .gpio_num = BUTTON_GPIO_NUM, /* 物理按键 GPIO。 */
        .active_level = 1,           /* 当前硬件接法下，高电平表示按下。 */
        .enable_power_save = true,   /* 允许按钮驱动进入低功耗策略。 */
        .disable_pull = false,       /* 保留内部上下拉配置能力。 */
    };

    button_handle_t gpio_btn_handle = NULL;
    esp_err_t err = iot_button_new_gpio_device(&gpio_btn_cfg, &gpio_cfg, &gpio_btn_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Button create failed");
        return;
    }
    iot_button_register_cb(gpio_btn_handle, BUTTON_LONG_PRESS_START, NULL, button_long_press_start_cb, NULL);
}
/**
 * @brief 初始化 NVS。
 * @return `ESP_OK` 表示初始化成功；其他错误表示 NVS 不可用。
 */
static esp_err_t hardware_nvs_init(void)
{
    /* 若因分页耗尽或版本不兼容失败，先擦除后重建 NVS。 */
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
 * @brief 统一初始化基础硬件能力。
 *
 * 该入口负责准备 NVS、音频资源、通用资源分区、SD、codec、板级电源和按键输入，
 * 但不会阻塞等待 Wi-Fi 真正连通。
 *
 * @return `ESP_OK` 表示基础硬件初始化成功；
 *         其他错误表示关键初始化失败。
 */
esp_err_t hardware_init(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing NVS...");
    ret = hardware_nvs_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 音频应用层先准备目录和控制入口；即使失败，也不阻断整机启动。 */
    ESP_LOGI(TAG, "Initializing Audio SPIFFS...");
    ret = audio_app_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Audio SPIFFS init failed: %s", esp_err_to_name(ret));
    }

    /*
     * 通用资源分区给 LVGL POSIX `A:` 盘符提供 `/resources` 根目录。
     * 挂载失败只影响运行时字体/图片/音频资源，不阻断基础 UI 启动。
     */
    ESP_LOGI(TAG, "Initializing Resource LittleFS...");
    ret = resource_fs_init();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Resource LittleFS init failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Initializing SD Card...");
    ret = sd_manager_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SD Card init failed: %s", esp_err_to_name(ret));
    }

    /* codec 初始化失败会影响录音和播报，但不一定要阻断整机其余能力。 */
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

    /* 板级电源快照用于 UI 电量展示和后续功耗策略观察。 */
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

    /* 网络主链路已迁到后台 `network_service`，这里不再同步初始化旧 `wifi_provision`。 */
    ESP_LOGI(TAG, "Initializing WiFi...");
    button_init();
    ESP_LOGI(TAG, "Hardware init complete: background network startup required");
    return ESP_OK;
}
