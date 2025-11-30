/**
 * @file audio_app.c
 * @brief 音频应用层实现 (录音/播放控制)
 */

#include "audio_app.h"
#include "audio_codec.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sd_manager.h>
#include <sys/types.h>
#include <sys/stat.h>

static const char *TAG = "audio_app";

// 录音任务句柄
static TaskHandle_t s_record_task_handle = NULL;
// 录音状态标志
static volatile bool s_is_recording = false;
// 录音文件名
static char s_record_filename[128] = {0};

// WAV文件头结构体
typedef struct
{
    char riff_tag[4];         // "RIFF"
    uint32_t riff_len;        // 文件总长度 - 8
    char wave_tag[4];         // "WAVE"
    char fmt_tag[4];          // "fmt "
    uint32_t fmt_len;         // fmt块长度 (通常16)
    uint16_t audio_fmt;       // 音频格式 (1 = PCM)
    uint16_t channels;        // 声道数
    uint32_t sample_rate;     // 采样率
    uint32_t byte_rate;       // 字节率 = 采样率 * 声道数 * 位深/8
    uint16_t block_align;     // 块对齐 = 声道数 * 位深/8
    uint16_t bits_per_sample; // 位深 (16)
    char data_tag[4];         // "data"
    uint32_t data_len;        // 数据块长度
} wav_header_t;

// 生成WAV头
static void generate_wav_header(wav_header_t *header, uint32_t data_len, uint32_t sample_rate, uint16_t channels, uint16_t bits)
{
    memcpy(header->riff_tag, "RIFF", 4);
    header->riff_len = data_len + sizeof(wav_header_t) - 8;
    memcpy(header->wave_tag, "WAVE", 4);
    memcpy(header->fmt_tag, "fmt ", 4);
    header->fmt_len = 16;
    header->audio_fmt = 1; // PCM
    header->channels = channels;
    header->sample_rate = sample_rate;
    header->byte_rate = sample_rate * channels * bits / 8;
    header->block_align = channels * bits / 8;
    header->bits_per_sample = bits;
    memcpy(header->data_tag, "data", 4);
    header->data_len = data_len;
}

// 录音任务
static void record_task(void *arg)
{
    esp_codec_dev_handle_t record_dev = audio_codec_get_record_dev();
    if (record_dev == NULL)
    {
        ESP_LOGE(TAG, "无法获取录音设备");
        s_is_recording = false;
        vTaskDelete(NULL);
        return;
    }

    FILE *f = fopen(s_record_filename, "wb");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "无法创建录音文件: %s", s_record_filename);
        s_is_recording = false;
        vTaskDelete(NULL);
        return;
    }

    // 预留WAV头空间
    wav_header_t header;
    memset(&header, 0, sizeof(wav_header_t));
    fwrite(&header, 1, sizeof(wav_header_t), f);

    ESP_LOGI(TAG, "开始录音: %s", s_record_filename);
    // 提升录音增益：默认提升到36dB，适配低灵敏度驻极体麦克风
    audio_codec_set_record_gain(36.0f);

    // 申请缓冲区
    size_t buf_size = 4096 * 1; // 可调节为2048/4096以平衡时延与I/O
    // uint8_t *buffer = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *buffer = (uint8_t *)malloc(buf_size);
    if (buffer == NULL)
    {
        ESP_LOGE(TAG, "内存不足");
        fclose(f);
        s_is_recording = false;
        vTaskDelete(NULL);
        return;
    }

    size_t total_bytes = 0;
    int read_res;

    while (s_is_recording)
    {
        read_res = esp_codec_dev_read(record_dev, buffer, buf_size);
        if (read_res == ESP_CODEC_DEV_OK)
        {
            fwrite(buffer, 1, buf_size, f);
            total_bytes += buf_size;
        }
        else
        {
            ESP_LOGW(TAG, "读取音频数据失败或超时: %d", read_res);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    // 录音结束，回填WAV头
    ESP_LOGI(TAG, "录音结束，正在保存... 总大小: %d 字节", total_bytes);

    // 假设采样率48000, 双声道, 16位 (需与audio_codec配置一致)
    generate_wav_header(&header, total_bytes, 48000, 2, 16);
    fseek(f, 0, SEEK_SET);
    fwrite(&header, 1, sizeof(wav_header_t), f);

    fclose(f);
    free(buffer);

    s_record_task_handle = NULL;
    ESP_LOGI(TAG, "录音文件已保存");
    vTaskDelete(NULL);
}
esp_err_t audio_app_init(void)
{
    // 这里可以做一些应用层的初始化，目前驱动层已经初始化好了
    ESP_LOGI(TAG, "音频应用初始化");

    return ESP_OK;
}

esp_err_t audio_app_start_record(const char *filename)
{
    if (s_is_recording)
    {
        ESP_LOGW(TAG, "正在录音中，请先停止");
        return ESP_ERR_INVALID_STATE;
    }

    if (filename == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(s_record_filename, filename, sizeof(s_record_filename) - 1);
    s_is_recording = true;

    // 创建录音任务，优先级稍高，分配4KB栈空间
    BaseType_t ret = xTaskCreate(record_task, "RecTask", 4096, NULL, 5, &s_record_task_handle);
    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "创建录音任务失败");
        s_is_recording = false;
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t audio_app_stop_record(void)
{
    if (!s_is_recording)
    {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "请求停止录音...");
    s_is_recording = false;

    // 等待任务结束（简单处理，实际可能需要信号量同步）
    // 这里不wait也行，任务会自动删除

    return ESP_OK;
}

bool audio_app_is_recording(void)
{
    return s_is_recording;
}
