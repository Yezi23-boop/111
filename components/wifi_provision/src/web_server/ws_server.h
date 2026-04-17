#ifndef _WS_SERVER_H_
#define _WS_SERVER_H_
#include "esp_err.h"
#include <stdint.h>

/* WebSocket 文本消息接收回调。 */
typedef void (*ws_server_receive_cb)(const char *data, int len);

typedef struct
{
    const char *html_code;   /**< 根路径返回的 HTML 内容。 */
    ws_server_receive_cb cb; /**< WebSocket 文本消息处理回调。 */
} ws_server_config_t;

/**
 * @brief 启动 HTTP + WebSocket 服务器。
 * @param[in] config 服务器配置。
 * @return `ESP_OK` 表示成功；其他错误表示参数非法或 HTTP 服务器启动失败。
 */
esp_err_t ws_server_start(ws_server_config_t *config);

/**
 * @brief 停止 HTTP + WebSocket 服务器。
 * @return `ESP_OK` 表示成功。
 */
esp_err_t ws_server_stop(void);

/**
 * @brief 通过 WebSocket 主动发送一帧文本消息。
 * @param[in] data 要发送的数据。
 * @param[in] len 数据长度，单位为字节。
 * @return `ESP_OK` 表示成功；其他错误表示当前未连接或发送失败。
 */
esp_err_t ws_server_send(uint8_t *data, int len);
#endif
