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
/** @brief 内嵌 `prov_client.js` 资源起始地址。 */
extern const uint8_t prov_client_js_start[] asm("_binary_prov_client_js_start");
/** @brief 内嵌 `prov_proto_bundle.js` 资源起始地址。 */
extern const uint8_t prov_proto_bundle_js_start[] asm("_binary_prov_proto_bundle_js_start");

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
 * @brief 返回 provisioning client 模块脚本。
 *
 * 浏览器入口 `app.js` 会通过 ES module 直接导入该文件；如果这里不显式注册 GET 路由，
 * SoftAP 门户虽然能打开首页，但模块导入会在浏览器端直接 `404`，导致官方 provisioning
 * client 根本无法初始化。
 *
 * @param[in] req HTTP 请求对象。
 * @return `ESP_OK` 表示响应已提交。
 */
static esp_err_t ap_portal_prov_client_js_handler(httpd_req_t *req)
{
    return ap_portal_send_text(req, "application/javascript; charset=utf-8",
                               (const char *)prov_client_js_start);
}

/**
 * @brief 返回最小 protobuf 编解码模块脚本。
 *
 * 该脚本由 `prov_client.js` 继续导入；把协议层拆到独立模块后，路由层也必须同步暴露对应
 * 的静态资源，否则浏览器会在第二层 import 处失败。
 *
 * @param[in] req HTTP 请求对象。
 * @return `ESP_OK` 表示响应已提交。
 */
static esp_err_t ap_portal_prov_proto_bundle_js_handler(httpd_req_t *req)
{
    return ap_portal_send_text(req, "application/javascript; charset=utf-8",
                               (const char *)prov_proto_bundle_js_start);
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
 * @brief 返回旧 JSON 接口的兼容提示。
 *
 * 当前门户正式主链路已经迁到浏览器端官方 provisioning client，因此旧 `/api/scan`
 * 与 `/api/configure` 只保留兼容提示，不再承载真实配网逻辑。这里使用 `410 Gone`
 * 明确告诉调用方：该 JSON 接口已经退役，而不是暂时未实现。
 *
 * @param[in] req HTTP 请求对象。
 * @return `ESP_OK` 表示响应已提交。
 */
static esp_err_t ap_portal_legacy_api_handler(httpd_req_t *req)
{
    /* 使用稳定英文错误码与提示文案，便于旧调用方和源码测试统一识别接口已迁移事实。 */
    static const char *kLegacyJson =
        "{"
        "\"ok\":false,"
        "\"error\":\"legacy_api_removed\","
        "\"message\":\"Legacy JSON API has been replaced by official provisioning client.\""
        "}";

    httpd_resp_set_status(req, "410 Gone");
    return ap_portal_send_text(req, "application/json; charset=utf-8",
                               kLegacyJson);
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
 * - 前端资源 `/app.js`、`/app.css`、`/prov_client.js` 与 `/prov_proto_bundle.js`
 * - 最小状态接口 `/api/status`
 * - 兼容提示接口 `/api/scan` 与 `/api/configure`
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
    httpd_uri_t prov_client_uri = {
        .uri = "/prov_client.js",
        .method = HTTP_GET,
        .handler = ap_portal_prov_client_js_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t prov_proto_bundle_uri = {
        .uri = "/prov_proto_bundle.js",
        .method = HTTP_GET,
        .handler = ap_portal_prov_proto_bundle_js_handler,
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
        .handler = ap_portal_legacy_api_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t configure_uri = {
        .uri = "/api/configure",
        .method = HTTP_POST,
        .handler = ap_portal_legacy_api_handler,
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
        &prov_client_uri,
        &prov_proto_bundle_uri,
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
