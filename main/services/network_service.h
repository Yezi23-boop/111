#ifndef NETWORK_SERVICE_H
#define NETWORK_SERVICE_H

#include <stdbool.h>

#include "esp_err.h"

/*
 * 网络服务层：
 * 1. 对底层 `wifi_provision` 做二次封装，向上层屏蔽 BLE 配网 / AP 门户 / 自动重连细节。
 * 2. 额外增加“云端业务是否真正可用”的探测，不把“仅拿到 IP”误判成“服务已就绪”。
 * 3. 供 UI、聊天服务和业务控制器查询统一网络状态。
 */

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        NETWORK_SERVICE_STATE_OFFLINE = 0,      /* 尚未启动服务任务。 */
        NETWORK_SERVICE_STATE_BLE_PROVISIONING, /* 无凭据或主动请求 BLE 配网。 */
        NETWORK_SERVICE_STATE_CONNECTING,       /* 已有凭据，正在后台连 Wi-Fi。 */
        NETWORK_SERVICE_STATE_WIFI_READY,       /* 已连上 Wi-Fi，但云端依赖尚未全部验证。 */
        NETWORK_SERVICE_STATE_SERVICE_READY,    /* Wi-Fi 与关键业务域名探测均已通过。 */
        NETWORK_SERVICE_STATE_PORTAL_REQUIRED,  /* BLE 不可用或失败后，回退到 AP 门户模式。 */
        NETWORK_SERVICE_STATE_ERROR,            /* 启动底层配网流程失败。 */
    } network_service_state_t;

    /* 状态迁移主路径（当前项目）:
     * OFFLINE -> CONNECTING/BLE_PROVISIONING -> WIFI_READY -> SERVICE_READY
     * 失败或兜底时可转到 PORTAL_REQUIRED。 */

    /* 启动后台网络状态机；重复调用安全。 */
    esp_err_t network_service_start(void);

    /* 获取服务层当前状态，供 UI 或上层业务轮询。 */
    network_service_state_t network_service_get_state(void);

    /* 只有在 Wi-Fi 可用且业务探测通过后才返回 true。 */
    bool network_service_is_service_ready(void);

    /* 获取当前 IPv4 字符串；未连接时返回 `ESP_ERR_INVALID_STATE`。 */
    esp_err_t network_service_get_ip(char *ip_str, size_t ip_str_len);

    /* 主动切换到 AP 门户配网，常用于用户在 UI 中点击“重新配网”。 */
    void network_service_request_portal(void);

    /* 主动切换到 BLE 配网。 */
    void network_service_request_ble(void);

#ifdef __cplusplus
}
#endif

#endif // NETWORK_SERVICE_H
