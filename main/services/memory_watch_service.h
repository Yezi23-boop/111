#ifndef MEMORY_WATCH_SERVICE_H
#define MEMORY_WATCH_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define MEMORY_WATCH_SERVICE_ID_MAX_BYTES 64
#define MEMORY_WATCH_SERVICE_TEXT_MAX_BYTES 128
#define MEMORY_WATCH_SERVICE_URL_MAX_BYTES 128
#define MEMORY_WATCH_SERVICE_DEVICE_ID_MAX_BYTES 32
#define MEMORY_WATCH_SERVICE_DEVICE_TOKEN_MAX_BYTES 128
#define MEMORY_WATCH_SERVICE_HEALTH_TIMEOUT_MS 5000U

    /**
     * @brief AI Memory Watch 服务状态。
     *
     * 状态只描述 ESP32-S3 侧的当前交互阶段；长期记忆、ASR 和工具执行结果仍由
     * 服务器侧 Hermes 负责。
     */
    typedef enum
    {
        MEMORY_WATCH_SERVICE_STATE_READY = 0,       /**< 可开始一次新的语音请求。 */
        MEMORY_WATCH_SERVICE_STATE_WAITING_NETWORK, /**< 等待网络 service ready。 */
        MEMORY_WATCH_SERVICE_STATE_RECORDING,       /**< 正在录音。 */
        MEMORY_WATCH_SERVICE_STATE_ENCODING,        /**< 正在封装 Ogg Opus。 */
        MEMORY_WATCH_SERVICE_STATE_UPLOADING,       /**< 正在上传到 watch endpoint。 */
        MEMORY_WATCH_SERVICE_STATE_THINKING,        /**< 已上传，等待 Hermes 最终文本。 */
        MEMORY_WATCH_SERVICE_STATE_NEEDS_CLARIFICATION, /**< Hermes 需要补充说明。 */
        MEMORY_WATCH_SERVICE_STATE_DONE,            /**< 本次交互已完成。 */
        MEMORY_WATCH_SERVICE_STATE_TIMEOUT,         /**< 等待服务器超过手表侧预算。 */
        MEMORY_WATCH_SERVICE_STATE_ERROR,           /**< 本次交互失败。 */
        MEMORY_WATCH_SERVICE_STATE_CANCELED,        /**< 用户取消录音或等待。 */
    } memory_watch_service_state_t;

    /**
     * @brief UI 读取的 AI Memory Watch 快照。
     *
     * getter 只复制该结构，不做网络请求、不读音频、不推进状态机，避免 LVGL
     * 定时刷新路径阻塞。
     */
    typedef struct
    {
        memory_watch_service_state_t state; /**< 当前服务状态。 */
        bool network_ready;                 /**< 最近一次观测到的网络 service ready。 */
        bool endpoint_configured;           /**< 是否已有运行期 watch endpoint 配置。 */
        bool hermes_online;                 /**< 最近一次 `/v1/watch/health` 是否确认 Hermes 在线。 */
        bool request_active;                /**< 是否有未收敛的 active request。 */
        bool clarification_active;          /**< 是否正在回答 Hermes 追问。 */
        esp_err_t last_error;               /**< 最近一次服务层错误。 */
        char request_id[MEMORY_WATCH_SERVICE_ID_MAX_BYTES];      /**< 当前请求 ID。 */
        char clarification_id[MEMORY_WATCH_SERVICE_ID_MAX_BYTES]; /**< 当前追问 ID。 */
        char asr_text[MEMORY_WATCH_SERVICE_TEXT_MAX_BYTES];       /**< 最近 ASR 文本。 */
        char reply_text[MEMORY_WATCH_SERVICE_TEXT_MAX_BYTES];     /**< 最近 Hermes 回复。 */
    } memory_watch_service_snapshot_t;

    /**
     * @brief AI Memory Watch watch endpoint 运行期配置。
     *
     * `device_token` 只用于 ESP32-S3 到 watch endpoint 的设备鉴权。Hermes
     * API key、MiMo key 或 API Server key 不属于本结构，也不得写入固件源码。
     */
    typedef struct
    {
        const char *base_url;     /**< watch endpoint 基础地址，例如 `https://watch.example.com`。 */
        const char *device_id;    /**< 设备 ID，例如 `watch-001`。 */
        const char *device_token; /**< 运行期配置/NVS 提供的设备 token。 */
        uint32_t timeout_ms;      /**< 语音请求等待预算；0 使用 voice client 默认值。 */
        bool allow_insecure_http; /**< 仅本地开发联调允许明文 HTTP。 */
    } memory_watch_service_endpoint_config_t;

    /**
     * @brief 初始化 AI Memory Watch owner task。
     * @return `ESP_OK` 表示已初始化或本次初始化成功。
     */
    esp_err_t memory_watch_service_init(void);

    /**
     * @brief 配置 watch endpoint。
     *
     * 该接口只复制运行期提供的配置，不持久化、不打印 token，也不触发网络请求。
     *
     * @param[in] config endpoint 配置，字符串不能为空。
     * @return `ESP_OK` 表示已复制配置。
     */
    esp_err_t memory_watch_service_configure_endpoint(
        const memory_watch_service_endpoint_config_t *config);

    /**
     * @brief 保存 watch endpoint 配置到 NVS 并应用到运行期。
     *
     * 该接口只保存 ESP32-S3 到 watch endpoint 的 `device_token`，不保存
     * Hermes API key、MiMo key 或 API Server key；日志也不得打印 token。
     * 如当前已有未完成请求，为避免中途切换服务器配置，将返回
     * `ESP_ERR_INVALID_STATE`。
     *
     * @param[in] config endpoint 配置，字符串不能为空。
     * @return `ESP_OK` 表示已写入 NVS 并应用运行期配置。
     */
    esp_err_t memory_watch_service_save_endpoint_to_nvs(
        const memory_watch_service_endpoint_config_t *config);

    /**
     * @brief 请求 owner task 检查 `/v1/watch/health`。
     *
     * UI 进入 Hermes 页面时可投递一次该命令；实际 HTTP GET 在 service task
     * 中执行，且 health 请求使用短超时，不阻塞 LVGL 刷新路径。
     *
     * @return `ESP_OK` 表示命令已投递到 owner task。
     */
    esp_err_t memory_watch_service_check_health(void);

    /**
     * @brief 用户按住语音按钮，开始一次录音意图。
     * @return `ESP_OK` 表示命令已投递到 owner task。
     */
    esp_err_t memory_watch_service_begin_recording(void);

    /**
     * @brief 用户松开发送，提交当前录音。
     * @return `ESP_OK` 表示命令已投递到 owner task。
     */
    esp_err_t memory_watch_service_send_recording(void);

    /**
     * @brief 用户滑出按钮或松手取消，丢弃当前录音。
     * @return `ESP_OK` 表示命令已投递到 owner task。
     */
    esp_err_t memory_watch_service_cancel_recording(void);

    /**
     * @brief 用户取消等待服务器结果。
     * @return `ESP_OK` 表示命令已投递到 owner task。
     */
    esp_err_t memory_watch_service_cancel_waiting(void);

    /**
     * @brief 用户取消当前 Hermes 追问。
     * @return `ESP_OK` 表示命令已投递到 owner task。
     */
    esp_err_t memory_watch_service_cancel_clarification(void);

    /**
     * @brief 复制当前服务快照。
     * @param[out] out_snapshot 输出快照，不能为空。
     * @return `ESP_OK` 表示成功复制。
     */
    esp_err_t memory_watch_service_get_snapshot(
        memory_watch_service_snapshot_t *out_snapshot);

    /**
     * @brief 获取状态字符串。
     * @param[in] state 服务状态。
     * @return 静态字符串。
     */
    const char *memory_watch_service_state_to_string(
        memory_watch_service_state_t state);

#ifdef __cplusplus
}
#endif

#endif // MEMORY_WATCH_SERVICE_H
