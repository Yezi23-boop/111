#ifndef AUDIO_CODEC_H
#define AUDIO_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * 硬件引脚映射
 * - I2C: codec 控制面
 * - I2S: 音频数据面
 * - PA: 功放使能控制
 */
#define AUDIO_I2C_SDA_GPIO (15)    // I2C SDA
#define AUDIO_I2C_SCL_GPIO (14)    // I2C SCL
#define AUDIO_I2S_ASDOUT_GPIO (40) // I2S TX 数据输出（到 DAC）
#define AUDIO_I2S_LRCK_GPIO (45)   // I2S LRCK/WS
#define AUDIO_I2S_MCLK_GPIO (16)   // I2S MCLK
#define AUDIO_I2S_SCLK_GPIO (41)   // I2S BCLK/SCLK
#define AUDIO_I2S_DSDIN_GPIO (42)  // I2S RX 数据输入（来自 ADC）
#define AUDIO_PA_CTRL_GPIO (46)    // 外部功放使能引脚

    /**
     * @brief 初始化音频 codec 子系统
     * @details 包含 I2C、I2S 总线、ES8311（播放）和 ES7210（录音）初始化。
     */
    esp_err_t audio_codec_init(void);

    /**
     * @brief 反初始化音频 codec 子系统并释放资源
     */
    esp_err_t audio_codec_deinit(void);

    /**
     * @brief 从录音链路读取 PCM 数据
     * @param buffer 接收缓冲区地址
     * @param bytes 期望读取字节数
     * @param bytes_read 输出参数，返回实际读取字节数，可为 NULL
     * @param ticks_to_wait 最大等待 tick 数，portMAX_DELAY 表示无限等待
     */
    esp_err_t audio_codec_read(void *buffer, size_t bytes, size_t *bytes_read,
                               TickType_t ticks_to_wait);

    /**
     * @brief 向播放链路写入 PCM 数据
     * @param buffer PCM 数据地址
     * @param bytes 待写入字节数
     */
    esp_err_t audio_codec_write(const void *buffer, size_t bytes);

    /**
     * @brief 通过预装静音数据刷新 TX 通道，减少尾音残留
     */
    esp_err_t audio_codec_flush_output(void);

    /**
     * @brief 设置播放音量
     * @param volume 音量百分比（0~100）
     */
    esp_err_t audio_codec_set_volume(int volume);

    /**
     * @brief 获取当前缓存音量
     * @param volume 输出参数，返回 0~100
     */
    esp_err_t audio_codec_get_volume(int *volume);

    /**
     * @brief 设置播放静音状态
     * @param enable true=静音，false=取消静音
     */
    esp_err_t audio_codec_set_mute(bool enable);

    /**
     * @brief 控制外部功放使能引脚
     * @param enable true=打开功放，false=关闭功放
     */
    esp_err_t audio_codec_set_pa_enable(bool enable);

    /**
     * @brief 设置录音增益
     * @param db 目标增益（dB）
     */
    esp_err_t audio_codec_set_record_gain(float db);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_CODEC_H
