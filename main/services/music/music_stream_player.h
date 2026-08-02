#ifndef MUSIC_STREAM_PLAYER_H
#define MUSIC_STREAM_PLAYER_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "music_http_client.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct music_stream_player music_stream_player_t;

    /** 播放 worker 发给 music service 的生命周期事件。 */
    typedef enum
    {
        MUSIC_STREAM_PLAYER_EVENT_BUFFERING = 0,
        MUSIC_STREAM_PLAYER_EVENT_PLAYING,
        MUSIC_STREAM_PLAYER_EVENT_ENDED,
        MUSIC_STREAM_PLAYER_EVENT_STOPPED,
        MUSIC_STREAM_PLAYER_EVENT_ERROR,
    } music_stream_player_event_type_t;

    typedef struct
    {
        music_stream_player_event_type_t type;
        esp_err_t error;
        size_t buffered_bytes;
    } music_stream_player_event_t;

    /**
     * @brief 创建并启动一个 HTTP MP3 流播放 worker。
     *
     * 压缩 ring、PCM 临时块和 worker 栈均不放在调用方栈上；全局并发限制由
     * `music_service` owner 保证，这个模块只负责一条已授权 stream_id。
     */
    esp_err_t music_stream_player_start(
        const music_http_client_config_t *config, const char *stream_id,
        QueueHandle_t event_queue, music_stream_player_t **out_player);

    /**
     * @brief 请求停止并等待 worker 释放 HTTP、decoder 与音频输出 owner。
     * @param[in] player 播放 worker。
     * @param[in] timeout_ms 等待时间，单位毫秒。
     * @return ESP_OK 表示 worker 已停止；超时表示仍在收敛。
     */
    esp_err_t music_stream_player_stop(music_stream_player_t *player,
                                       uint32_t timeout_ms);

    /** @brief 判断 worker 是否已经完成停止收敛。 */
    bool music_stream_player_is_stopped(const music_stream_player_t *player);

    /**
     * @brief 释放已停止的 worker 控制块。
     *
     * 调用方必须先调用 `music_stream_player_stop()`；该函数不会强杀仍在运行
     * 的任务，避免释放正在使用的 PSRAM ring。
     */
    void music_stream_player_release(music_stream_player_t *player);

#ifdef __cplusplus
}
#endif

#endif /* MUSIC_STREAM_PLAYER_H */
