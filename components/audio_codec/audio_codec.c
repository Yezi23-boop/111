/**
 * @file audio_codec.c
 * @brief 音频 codec 统一适配层实现
 * @details
 * - 控制面：通过 I2C 管理 ES8311/ES7210 寄存器
 * - 数据面：通过 I2S TX/RX 通道传输 PCM
 * - 对上层提供初始化、读写、音量、静音和增益控制接口
 */

#include <string.h>

#include "audio_codec_bus.h"
#include "audio_codec.h"
#include "audio_platform_config.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "es7210_adc.h"
#include "es8311_codec.h"
#include "freertos/task.h"
#include "i2c_manager.h"

static const char *TAG = "audio_codec";

/* 控制面/数据面接口对象（由 esp_codec_dev 框架创建） */
static const audio_codec_ctrl_if_t *s_playback_ctrl_if = NULL; // ES8311 I2C 控制接口
static const audio_codec_ctrl_if_t *s_record_ctrl_if = NULL;   // ES7210 I2C 控制接口
static const audio_codec_gpio_if_t *s_gpio_if = NULL;          // 公共 GPIO 控制接口（PA 使能等）
static const audio_codec_if_t *s_playback_codec_if = NULL;     // ES8311 codec 抽象接口
static const audio_codec_if_t *s_record_codec_if = NULL;       // ES7210 codec 抽象接口
static const audio_codec_data_if_t *s_data_if = NULL;          // I2S 数据接口
static esp_codec_dev_handle_t s_playback_dev = NULL;           // 播放设备句柄
static esp_codec_dev_handle_t s_record_dev = NULL;             // 录音设备句柄

/* 运行时音频参数（可被初始化流程与读写流程复用） */
static int s_current_volume = 30;                                              // 当前缓存音量（0~100）
static int s_input_sample_rate = AUDIO_PLATFORM_HW_SAMPLE_RATE;                // 录音采样率（Hz）
static int s_output_sample_rate = AUDIO_PLATFORM_HW_SAMPLE_RATE;               // 播放采样率（Hz）
static int s_physical_rx_slots = 4;                                            // RX 物理时隙数（TDM）
static const char *s_logical_input_format = AUDIO_PLATFORM_ADC_CHANNEL_FORMAT; // 逻辑输入格式标签
static int s_logical_input_channels = AUDIO_PLATFORM_HW_INPUT_CHANNELS;        // 逻辑输入通道数
static bool s_input_reference = true;                                          // 是否包含参考通道
static int s_output_channels = AUDIO_PLATFORM_HW_OUTPUT_CHANNELS;              // 输出通道数
static int s_bits_per_sample = AUDIO_PLATFORM_HW_BITS_PER_SAMPLE;              // 采样位宽（bit）
static const uint8_t s_tx_silence_preload[2048] = {0};                         // TX flush 使用的静音预装缓冲

#define ES8311_CODEC_ADDR 0x30
#define ES7210_ADC_ADDR 0x80

static int audio_codec_get_tx_slot_channels(void)
{
    // TX 至少按双声道时隙工作，避免单声道配置下的 slot 对齐问题。
    return s_output_channels > 1 ? s_output_channels : 2;
}

static bool audio_codec_channel_order_is_identity(void)
{
    return true;
}

static void audio_codec_reorder_to_logical_input_format(int16_t *samples,
                                                        size_t sample_count)
{
    (void)samples;
    (void)sample_count;
    /* Keep the logical MR order stable here if runtime probing later shows
     * the selected RX slots are not already emitted as microphone/reference. */
}

