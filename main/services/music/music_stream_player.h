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

    /** 播放器发给 music service owner 的生命周期事件。 */
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
     * @brief 创建并启动一个 HTTP MP3 流播放器。
     *
     * micro-decoder 拥有 HTTPS reader、压缩 ring 和 MP3 decoder；调用方必须
     * 在同一个 music_service owner task 中继续调用 `music_stream_player_poll()`。
     */
    esp_err_t music_stream_player_start(
        const music_http_client_config_t *config, const char *stream_id,
        QueueHandle_t event_queue, music_stream_player_t **out_player);

    /** @brief 在 music_service owner task 中派发 micro-decoder 状态变化。 */
    void music_stream_player_poll(music_stream_player_t *player);

    /**
     * @brief 按 micro-decoder 原版语义停止并等待其内部任务退出。
     * @param[in] player 播放 worker。
     * @return ESP_OK 表示 HTTP、decoder 与音频输出 owner 已释放。
     */
    esp_err_t music_stream_player_stop(music_stream_player_t *player);

    /** @brief 判断 worker 是否已经完成停止收敛。 */
    bool music_stream_player_is_stopped(const music_stream_player_t *player);

    /**
     * @brief 释放已停止的 worker 控制块。
     *
     * 调用方正常应先停止或收到终态事件；析构会再次收敛 micro-decoder，避免
     * 释放仍被其内部任务使用的 PSRAM ring。
     */
    void music_stream_player_release(music_stream_player_t *player);

#ifdef __cplusplus
}
#endif

#endif /* MUSIC_STREAM_PLAYER_H */
