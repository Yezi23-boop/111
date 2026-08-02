#ifndef MUSIC_HTTP_CLIENT_H
#define MUSIC_HTTP_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "music_protocol.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** music-service 请求配置；不包含 Hermes 或网易云私有密钥。 */
    typedef struct
    {
        char base_url[MUSIC_SERVICE_URL_MAX_BYTES];
        char device_id[MUSIC_SERVICE_DEVICE_ID_MAX_BYTES];
        char device_token[MUSIC_SERVICE_DEVICE_TOKEN_MAX_BYTES];
        uint32_t timeout_ms;
        bool allow_insecure_http;
    } music_http_client_config_t;

    /** music-service 会话接口返回的最小公共字段。 */
    typedef struct
    {
        int http_status;
        esp_err_t transport_error;
        music_service_state_t state;
        music_service_mode_t mode;
        uint32_t position_ms;
        char music_session_id[MUSIC_SERVICE_SESSION_ID_MAX_BYTES];
        char stream_id[MUSIC_SERVICE_STREAM_ID_MAX_BYTES];
        char source_id[MUSIC_SERVICE_SOURCE_ID_MAX_BYTES];
        char track_id[MUSIC_SERVICE_TRACK_ID_MAX_BYTES];
        char title[MUSIC_SERVICE_TITLE_MAX_BYTES];
        char artist[MUSIC_SERVICE_ARTIST_MAX_BYTES];
        char error_code[MUSIC_SERVICE_ERROR_MAX_BYTES];
    } music_http_session_result_t;

    typedef struct music_http_stream music_http_stream_t;

    /** 账户/二维码接口返回的窄状态；二维码位图由调用方提供缓冲区。 */
    typedef struct
    {
        int http_status;
        esp_err_t transport_error;
        music_service_account_state_t state;
        uint64_t expires_at_ms;
        uint16_t qr_size;
        size_t qr_bytes;
        uint8_t *qr_data;
        size_t qr_capacity;
        char login_id[MUSIC_SERVICE_QR_LOGIN_ID_MAX_BYTES];
        char error_code[MUSIC_SERVICE_ERROR_MAX_BYTES];
    } music_http_account_result_t;

    /**
     * @brief 创建一个 music-service 播放会话。
     *
     * 首版只要求服务端支持固定测试流；真实网易云来源仍由服务器决定。
     */
    esp_err_t music_http_client_create_session(
        const music_http_client_config_t *config, const char *source_id,
        const char *track_id, const char *command_id,
        music_http_session_result_t *out_result);

    /** @brief 读取一个来源的分页歌曲摘要。 */
    esp_err_t music_http_client_fetch_tracks(
        const music_http_client_config_t *config, const char *source_id,
        uint32_t offset, music_service_catalog_snapshot_t *out_catalog);

    /** @brief 获取当前网易云账户状态。 */
    esp_err_t music_http_client_get_account(
        const music_http_client_config_t *config,
        music_http_account_result_t *out_result);

    /** @brief 创建二维码登录会话并返回模块位图。 */
    esp_err_t music_http_client_create_qr(
        const music_http_client_config_t *config,
        music_http_account_result_t *out_result);

    /** @brief 轮询二维码登录状态。 */
    esp_err_t music_http_client_poll_qr(
        const music_http_client_config_t *config, const char *login_id,
        music_http_account_result_t *out_result);

    /**
     * @brief 调用 pause/resume/previous/next 或 destroy 会话动作。
     *
     * `action` 只接受服务器契约中的窄白名单，不把通用上游 API 暴露到手表。
     */
    esp_err_t music_http_client_session_command(
        const music_http_client_config_t *config, const char *session_id,
        const char *action, const char *mode, const char *command_id,
        music_http_session_result_t *out_result);

    /**
     * @brief 打开一次音频流。
     *
     * 该接口只完成 HTTP headers；音频字节由 `read` 分块读取，不落盘整首歌曲。
     */
    esp_err_t music_http_client_open_stream(
        const music_http_client_config_t *config, const char *stream_id,
        music_http_stream_t **out_stream);

    /**
     * @brief 从已打开的音频流读取一块 MP3 数据。
     * @return ESP_OK 表示读取了数据；ESP_ERR_NOT_FOUND 表示正常流尾。
     */
    esp_err_t music_http_client_read_stream(music_http_stream_t *stream,
                                             uint8_t *buffer, size_t capacity,
                                             size_t *out_bytes);

    /** @brief 关闭并释放音频流连接。 */
    void music_http_client_close_stream(music_http_stream_t *stream);

#ifdef __cplusplus
}
#endif

#endif /* MUSIC_HTTP_CLIENT_H */
