#ifndef AUDIO_CODEC_H
#define AUDIO_CODEC_H

#include "esp_err.h"
#include "esp_codec_dev.h"

#ifdef __cplusplus
extern "C"
{
#endif

// 音频引脚定义
#define AUDIO_I2C_SDA_GPIO (15)
#define AUDIO_I2C_SCL_GPIO (14)
#define AUDIO_I2S_ASDOUT_GPIO (40)
#define AUDIO_I2S_LRCK_GPIO (45)
#define AUDIO_I2S_MCLK_GPIO (16)
#define AUDIO_I2S_SCLK_GPIO (41)
#define AUDIO_I2S_DSDIN_GPIO (42)
#define AUDIO_PA_CTRL_GPIO (46)

// 默认音频配置
#define AUDIO_DEFAULT_SAMPLE_RATE (48000)
#define AUDIO_DEFAULT_BITS_PER_SAMPLE (16)
#define AUDIO_DEFAULT_CHANNELS (2)

    /**
     * @brief 初始化音频编解码器
     *
     * @return esp_err_t ESP_OK 成功，其他失败
     */
    esp_err_t audio_codec_init(void);

    /**
     * @brief 反初始化音频编解码器
     *
     * @return esp_err_t ESP_OK 成功，其他失败
     */
    esp_err_t audio_codec_deinit(void);

    /**
     * @brief 获取播放设备句柄
     *
     * @return esp_codec_dev_handle_t 播放设备句柄
     */
    esp_codec_dev_handle_t audio_codec_get_playback_dev(void);

    /**
     * @brief 获取录音设备句柄
     *
     * @return esp_codec_dev_handle_t 录音设备句柄
     */
    esp_codec_dev_handle_t audio_codec_get_record_dev(void);

    /**
     * @brief 设置播放音量
     *
     * @param volume 音量值 (0-100)
     * @return esp_err_t ESP_OK 成功，其他失败
     */
    esp_err_t audio_codec_set_volume(int volume);

    /**
     * @brief 获取当前播放音量
     *
     * @param volume 输出音量值
     * @return esp_err_t ESP_OK 成功，其他失败
     */
    esp_err_t audio_codec_get_volume(int *volume);

    /**
     * @brief 静音控制
     *
     * @param enable true 静音，false 取消静音
     * @return esp_err_t ESP_OK 成功，其他失败
     */
    esp_err_t audio_codec_set_mute(bool enable);

    /**
     * @brief 启用功率放大器
     *
     * @param enable true 启用，false 禁用
     * @return esp_err_t ESP_OK 成功，其他失败
     */
    esp_err_t audio_codec_set_pa_enable(bool enable);

    esp_err_t audio_codec_set_record_gain(float db);

    esp_err_t audio_codec_set_record_channel_gain(uint16_t channel_mask, float db);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_CODEC_H