static size_t audio_codec_get_read_slice_bytes(void)
{
    int bytes_per_sample = s_bits_per_sample / 8;
    size_t frame_bytes = 0;
    size_t bytes_per_tick = 0;

    if (s_input_sample_rate <= 0 || s_logical_input_channels <= 0 ||
        bytes_per_sample <= 0)
    {
        return 1;
    }
    frame_bytes =
        (size_t)s_logical_input_channels * (size_t)bytes_per_sample;
    if (frame_bytes == 0)
    {
        return 1;
    }

    /* 保持有限等待语义，但不要把分片切得比一个 RTOS tick 还细。 */
    bytes_per_tick = (((size_t)s_input_sample_rate * frame_bytes) +
                      configTICK_RATE_HZ - 1) /
                     configTICK_RATE_HZ;
    if (bytes_per_tick < frame_bytes)
    {
        bytes_per_tick = frame_bytes;
    }
    return ((bytes_per_tick + frame_bytes - 1) / frame_bytes) * frame_bytes;
}

static size_t audio_codec_get_i2s_bus_preload_bytes(void)
{
    int bytes_per_sample = s_bits_per_sample / 8;
    size_t frame_bytes = 0;
    size_t bytes_per_tick = 0;

    if (s_output_sample_rate <= 0 || bytes_per_sample <= 0)
    {
        return 1;
    }

    frame_bytes =
        (size_t)audio_codec_get_tx_slot_channels() * (size_t)bytes_per_sample;
    if (frame_bytes == 0)
    {
        return 1;
    }

    bytes_per_tick =
        (((size_t)s_output_sample_rate * frame_bytes) + configTICK_RATE_HZ - 1) /
        configTICK_RATE_HZ;
    if (bytes_per_tick < frame_bytes)
    {
        bytes_per_tick = frame_bytes;
    }
    return ((bytes_per_tick + frame_bytes - 1) / frame_bytes) * frame_bytes;
}

