#ifndef MP3_PLAYER_H
#define MP3_PLAYER_H

#include "esp_err.h"
#include "audio_player.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 初始化MP3播放器
     *        必须在audio_codec_init()之后调用
     *
     * @return
     *    - ESP_OK: 成功
     *    - 其他: 失败
     */
    esp_err_t mp3_player_init(void);

    /**
     * @brief 播放音频文件 (支持MP3和WAV格式)
     *        audio_player会自动识别文件格式
     *
     * @param file_path 文件路径,例如:
     *                  - "/spiffs/music.mp3" (MP3格式)
     *                  - "/spiffs/audio.wav" (WAV格式)
     *
     * @return
     *    - ESP_OK: 成功
     *    - 其他: 失败
     */
    esp_err_t mp3_player_play_file(const char *file_path);

    /**
     * @brief 暂停播放
     *
     * @return
     *    - ESP_OK: 成功
     *    - 其他: 失败
     */
    esp_err_t mp3_player_pause(void);

    /**
     * @brief 恢复播放
     *
     * @return
     *    - ESP_OK: 成功
     *    - 其他: 失败
     */
    esp_err_t mp3_player_resume(void);

    /**
     * @brief 停止播放
     *
     * @return
     *    - ESP_OK: 成功
     *    - 其他: 失败
     */
    esp_err_t mp3_player_stop(void);

    /**
     * @brief 反初始化MP3播放器
     *
     * @return
     *    - ESP_OK: 成功
     *    - 其他: 失败
     */
    esp_err_t mp3_player_deinit(void);

    /**
     * @brief 获取播放器状态
     *
     * @return audio_player_state_t 播放器状态
     */
    audio_player_state_t mp3_player_get_state(void);

#ifdef __cplusplus
}
#endif

#endif // MP3_PLAYER_H
