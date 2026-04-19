/**
 * @file ap_portal_routes.c
 * @brief AP 门户适配层的 HTTP 路由与静态资源分发。
 */

#include "ap_portal_routes.h"

#include <string.h>

#include "esp_http_server.h"

/** @brief 内嵌 `index.html` 资源起始地址。 */
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
/** @brief 内嵌 `app.js` 资源起始地址。 */
extern const uint8_t app_js_start[] asm("_binary_app_js_start");
/** @brief 内嵌 `app.css` 资源起始地址。 */
extern const uint8_t app_css_start[] asm("_binary_app_css_start");

/**
 * @brief 发送内嵌的文本静态资源。
 *
 * 这里统一用于发送 `html/css/javascript/json` 文本，避免每个 handler 都重复写响应头和
 * 文本发送逻辑。
 *
 * @param[in] req HTTP 请求对象。
 * @param[in] content_type 返回的 `Content-Type`。
 * @param[in] payload 要发送的文本内容。
 * @return `ESP_OK` 表示响应已提交；其他错误表示 HTTP 框架发送失败。
 */
static esp_err_t ap_portal_send_text(httpd_req_t *req, const char *content_type,
                                     const char *payload)
{
    if (req == NULL || content_type == NULL || payload == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    httpd_resp_set_type(req, content_type);
    return httpd_resp_send(req, payload, HTTPD_RESP_USE_STRLEN);
}

/**
 * @brief 返回门户首页 HTML。
 *
 * 当前 `index.html` 已从旧 `apcfg.html` 拆到独立资源目录，后续页面样式和脚本可独立演进，
 * 不再把整份前端混在单个 C 字符串里维护。
 *
 * @param[in] req HTTP 请求对象。
 * @return `ESP_OK` 表示响应已提交。
 */
static esp_err_t ap_portal_root_get_handler(httpd_req_t *req)
{
    return ap_portal_send_text(req, "text/html; charset=utf-8",
                               (const char *)index_html_start);
}

/**
 * @brief 返回门户前端脚本资源。
 *
 * 页面改为通过独立脚本文件承接浏览器交互逻辑，后续接设备侧 API 时无需再改 C 字符串。
 *
 * @param[in] req HTTP 请求对象。
 * @return `ESP_OK` 表示响应已提交。
 */
static esp_err_t ap_portal_app_js_handler(httpd_req_t *req)
{
    return ap_portal_send_text(req, "application/javascript; charset=utf-8",
                               (const char *)app_js_start);
}

/**
 * @brief 返回门户样式资源。
 *
 * @param[in] req HTTP 请求对象。
 * @return `ESP_OK` 表示响应已提交。
 */
static esp_err_t ap_portal_app_css_handler(httpd_req_t *req)
{
    return ap_portal_send_text(req, "text/css; charset=utf-8",
                               (const char *)app_css_start);
}

/**
 * @brief 返回最小门户状态接口。
 *
 * 这是 `ap_portal_adapter` 第一版显式暴露给页面的 HTTP API 接缝。当前先只提供门户与设备
 * 侧的占位状态，后续扫描和配置接口会在此基础上继续扩展。
 *
 * @param[in] req HTTP 请求对象。
 * @return `ESP_OK` 表示响应已提交。
 */
static esp_err_t ap_portal_status_handler(httpd_req_t *req)
{
    static const char *kStatusJson =
        "{"
        "\"ok\":true,"
        "\"portal\":\"ap_portal_adapter\","
        "\"api_version\":\"v1\","
        "\"scan_supported\":false,"
        "\"configure_supported\":false"
        "}";

    return ap_portal_send_text(req, "application/json; charset=utf-8",
                               kStatusJson);
}

/**
 * @brief 处理尚未接入的 API 请求。
 *
 * 在扫描和提交凭据接口未真正落地前，这里统一返回 `501` JSON，既能让前端明确知道当前
 * 能力边界，也能避免继续复用旧 WebSocket 协议作为长期主线。
 *
 * @param[in] req HTTP 请求对象。
 * @return `ESP_OK` 表示响应已提交。
 */
static esp_err_t ap_portal_not_ready_handler(httpd_req_t *req)
{
    static const char *kNotReadyJson =
        "{"
        "\"ok\":false,"
        "\"error\":\"not_ready\","
        "\"message\":\"Device-side API is not connected yet.\""
        "}";

    httpd_resp_set_status(req, "501 Not Implemented");
    return ap_portal_send_text(req, "application/json; charset=utf-8",
                               kNotReadyJson);
}

/**
 * @brief 处理 `favicon.ico` 请求并返回空响应。
 *
 * 浏览器默认会请求 `favicon.ico`，这里显式返回 `204`，避免调试阶段被多余 404 日志污染。
 *
 * @param[in] req HTTP 请求对象。
 * @return `ESP_OK` 表示响应已提交。
 */
static esp_err_t ap_portal_favicon_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

/**
 * @brief 向给定 HTTPD 注册 AP 门户路由。
 *
 * 当前会注册：
 * - 静态页面 `/`
 * - 前端资源 `/app.js` 与 `/app.css`
 * - 最小状态接口 `/api/status`
 * - 预留接口 `/api/scan` 与 `/api/configure`
 *
 * @param[in] server 已启动的 HTTPD 句柄。
 * @return `ESP_OK` 表示注册成功；其他错误表示参数非法或路由注册失败。
 */
esp_err_t ap_portal_routes_register(httpd_handle_t server)
{
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = ap_portal_root_get_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t js_uri = {
        .uri = "/app.js",
        .method = HTTP_GET,
        .handler = ap_portal_app_js_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t css_uri = {
        .uri = "/app.css",
        .method = HTTP_GET,
        .handler = ap_portal_app_css_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = ap_portal_status_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t scan_uri = {
        .uri = "/api/scan",
        .method = HTTP_POST,
        .handler = ap_portal_not_ready_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t configure_uri = {
        .uri = "/api/configure",
        .method = HTTP_POST,
        .handler = ap_portal_not_ready_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t favicon_uri = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = ap_portal_favicon_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t *uris[] = {
        &root_uri,
        &js_uri,
        &css_uri,
        &status_uri,
        &scan_uri,
        &configure_uri,
        &favicon_uri,
    };
    esp_err_t ret = ESP_OK;
    size_t index = 0;

    if (server == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    for (index = 0; index < (sizeof(uris) / sizeof(uris[0])); ++index)
    {
        ret = httpd_register_uri_handler(server, uris[index]);
        if (ret != ESP_OK)
        {
            return ret;
        }
    }

    return ESP_OK;
}