static esp_err_t audio_codec_read_with_timeout_slice(
    esp_codec_dev_handle_t dev, void *buffer, size_t bytes,
    size_t *bytes_read, TickType_t ticks_to_wait, size_t slice_bytes)
{
    TickType_t start_ticks = 0;
    size_t transferred = 0;

    if (bytes_read != NULL)
    {
        *bytes_read = 0;
    }
    if (dev == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (buffer == NULL && bytes > 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (bytes == 0)
    {
        return ESP_OK;
    }
    if (slice_bytes == 0)
    {
        slice_bytes = 1;
    }

    if (ticks_to_wait == portMAX_DELAY)
    {
        // 无限等待模式：一次性读满目标长度。
        int ret = esp_codec_dev_read(dev, buffer, (int)bytes);
        if (ret != ESP_CODEC_DEV_OK)
        {
            ESP_LOGW(TAG, "audio codec read failed: %d", ret);
            return ESP_FAIL;
        }
        if (bytes_read != NULL)
        {
            *bytes_read = bytes;
        }
        return ESP_OK;
    }

    start_ticks = xTaskGetTickCount();
    while (transferred < bytes)
    {
        size_t chunk_bytes = bytes - transferred; // 本轮计划读取字节数
        int ret = 0;

        if ((xTaskGetTickCount() - start_ticks) >= ticks_to_wait &&
            transferred > 0)
        {
            break;
        }
        if (chunk_bytes > slice_bytes)
        {
            chunk_bytes = slice_bytes;
        }

        ret = esp_codec_dev_read(dev, (uint8_t *)buffer + transferred,
                                 (int)chunk_bytes);
        if (ret != ESP_CODEC_DEV_OK)
        {
            ESP_LOGW(TAG, "audio codec read failed: %d", ret);
            return ESP_FAIL;
        }

        transferred += chunk_bytes;
        if (transferred >= bytes)
        {
            break;
        }
        if (ticks_to_wait == 0 ||
            (xTaskGetTickCount() - start_ticks) >= ticks_to_wait)
        {
            break;
        }
    }

    if (bytes_read != NULL)
    {
        *bytes_read = transferred;
    }
    return transferred == bytes ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t audio_i2c_init(void)
{
    return i2c_manager_init();
}

static const audio_codec_ctrl_if_t *audio_codec_new_shared_i2c_ctrl(uint8_t addr)
{
    // addr 使用 codec 驱动约定的地址格式（与总线扫描显示风格可能不同）。
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = I2C_MANAGER_PORT,
        .addr = addr,
    };

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
    i2c_cfg.bus_handle = i2c_manager_get_bus_handle();
    if (i2c_cfg.bus_handle == NULL)
    {
        ESP_LOGE(TAG, "I2C master bus handle is NULL");
        return NULL;
    }
#endif

    return audio_codec_new_i2c_ctrl(&i2c_cfg);
}

static esp_err_t audio_codec_init_default_interfaces(void)
{
    s_playback_ctrl_if = audio_codec_new_shared_i2c_ctrl(ES8311_CODEC_ADDR);
    if (s_playback_ctrl_if == NULL)
    {
        ESP_LOGE(TAG, "Failed to create ES8311 I2C control interface");
        return ESP_FAIL;
    }

    s_record_ctrl_if = audio_codec_new_shared_i2c_ctrl(ES7210_ADC_ADDR);
    if (s_record_ctrl_if == NULL)
    {
        ESP_LOGE(TAG, "Failed to create ES7210 I2C control interface");
        return ESP_FAIL;
    }

    s_gpio_if = audio_codec_new_gpio();
    if (s_gpio_if == NULL)
    {
        ESP_LOGE(TAG, "Failed to create shared GPIO interface");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t audio_es8311_init(void)
{
    // 播放链路采样参数：决定 DAC 端 I2S 工作格式。
    esp_codec_dev_hw_gain_t hw_gain = {
        .pa_voltage = 5.0,
        .codec_dac_voltage = 3.3,
    };
    esp_codec_dev_sample_info_t sample_info = {
        .sample_rate = s_output_sample_rate,
        .channel = s_output_channels,
        .bits_per_sample = s_bits_per_sample,
    };

    if (s_playback_ctrl_if == NULL || s_gpio_if == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    s_playback_codec_if = es8311_codec_new(&(es8311_codec_cfg_t){
        .ctrl_if = s_playback_ctrl_if,
        .gpio_if = s_gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = AUDIO_PA_CTRL_GPIO,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = hw_gain,
    });
    if (s_playback_codec_if == NULL)
    {
        ESP_LOGE(TAG, "Failed to create ES8311 codec");
        return ESP_FAIL;
    }

    s_playback_dev = esp_codec_dev_new(&(esp_codec_dev_cfg_t){
        .codec_if = s_playback_codec_if,
        .data_if = s_data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    });
    if (s_playback_dev == NULL ||
        esp_codec_dev_open(s_playback_dev, &sample_info) != ESP_CODEC_DEV_OK)
    {
        ESP_LOGE(TAG, "Failed to init ES8311");
        return ESP_FAIL;
    }

    esp_codec_dev_set_out_vol(s_playback_dev, s_current_volume);
    return ESP_OK;
}

static esp_err_t audio_es7210_init(void)
{
    // 录音链路采样参数：channel 表示物理时隙数，channel_mask 决定实际采集通道。
    esp_codec_dev_sample_info_t sample_info = {
        .sample_rate = s_input_sample_rate,
        .channel = s_physical_rx_slots,
        .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
        .bits_per_sample = s_bits_per_sample,
        .mclk_multiple = 0,
    };

    if (s_record_ctrl_if == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_input_reference)
    {
        sample_info.channel_mask |= ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1);
    }

    s_record_codec_if = es7210_codec_new(&(es7210_codec_cfg_t){
        .ctrl_if = s_record_ctrl_if,
        .master_mode = false,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4,
        .mclk_src = ES7210_MCLK_FROM_PAD,
    });
    if (s_record_codec_if == NULL)
    {
        ESP_LOGE(TAG, "Failed to create ES7210 codec");
        return ESP_FAIL;
    }

    s_record_dev = esp_codec_dev_new(&(esp_codec_dev_cfg_t){
        .codec_if = s_record_codec_if,
        .data_if = s_data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    });
    if (s_record_dev == NULL ||
        esp_codec_dev_open(s_record_dev, &sample_info) != ESP_CODEC_DEV_OK)
    {
        ESP_LOGE(TAG, "Failed to init ES7210");
        return ESP_FAIL;
    }

    esp_codec_dev_set_in_gain(s_record_dev, 36.0);
    return ESP_OK;
}

esp_err_t audio_codec_init(void)
{
    esp_err_t ret;
    audio_codec_i2s_cfg_t i2s_cfg = {0};

    ret = audio_i2c_init();
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = audio_codec_bus_init();
    if (ret != ESP_OK)
    {
        return ret;
    }

    i2s_cfg.rx_handle = audio_codec_bus_get_rx_handle(); // 录音 RX 通道
    i2s_cfg.tx_handle = audio_codec_bus_get_tx_handle(); // 播放 TX 通道
    i2s_cfg.port = audio_codec_bus_get_port();           // 绑定 I2S 端口
    if (i2s_cfg.rx_handle == NULL || i2s_cfg.tx_handle == NULL)
    {
        ret = ESP_ERR_INVALID_STATE;
        ESP_LOGE(TAG, "Audio codec bus handles are unavailable");
        goto init_failed;
    }

    s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (s_data_if == NULL)
    {
        ret = ESP_FAIL;
        ESP_LOGE(TAG, "Failed to create I2S data interface");
        goto init_failed;
    }

    ret = audio_codec_init_default_interfaces();
    if (ret != ESP_OK)
    {
        goto init_failed;
    }

    ret = audio_es8311_init();
    if (ret != ESP_OK)
    {
        goto init_failed;
    }

    ret = audio_es7210_init();
    if (ret != ESP_OK)
    {
        goto init_failed;
    }

    ESP_LOGI(TAG,
             "Audio codec ready: tx_mode=std rx_mode=tdm physical_rx_slots=%d "
             "logical_input_format=%s logical_input_channels=%d "
             "input_reference=%d in=%dHz/%dbit/%dch out=%dHz/%dbit/%dch",
             s_physical_rx_slots, s_logical_input_format,
             s_logical_input_channels, s_input_reference ? 1 : 0,
             s_input_sample_rate, s_bits_per_sample, s_logical_input_channels,
             s_output_sample_rate, s_bits_per_sample, s_output_channels);
    return ESP_OK;

init_failed:
    audio_codec_deinit();
    return ret;
}

esp_err_t audio_codec_deinit(void)
{
    // 释放顺序遵循“先 device，再接口对象，再总线”，避免悬挂引用。
    if (s_playback_dev != NULL)
    {
        esp_codec_dev_close(s_playback_dev);
        esp_codec_dev_delete(s_playback_dev);
        s_playback_dev = NULL;
    }

    if (s_record_dev != NULL)
    {
        esp_codec_dev_close(s_record_dev);
        esp_codec_dev_delete(s_record_dev);
        s_record_dev = NULL;
    }

    if (s_playback_codec_if != NULL)
    {
        audio_codec_delete_codec_if(s_playback_codec_if);
        s_playback_codec_if = NULL;
    }

    if (s_record_codec_if != NULL)
    {
        audio_codec_delete_codec_if(s_record_codec_if);
        s_record_codec_if = NULL;
    }

    if (s_playback_ctrl_if != NULL)
    {
        audio_codec_delete_ctrl_if(s_playback_ctrl_if);
        s_playback_ctrl_if = NULL;
    }

    if (s_record_ctrl_if != NULL)
    {
        audio_codec_delete_ctrl_if(s_record_ctrl_if);
        s_record_ctrl_if = NULL;
    }

    if (s_gpio_if != NULL)
    {
        audio_codec_delete_gpio_if(s_gpio_if);
        s_gpio_if = NULL;
    }

    if (s_data_if != NULL)
    {
        audio_codec_delete_data_if(s_data_if);
        s_data_if = NULL;
    }

    (void)audio_codec_bus_deinit();

    ESP_LOGI(TAG, "Audio codec deinitialized");
    return ESP_OK;
}

esp_err_t audio_codec_read(void *buffer, size_t bytes, size_t *bytes_read,
                           TickType_t ticks_to_wait)
{
    TickType_t start_ticks = xTaskGetTickCount();
    esp_err_t ret = audio_codec_read_with_timeout_slice(
        s_record_dev, buffer, bytes, bytes_read, ticks_to_wait,
        audio_codec_get_read_slice_bytes());
    size_t actual_bytes = bytes_read != NULL ? *bytes_read : bytes; // 实际读取长度

    if (ret == ESP_OK && buffer != NULL &&
        !audio_codec_channel_order_is_identity())
    {
        audio_codec_reorder_to_logical_input_format(
            (int16_t *)buffer, actual_bytes / sizeof(int16_t));
    }

    ESP_LOGD(TAG,
             "read target=%u actual=%u elapsed_ticks=%lu wait_ticks=%lu ret=%s",
             (unsigned int)bytes,
             (unsigned int)actual_bytes,
             (unsigned long)(xTaskGetTickCount() - start_ticks),
             (unsigned long)ticks_to_wait, esp_err_to_name(ret));
    return ret;
}

esp_err_t audio_codec_write(const void *buffer, size_t bytes)
{
    if (s_playback_dev == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (buffer == NULL && bytes > 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (bytes == 0)
    {
        return ESP_OK;
    }

    if (esp_codec_dev_write(s_playback_dev, (void *)buffer, (int)bytes) !=
        ESP_CODEC_DEV_OK)
    {
        ESP_LOGW(TAG, "audio codec write failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t audio_codec_flush_output(void)
{
    esp_err_t ret = ESP_OK;
    i2s_chan_handle_t tx_handle = audio_codec_bus_get_tx_handle(); // TX 通道句柄
    size_t preload_bytes = 0;                                      // 计划预装静音字节数
    size_t bytes_loaded = 0;                                       // 实际预装成功字节数

    if (tx_handle == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    preload_bytes = audio_codec_get_i2s_bus_preload_bytes();
    if (preload_bytes > sizeof(s_tx_silence_preload))
    {
        preload_bytes = sizeof(s_tx_silence_preload);
    }

    ret = i2s_channel_disable(tx_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "failed to disable I2S TX for flush: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    ret = i2s_channel_preload_data(tx_handle, s_tx_silence_preload,
                                   preload_bytes, &bytes_loaded);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "failed to preload silence for I2S TX flush: %s",
                 esp_err_to_name(ret));
        (void)i2s_channel_enable(tx_handle);
        return ret;
    }

    ret = i2s_channel_enable(tx_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "failed to enable I2S TX after flush: %s",
                 esp_err_to_name(ret));
        return ret;
    }
    if (bytes_loaded == 0)
    {
        ESP_LOGW(TAG, "no silence bytes were preloaded during TX flush");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t audio_codec_set_volume(int volume)
{
    if (s_playback_dev == NULL || volume < 0 || volume > 100)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (esp_codec_dev_set_out_vol(s_playback_dev, volume) != ESP_CODEC_DEV_OK)
    {
        return ESP_FAIL;
    }
    s_current_volume = volume;
    return ESP_OK;
}

esp_err_t audio_codec_get_volume(int *volume)
{
    if (s_playback_dev == NULL || volume == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    *volume = s_current_volume;
    return ESP_OK;
}

esp_err_t audio_codec_set_mute(bool enable)
{
    if (s_playback_dev == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_codec_dev_set_out_mute(s_playback_dev, enable) ==
                   ESP_CODEC_DEV_OK
               ? ESP_OK
               : ESP_FAIL;
}

esp_err_t audio_codec_set_pa_enable(bool enable)
{
    if (s_gpio_if == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    return s_gpio_if->set(AUDIO_PA_CTRL_GPIO, enable) == ESP_CODEC_DEV_OK
               ? ESP_OK
               : ESP_FAIL;
}

esp_err_t audio_codec_set_record_gain(float db)
{
    if (s_record_dev == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_codec_dev_set_in_gain(s_record_dev, db) == ESP_CODEC_DEV_OK
               ? ESP_OK
               : ESP_FAIL;
}
