#include "mp3_player.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_spiffs.h"
#include "audio_player.h"
#include "audio_codec.h"

static const char *TAG = "mp3_player";

// 音频播放器回调函数
static void audio_player_callback(audio_player_cb_ctx_t *ctx)
{
    switch (ctx->audio_event)
    {
    case AUDIO_PLAYER_CALLBACK_EVENT_IDLE:
        ESP_LOGI(TAG, "播放器状态: 空闲");
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_PLAYING:
        ESP_LOGI(TAG, "播放器状态: 正在播放");
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_COMPLETED_PLAYING_NEXT:
        ESP_LOGI(TAG, "播放器状态: 切换到下一首");
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_PAUSE:
        ESP_LOGI(TAG, "播放器状态: 暂停");
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_SHUTDOWN:
        ESP_LOGI(TAG, "播放器状态: 关闭");
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_UNKNOWN_FILE_TYPE:
        ESP_LOGE(TAG, "错误: 未知文件类型");
        break;
    default:
        ESP_LOGW(TAG, "未知事件: %d", ctx->audio_event);
        break;
    }
}

// 静音控制回调
static esp_err_t audio_mute_callback(AUDIO_PLAYER_MUTE_SETTING setting)
{
    bool mute = (setting == AUDIO_PLAYER_MUTE);
    ESP_LOGI(TAG, "静音设置: %s", mute ? "开启" : "关闭");
    return audio_codec_set_mute(mute);
}

// I2S写入回调
static esp_err_t audio_write_callback(void *audio_buffer, size_t len, size_t *bytes_written, uint32_t timeout_ms)
{
    esp_codec_dev_handle_t playback_dev = audio_codec_get_playback_dev();
    if (playback_dev == NULL)
    {
        ESP_LOGE(TAG, "播放设备未初始化");
        return ESP_FAIL;
    }

    int ret = esp_codec_dev_write(playback_dev, audio_buffer, len);
    if (ret > 0)
    {
        *bytes_written = ret;
        return ESP_OK;
    }
    else
    {
        *bytes_written = 0;
        return ESP_FAIL;
    }
}

// I2S时钟重配置回调
static esp_err_t audio_clk_reconfig_callback(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch)
{
    ESP_LOGI(TAG, "重配置I2S时钟: %lu Hz, %lu bits, %s",
             rate, bits_cfg,
             ch == I2S_SLOT_MODE_MONO ? "单声道" : "立体声");

    // 这里可以添加重新配置I2S时钟的代码
    // 因为audio_codec已经配置好了,通常不需要动态改变
    // 如果需要支持不同采样率的MP3文件,可以在这里实现

    return ESP_OK;
}

esp_err_t mp3_player_init(void)
{
    ESP_LOGI(TAG, "初始化MP3播放器");

    // 配置audio_player
    audio_player_config_t config = {
        .mute_fn = audio_mute_callback,
        .write_fn = audio_write_callback,
        .clk_set_fn = audio_clk_reconfig_callback,
        .priority = 5, // 任务优先级
        .coreID = 0    // 运行在核心0
    };

    esp_err_t ret = audio_player_new(config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "创建audio_player失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 注册事件回调
    ret = audio_player_callback_register(audio_player_callback, NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "注册回调失败: %s", esp_err_to_name(ret));
        audio_player_delete();
        return ret;
    }

    ESP_LOGI(TAG, "MP3播放器初始化成功");
    return ESP_OK;
}

esp_err_t mp3_player_play_file(const char *file_path)
{
    if (file_path == NULL)
    {
        ESP_LOGE(TAG, "文件路径为空");
        return ESP_ERR_INVALID_ARG;
    }

    // 检测文件格式
    const char *file_ext = strrchr(file_path, '.');
    const char *format_name = "未知";

    if (file_ext != NULL)
    {
        if (strcasecmp(file_ext, ".mp3") == 0)
        {
            format_name = "MP3";
        }
        else if (strcasecmp(file_ext, ".wav") == 0)
        {
            format_name = "WAV";
        }
    }

    ESP_LOGI(TAG, "准备播放文件: %s (格式: %s)", file_path, format_name);

    // 打开文件
    FILE *fp = fopen(file_path, "rb");
    if (fp == NULL)
    {
        ESP_LOGE(TAG, "无法打开文件: %s", file_path);
        return ESP_FAIL;
    }

    // 获取文件大小
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    ESP_LOGI(TAG, "文件大小: %ld 字节 (%.2f MB)", file_size, file_size / 1024.0 / 1024.0);

    // 调用audio_player播放 (自动识别MP3和WAV格式)
    // 注意: audio_player_play会接管fp的生命周期,播放完成后会自动fclose
    esp_err_t ret = audio_player_play(fp);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "播放失败: %s", esp_err_to_name(ret));
        fclose(fp); // 如果播放失败,需要手动关闭文件
        return ret;
    }

    ESP_LOGI(TAG, "开始播放 %s 文件", format_name);
    return ESP_OK;
}

esp_err_t mp3_player_pause(void)
{
    ESP_LOGI(TAG, "暂停播放");
    return audio_player_pause();
}

esp_err_t mp3_player_resume(void)
{
    ESP_LOGI(TAG, "恢复播放");
    return audio_player_resume();
}

esp_err_t mp3_player_stop(void)
{
    ESP_LOGI(TAG, "停止播放");
    return audio_player_stop();
}

esp_err_t mp3_player_deinit(void)
{
    ESP_LOGI(TAG, "反初始化MP3播放器");
    return audio_player_delete();
}

audio_player_state_t mp3_player_get_state(void)
{
    return audio_player_get_state();
}
