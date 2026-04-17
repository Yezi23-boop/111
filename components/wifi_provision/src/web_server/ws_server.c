#include "ws_server.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "soc/gpio_sig_map.h"
#include "string.h"

#define TAG "ws_server"

static const char *http_html = NULL;         // 配网 HTML 页面内容指针。
static ws_server_receive_cb ws_server_cb = NULL; // WebSocket 文本消息回调。
static httpd_handle_t server_handle = NULL;  // HTTP 服务器句柄。
static int socket_fd = -1;                   // 当前 WebSocket 客户端 socket 描述符。

/**
 * @brief 处理根路径 HTTP GET 请求并返回配网页面。
 * @param[in] r HTTP 请求对象。
 * @return `ESP_OK` 表示框架层已完成处理。
 */
esp_err_t get_hyyp_req(httpd_req_t *r)
{
    esp_err_t ret = httpd_resp_send(r, http_html, HTTPD_RESP_USE_STRLEN);

    // 浏览器刷新或主动离开页面时，HTTP 响应可能在发送中途被对端关闭，这里不再把它放大成业务错误。
    if (ret != ESP_OK && ret != ESP_ERR_HTTPD_RESP_SEND)
    {
        ESP_LOGW(TAG, "发送HTTP响应失败: %s", esp_err_to_name(ret));
    }
    return ESP_OK; // 返回OK避免框架打印额外警告
}

/**
 * @brief 处理 `favicon.ico` 请求并返回 204。
 * @param[in] r HTTP 请求对象。
 * @return `ESP_OK` 表示响应已发送。
 */
static esp_err_t favicon_handler(httpd_req_t *r)
{
    httpd_resp_set_status(r, "204 No Content");
    return httpd_resp_send(r, NULL, 0);
}

/**
 * @brief 处理 `/ws` WebSocket 请求。
 * @param[in] r HTTP/WebSocket 请求对象。
 * @return `ESP_OK` 表示处理完成；其他错误表示帧接收失败。
 *
 * @note 当前实现只处理文本帧；首次 `HTTP_GET` 请求为握手阶段，会保存 `socket_fd` 以支持后续主动推送。
 */
esp_err_t handle_ws_req(httpd_req_t *r)
{
    if (r->method == HTTP_GET)
    {
        socket_fd = httpd_req_to_sockfd(r);
        ESP_LOGI(TAG, "WebSocket连接建立, socket_fd=%d", socket_fd);
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    esp_err_t err;

    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    err = httpd_ws_recv_frame(r, &ws_pkt, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "获取帧长度失败: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "WebSocket帧长度: %d字节", ws_pkt.len);

    uint8_t *buf = malloc(ws_pkt.len + 1);
    if (buf == NULL)
    {
        ESP_LOGE(TAG, "内存分配失败");
        return ESP_ERR_NO_MEM;
    }

    ws_pkt.payload = buf;

    err = httpd_ws_recv_frame(r, &ws_pkt, ws_pkt.len);
    if (err == ESP_OK)
    {
        if (ws_pkt.type == HTTPD_WS_TYPE_TEXT)
        {
            buf[ws_pkt.len] = 0;
            ESP_LOGI(TAG, "WebSocket收到: %s", ws_pkt.payload);

            if (ws_server_cb)
            {
                ws_server_cb((const char *)buf, ws_pkt.len);
            }
        }
        else
        {
            ESP_LOGW(TAG, "收到非Text类型的WebSocket帧: %d", ws_pkt.type);
        }
    }
    else
    {
        ESP_LOGE(TAG, "读取帧数据失败: %s", esp_err_to_name(err));
    }

    free(buf);
    return ESP_OK;
}

/**
 * @brief 启动 HTTP + WebSocket 服务器。
 * @param[in] config 服务器配置。
 * @return `ESP_OK` 表示成功；其他错误表示参数非法或 HTTP 服务器启动失败。
 */
esp_err_t ws_server_start(ws_server_config_t *config)
{
    if (config == NULL)
    {
        ESP_LOGE(TAG, "服务器配置为NULL");
        return ESP_FAIL;
    }

    // 若服务器已在运行，则只更新 HTML 内容和消息回调，避免重复占用端口和 handler 资源。
    if (server_handle != NULL)
    {
        ESP_LOGI(TAG, "HTTP服务器已在运行，无需重复启动");
        http_html = config->html_code;
        ws_server_cb = config->cb;
        return ESP_OK;
    }

    http_html = config->html_code;
    ws_server_cb = config->cb;

    httpd_config_t httpd_config = HTTPD_DEFAULT_CONFIG();

    /* 浏览器请求头和 WebSocket handler 都会放大 HTTP server 栈和 handler 占用，
     * 因此这里显式提高 handler 数量和栈大小，避免配网页面交互时触发 431 或栈不足。 */
    httpd_config.uri_match_fn = httpd_uri_match_wildcard;
    httpd_config.max_uri_handlers = 8;
    httpd_config.stack_size = 8192;

    esp_err_t ret = httpd_start(&server_handle, &httpd_config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "启动HTTP服务器失败: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "HTTP服务器启动成功，端口: %d", httpd_config.server_port);

    httpd_uri_t uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = get_hyyp_req,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server_handle, &uri);
    ESP_LOGI(TAG, "注册路由: GET /");

    httpd_uri_t uri_ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = handle_ws_req,
        .user_ctx = NULL,
        .is_websocket = true, // 启用WebSocket处理
    };
    httpd_register_uri_handler(server_handle, &uri_ws);
    ESP_LOGI(TAG, "注册路由: GET /ws (WebSocket)");

    httpd_uri_t uri_favicon = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = favicon_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server_handle, &uri_favicon);
    ESP_LOGI(TAG, "注册路由: GET /favicon.ico");

    return ESP_OK;
}

/**
 * @brief 停止 HTTP + WebSocket 服务器。
 * @return `ESP_OK` 表示成功。
 */
esp_err_t ws_server_stop(void)
{
    if (server_handle)
    {
        ESP_LOGI(TAG, "停止HTTP服务器");
        httpd_stop(server_handle);
        server_handle = NULL;
        socket_fd = -1;
    }
    return ESP_OK;
}

/**
 * @brief 通过 WebSocket 主动发送数据到浏览器。
 * @param[in] data 要发送的数据。
 * @param[in] len 数据长度。
 * @return `ESP_OK` 表示成功；其他错误表示当前未连接或发送失败。
 */
esp_err_t ws_server_send(uint8_t *data, int len)
{
    if (socket_fd < 0)
    {
        ESP_LOGE(TAG, "WebSocket未连接，无法发送");
        return ESP_FAIL;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = data;
    ws_pkt.len = len;
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_send_data(server_handle, socket_fd, &ws_pkt);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "WebSocket发送失败: %s", esp_err_to_name(ret));
    }
    return ret;
}
