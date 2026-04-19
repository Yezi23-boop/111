#ifndef AP_PORTAL_ADAPTER_H
#define AP_PORTAL_ADAPTER_H

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动 AP 门户适配层的最小 HTTP 服务器。
 *
 * 当前阶段只提供最小页面与 HTTPD handle 复用接缝，供官方 SoftAP provisioning scheme
 * 在后续启动时复用同一实例，而不是再起第二个 HTTP server。
 *
 * @return `ESP_OK` 表示启动成功；其他错误表示 HTTP server 或路由注册失败。
 */
esp_err_t ap_portal_adapter_start(void);

/**
 * @brief 停止 AP 门户适配层的 HTTP 服务器。
 *
 * 停止前会先把已注册给官方 SoftAP provisioning scheme 的 HTTPD handle 清空，避免后续
 * manager 继续持有悬空服务器句柄。
 *
 * @return `ESP_OK` 表示停止成功或服务器本就未启动；其他错误表示停止失败。
 */
esp_err_t ap_portal_adapter_stop(void);

/**
 * @brief 获取当前 AP 门户 HTTPD 句柄。
 *
 * 该接口用于让上层或调试路径确认当前门户实例是否已创建，以及官方 SoftAP provisioning
 * 将复用哪个 HTTP server。
 *
 * @return 当前 HTTPD 句柄；若门户未启动则返回 `NULL`。
 */
httpd_handle_t ap_portal_adapter_get_httpd_handle(void);

#ifdef __cplusplus
}
#endif

#endif /* AP_PORTAL_ADAPTER_H */
