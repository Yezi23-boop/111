#ifndef _WS_SERVER_H_
#define _WS_SERVER_H_
#include "esp_err.h"
#include <stdint.h>

// WebSocket 文本消息接收回调。
typedef void (*ws_server_receive_cb)(const char *data, int len);
typedef struct
{
    const char *html_code;   // 根路径返回的 HTML 内容
    ws_server_receive_cb cb; // WebSocket 文本消息处理回调
} ws_server_config_t;

esp_err_t ws_server_start(ws_server_config_t *config);

esp_err_t ws_server_stop(void);

esp_err_t ws_server_send(uint8_t *data, int len);
#endif