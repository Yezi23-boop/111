/**
 * @file audio_app.h
 * @brief 音频应用层接口 (录音/播放控制)
 */

#ifndef AUDIO_APP_H
#define AUDIO_APP_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 初始化音频应用
     * @return esp_err_t
     */
    esp_err_t audio_app_init(void);

    /**
     * @brief 开始录音
     * @param filename 保存的文件名 (例如 "/sdcard/record.wav")
     * @return esp_err_t
     */
    esp_err_t audio_app_start_record(const char *filename);

    /**
     * @brief 停止录音
     * @return esp_err_t
     */
    esp_err_t audio_app_stop_record(void);

    /**
     * @brief 检查是否正在录音
     * @return true 正在录音
     * @return false 未在录音
     */
    bool audio_app_is_recording(void);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_APP_H
