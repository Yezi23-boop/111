/**
 * MP3播放器测试示例
 *
 * 本文件演示如何使用mp3_player组件播放MP3文件
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "mp3_player.h"
#include "audio_codec.h"

static const char *TAG = "mp3_test";

/**
 * 示例1: 播放单个MP3文件
 */
void example_play_single_mp3(void)
{
    ESP_LOGI(TAG, "=== 示例1: 播放单个MP3文件 ===");

    // 播放MP3文件
    esp_err_t ret = mp3_player_play_file("/spiffs/music.mp3");
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "MP3文件开始播放");
    }
    else
    {
        ESP_LOGE(TAG, "播放失败");
    }
}

/**
 * 示例2: 播放控制 (暂停/恢复/停止)
 */
void example_playback_control(void)
{
    ESP_LOGI(TAG, "=== 示例2: 播放控制 ===");

    // 开始播放
    mp3_player_play_file("/spiffs/music.mp3");
    ESP_LOGI(TAG, "开始播放...");

    // 播放5秒
    vTaskDelay(pdMS_TO_TICKS(5000));

    // 暂停
    mp3_player_pause();
    ESP_LOGI(TAG, "暂停播放");
    vTaskDelay(pdMS_TO_TICKS(2000));

    // 恢复
    mp3_player_resume();
    ESP_LOGI(TAG, "恢复播放");
    vTaskDelay(pdMS_TO_TICKS(5000));

    // 停止
    mp3_player_stop();
    ESP_LOGI(TAG, "停止播放");
}

/**
 * 示例3: 音量控制
 */
void example_volume_control(void)
{
    ESP_LOGI(TAG, "=== 示例3: 音量控制 ===");

    // 开始播放
    mp3_player_play_file("/spiffs/music.mp3");

    // 从低音量到高音量渐变
    for (int vol = 20; vol <= 100; vol += 20)
    {
        audio_codec_set_volume(vol);
        ESP_LOGI(TAG, "音量设置为: %d", vol);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }

    // 静音
    audio_codec_set_mute(true);
    ESP_LOGI(TAG, "静音");
    vTaskDelay(pdMS_TO_TICKS(2000));

    // 取消静音
    audio_codec_set_mute(false);
    ESP_LOGI(TAG, "取消静音");
}

/**
 * 示例4: 播放列表循环
 */
void example_playlist(void)
{
    ESP_LOGI(TAG, "=== 示例4: 播放列表 ===");

    const char *playlist[] = {
        "/spiffs/song1.mp3",
        "/spiffs/song2.mp3",
        "/spiffs/song3.mp3"};
    int playlist_count = sizeof(playlist) / sizeof(playlist[0]);

    for (int i = 0; i < playlist_count; i++)
    {
        ESP_LOGI(TAG, "正在播放 [%d/%d]: %s", i + 1, playlist_count, playlist[i]);

        esp_err_t ret = mp3_player_play_file(playlist[i]);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "播放失败，跳过此文件");
            continue;
        }

        // 等待播放完成
        while (mp3_player_get_state() == AUDIO_PLAYER_STATE_PLAYING)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        ESP_LOGI(TAG, "播放完成");
        vTaskDelay(pdMS_TO_TICKS(500)); // 歌曲之间间隔0.5秒
    }

    ESP_LOGI(TAG, "播放列表全部完成");
}

/**
 * 示例5: 状态监控
 */
void example_state_monitor(void)
{
    ESP_LOGI(TAG, "=== 示例5: 状态监控 ===");

    // 开始播放
    mp3_player_play_file("/spiffs/music.mp3");

    // 持续监控状态
    for (int i = 0; i < 20; i++)
    {
        audio_player_state_t state = mp3_player_get_state();

        const char *state_str = "未知";
        switch (state)
        {
        case AUDIO_PLAYER_STATE_IDLE:
            state_str = "空闲";
            break;
        case AUDIO_PLAYER_STATE_PLAYING:
            state_str = "播放中";
            break;
        case AUDIO_PLAYER_STATE_PAUSE:
            state_str = "暂停";
            break;
        case AUDIO_PLAYER_STATE_SHUTDOWN:
            state_str = "已关闭";
            break;
        }

        ESP_LOGI(TAG, "当前状态: %s", state_str);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * 示例6: 自动检测文件格式播放
 */
void example_auto_detect_format(const char *file_path)
{
    ESP_LOGI(TAG, "=== 示例6: 自动检测格式 ===");
    ESP_LOGI(TAG, "文件路径: %s", file_path);

    // 检查文件扩展名
    if (strstr(file_path, ".mp3") != NULL || strstr(file_path, ".MP3") != NULL)
    {
        ESP_LOGI(TAG, "检测到MP3格式");
        mp3_player_play_file(file_path);
    }
    else if (strstr(file_path, ".wav") != NULL || strstr(file_path, ".WAV") != NULL)
    {
        ESP_LOGI(TAG, "检测到WAV格式");
        // 调用你的WAV播放函数
        // play_wav_file(file_path);
        ESP_LOGW(TAG, "WAV播放功能需要自行实现");
    }
    else
    {
        ESP_LOGE(TAG, "不支持的文件格式");
    }
}

/**
 * 完整的MP3播放器测试任务
 */
void mp3_player_test_task(void *pvParameters)
{
    ESP_LOGI(TAG, "MP3播放器测试开始");

    // 等待系统稳定
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 运行测试示例 (取消注释你想要测试的示例)

    // example_play_single_mp3();
    // example_playback_control();
    // example_volume_control();
    // example_playlist();
    // example_state_monitor();
    // example_auto_detect_format("/spiffs/music.mp3");

    ESP_LOGI(TAG, "MP3播放器测试完成");

    vTaskDelete(NULL);
}

/**
 * 启动MP3播放器测试
 * 在time_and_weather()中调用此函数
 */
void start_mp3_player_test(void)
{
    xTaskCreate(mp3_player_test_task, "mp3_test", 4096, NULL, 5, NULL);
}
