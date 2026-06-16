#ifndef MEMORY_WATCH_VOICE_CLIENT_H
#define MEMORY_WATCH_VOICE_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define MEMORY_WATCH_VOICE_CLIENT_REQUEST_ID_MAX_LEN 96U
#define MEMORY_WATCH_VOICE_CLIENT_ID_MAX_BYTES \
    (MEMORY_WATCH_VOICE_CLIENT_REQUEST_ID_MAX_LEN + 1U)
#define MEMORY_WATCH_VOICE_CLIENT_STATUS_MAX_BYTES 24U
#define MEMORY_WATCH_VOICE_CLIENT_ACTION_MAX_BYTES 32U
#define MEMORY_WATCH_VOICE_CLIENT_TEXT_MAX_BYTES 256U
#define MEMORY_WATCH_VOICE_CLIENT_ERROR_MAX_BYTES 64U
#define MEMORY_WATCH_VOICE_CLIENT_DEFAULT_TIMEOUT_MS 120000U
#define MEMORY_WATCH_VOICE_CLIENT_MAX_AUDIO_BYTES (6U * 1024U * 1024U)
#define MEMORY_WATCH_VOICE_CLIENT_MAX_TEXT_BYTES 512U

    /**
     * @brief AI Memory Watch 设备侧 HTTP client 配置。
     *
     * `device_token` 只代表 watch endpoint 的设备 token；Hermes API key、
     * API Server key 与 MiMo key 必须只保留在服务器侧。
     */
    typedef struct
    {
        const char *base_url;     /**< watch endpoint 基础地址，例如 `https://watch.example.com`。 */
        const char *device_id;    /**< 服务器 allowlist 中的设备 ID。 */
        const char *device_token; /**< 设备 token，运行期配置/NVS 提供，不硬编码源码。 */
        uint32_t timeout_ms;      /**< HTTP 总等待预算；0 使用 V1 默认 120 秒。 */
        bool allow_insecure_http; /**< 仅开发联调使用；false 时拒绝明文 HTTP 发送 device token。 */
    } memory_watch_voice_client_config_t;

    /**
     * @brief voice-command 请求参数。
     */
    typedef struct
    {
        const char *request_id;       /**< `<device_id>-<boot_id>-<seq>`，最长 96 字符。 */
        const uint8_t *audio;         /**< Ogg Opus 音频数据。 */
        size_t audio_len;             /**< 音频字节数，必须在服务器 6 MiB 上限内。 */
        const char *clarification_id; /**< 可选追问 ID；无追问可传 NULL 或空字符串。 */
        bool has_battery_percent;     /**< 是否上传电量百分比。 */
        int battery_percent;          /**< 电量百分比，0..100。 */
        bool has_charging;            /**< 是否上传充电状态。 */
        bool charging;                /**< 当前是否充电。 */
        bool has_rssi;                /**< 是否上传 Wi-Fi RSSI。 */
        int rssi;                     /**< Wi-Fi RSSI，单位 dBm。 */
        const char *firmware_version; /**< 可选固件版本。 */
        const char *ui_state;         /**< 当前页面状态；NULL 时使用 `ready`。 */
    } memory_watch_voice_client_request_t;

    /**
     * @brief text-command 请求参数。
     *
     * 文本链路用于开发调试、后续手表侧候选文本输入和无麦克风路径；它仍只
     * 面向 watch endpoint，不能绕过服务器直接调用私有大脑接口。
     */
    typedef struct
    {
        const char *request_id;       /**< `<device_id>-<boot_id>-<seq>`，最长 96 字符。 */
        const char *text;             /**< 要发送给 Hermes 的 UTF-8 文本。 */
        const char *clarification_id; /**< 可选追问 ID；无追问可传 NULL 或空字符串。 */
        bool has_battery_percent;     /**< 是否上传电量百分比。 */
        int battery_percent;          /**< 电量百分比，0..100。 */
        bool has_charging;            /**< 是否上传充电状态。 */
        bool charging;                /**< 当前是否充电。 */
        bool has_rssi;                /**< 是否上传 Wi-Fi RSSI。 */
        int rssi;                     /**< Wi-Fi RSSI，单位 dBm。 */
        const char *firmware_version; /**< 可选固件版本。 */
        const char *ui_state;         /**< 当前页面状态；NULL 时使用 `ready`。 */
    } memory_watch_voice_client_text_request_t;

    /**
     * @brief watch endpoint 固定 7 字段响应。
     */
    typedef struct
    {
        int http_status;          /**< HTTP 状态码，传输失败时为 0。 */
        esp_err_t transport_error; /**< 最近一次 HTTP/解析错误。 */
        char request_id[MEMORY_WATCH_VOICE_CLIENT_ID_MAX_BYTES];
        char status[MEMORY_WATCH_VOICE_CLIENT_STATUS_MAX_BYTES];
        char action[MEMORY_WATCH_VOICE_CLIENT_ACTION_MAX_BYTES];
        char asr_text[MEMORY_WATCH_VOICE_CLIENT_TEXT_MAX_BYTES];
        char reply_text[MEMORY_WATCH_VOICE_CLIENT_TEXT_MAX_BYTES];
        char clarification_id[MEMORY_WATCH_VOICE_CLIENT_ID_MAX_BYTES];
        char error_code[MEMORY_WATCH_VOICE_CLIENT_ERROR_MAX_BYTES];
    } memory_watch_voice_client_response_t;

    /**
     * @brief `/v1/watch/health` 设备侧健康检查响应。
     */
    typedef struct
    {
        int http_status;           /**< HTTP 状态码，传输失败时为 0。 */
        esp_err_t transport_error; /**< 最近一次 HTTP/解析错误。 */
        char status[MEMORY_WATCH_VOICE_CLIENT_STATUS_MAX_BYTES];
        char hermes_status[MEMORY_WATCH_VOICE_CLIENT_STATUS_MAX_BYTES];
        char device_id[MEMORY_WATCH_VOICE_CLIENT_ID_MAX_BYTES];
    } memory_watch_voice_client_health_t;

    /**
     * @brief 进入 Hermes 页面时检查 watch endpoint 与 Hermes 在线状态。
     *
     * 该函数同步执行 HTTP GET，只允许 service/worker task 调用；UI 只能读取
     * `memory_watch_service` 发布的 snapshot，不得直接调用本函数。
     *
     * @param[in] config HTTP client 配置。
     * @param[out] out_health 健康检查响应。
     * @return `ESP_OK` 表示 HTTP 2xx 且响应解析成功。
     */
    esp_err_t memory_watch_voice_client_get_health(
        const memory_watch_voice_client_config_t *config,
        memory_watch_voice_client_health_t *out_health);

    /**
     * @brief 上传一次 Ogg Opus 语音请求并解析手表 V1 响应。
     *
     * 该函数同步执行 HTTP 请求，只允许上传 worker task 调用；
     * `memory_watch_service` owner task 后续应只投递请求、消费结果和处理
     * cancel 命令，不得在 owner task 内直接等待 120 秒 HTTP 响应。
     * 也不得在 LVGL timer/getter 或音频采样高频路径中调用。
     *
     * @param[in] config HTTP client 配置。
     * @param[in] request 请求参数。
     * @param[out] out_response 固定 7 字段响应；失败时也会尽量填入 HTTP 状态。
     * @return `ESP_OK` 表示 HTTP 2xx 且响应契约解析成功。
     */
    esp_err_t memory_watch_voice_client_post_voice_command(
        const memory_watch_voice_client_config_t *config,
        const memory_watch_voice_client_request_t *request,
        memory_watch_voice_client_response_t *out_response);

    /**
     * @brief 发送一次文本请求并解析手表 V1 响应。
     *
     * 该函数同步执行 HTTP POST，只允许 service/worker task 调用；UI 只能向
     * `memory_watch_service` 投递文本意图，不能在 LVGL 路径直接等待网络。
     *
     * @param[in] config HTTP client 配置。
     * @param[in] request 文本请求参数。
     * @param[out] out_response 固定 7 字段响应。
     * @return `ESP_OK` 表示 HTTP 2xx 且响应契约解析成功。
     */
    esp_err_t memory_watch_voice_client_post_text_command(
        const memory_watch_voice_client_config_t *config,
        const memory_watch_voice_client_text_request_t *request,
        memory_watch_voice_client_response_t *out_response);

    /**
     * @brief 通知服务器取消当前等待请求。
     *
     * 取消只负责让手表回到 ready 并尽力通知 voice endpoint；它不承诺撤销
     * Hermes 已经执行的外部工具副作用。
     *
     * @param[in] config HTTP client 配置。
     * @param[in] request_id 要取消的请求 ID。
     * @param[out] out_response 固定 7 字段响应。
     * @return `ESP_OK` 表示 HTTP 2xx 且响应契约解析成功。
     */
    esp_err_t memory_watch_voice_client_cancel_request(
        const memory_watch_voice_client_config_t *config,
        const char *request_id,
        memory_watch_voice_client_response_t *out_response);

#ifdef __cplusplus
}
#endif

#endif // MEMORY_WATCH_VOICE_CLIENT_H
