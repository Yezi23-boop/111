#include "network_service.h"

#include <lwip/netdb.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_provision.h"

/*
 * 网络服务状态机说明：
 * - 本模块只关心“设备是否可联网并访问关键云端依赖”，不直接处理业务协议；
 * - 先通过 `wifi_provision` 管理 BLE/AP/自动重连，再通过 DNS 探测关键主机，
 *   防止仅仅拿到局域网 IP 就让上层误判为“聊天服务可用”；
 * - 上层只需依赖本模块暴露的统一状态，而不必理解配网细节。
 */

static const char *TAG = "NETWORK_SERVICE";
static const char *kProbeHosts[] = {"api.tenclass.net", "mqtt.xiaozhi.me"}; // 关键云端依赖域名列表。
static const uint32_t kProbeAttemptMax = 15;                                // 单个域名的最大探测次数。

static TaskHandle_t s_network_task_handle = NULL; // 后台网络状态机任务句柄。
static volatile network_service_state_t s_network_state =
    NETWORK_SERVICE_STATE_OFFLINE;
static char s_network_ip[16] = {0};     // 当前 STA IPv4 字符串缓存，仅服务任务更新。
static bool s_portal_requested = false; // 是否已被上层显式请求 AP 门户。

/**
 * @brief 统一进入 AP 门户兜底状态。
 *
 * AP 门户是 BLE 配网不可用或失败时的兜底路径。
 * 通过单独封装该逻辑，可以避免多个错误分支散落 AP 启动和状态赋值代码。
 *
 * @return 无返回值。
 */
static void network_service_enter_portal_required_state(void)
{
    esp_err_t ret = wifi_provision_start_apcfg();

    s_network_ip[0] = '\0';
    if (ret == ESP_OK)
    {
        s_portal_requested = true;
        s_network_state = NETWORK_SERVICE_STATE_PORTAL_REQUIRED;
    }
    else
    {
        ESP_LOGE(TAG, "start AP fallback failed: %s", esp_err_to_name(ret));
        s_portal_requested = false;
        s_network_state = NETWORK_SERVICE_STATE_ERROR;
    }
}

/**
 * @brief 对单个域名执行一次解析探测。
 *
 * 这里选择 DNS 解析而不是直接发业务请求，是为了用更轻量的方式同时验证
 * DNS 基本可用性和外网访问链路是否已经打通。
 *
 * @param[in] hostname 待探测域名。
 * @return true 表示本次探测成功。
 */
static bool resolve_hostname_once(const char *hostname)
{
    struct addrinfo hints = {0};
    struct addrinfo *result = NULL;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    // 使用域名解析做轻量探测，既能验证 DNS，也能验证外网访问链路基本可用。
    int err = getaddrinfo(hostname, "443", &hints, &result);
    if (err == 0 && result != NULL)
    {
        freeaddrinfo(result);
        ESP_LOGI(TAG, "network service ready: %s", hostname);
        return true;
    }

    if (result != NULL)
    {
        freeaddrinfo(result);
    }

    ESP_LOGW(TAG, "network service not ready yet: host=%s err=%d", hostname,
             err);
    return false;
}

/**
 * @brief 探测关键业务依赖是否已就绪。
 *
 * 当前策略要求所有关键域名都能成功解析，才把状态提升到 `SERVICE_READY`，
 * 以避免页面在“仅连上路由器但云端仍不可用”时误判。
 *
 * @return `ESP_OK` 表示所有关键依赖都已就绪；其他错误表示探测超时或失败。
 */
static esp_err_t probe_network_services_ready(void)
{
    for (size_t host_index = 0;
         host_index < (sizeof(kProbeHosts) / sizeof(kProbeHosts[0]));
         ++host_index)
    {
        const char *hostname = kProbeHosts[host_index];
        for (uint32_t attempt = 1; attempt <= kProbeAttemptMax; ++attempt)
        {
            if (resolve_hostname_once(hostname))
            {
                break;
            }

            if (attempt == kProbeAttemptMax)
            {
                ESP_LOGW(TAG,
                         "network service probe timed out: host=%s attempts=%u",
                         hostname, (unsigned)attempt);
                return ESP_ERR_TIMEOUT;
            }

            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    return ESP_OK;
}

/**
 * @brief 从底层配网模块同步最新 IP 地址。
 *
 * 只有在 IP 变化时才打印日志，避免后台轮询造成刷屏。
 *
 * @return 无返回值。
 */
static void refresh_connected_ip(void)
{
    char ip[16] = {0}; // `wifi_provision` 返回的最新 IP 字符串。
    if (wifi_provision_get_ip(ip, sizeof(ip)) == ESP_OK)
    {
        if (strcmp(s_network_ip, ip) != 0)
        {
            strncpy(s_network_ip, ip, sizeof(s_network_ip) - 1);
            s_network_ip[sizeof(s_network_ip) - 1] = '\0';
            ESP_LOGI(TAG, "Wi-Fi connected, IP: %s", s_network_ip);
        }
    }
    else
    {
        s_network_ip[0] = '\0';
    }
}

/**
 * @brief 网络服务后台任务。
 * @param[in] pv_parameter 未使用，保留任务签名。
 * @return 无返回值。
 *
 * 该任务持续协调三条路径：
 * 1. 有凭据时尝试自动联网；
 * 2. 无凭据时拉起 BLE 配网；
 * 3. BLE 不可用或失败时回退到 AP 门户。
 *
 * @note 该任务是网络状态机唯一的主动推进者；上层接口只改变意图，不直接管理连接流程。
 */
static void network_service_task(void *pv_parameter)
{
    (void)pv_parameter;

    esp_err_t ret = ESP_OK;

    // 首次启动时优先尝试已有凭据自动联网，否则进入 BLE 配网。
    if (wifi_provision_has_credentials())
    {
        s_network_state = NETWORK_SERVICE_STATE_CONNECTING;
        ret = wifi_provision_start_auto();
    }
    else
    {
        s_network_state = NETWORK_SERVICE_STATE_BLE_PROVISIONING;
        ret = wifi_provision_start_blecfg();
    }

    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "initial BLE provisioning start failed, fallback to AP: %s",
                 esp_err_to_name(ret));
        network_service_enter_portal_required_state();
    }

    while (1)
    {
        if (wifi_provision_is_connected())
        {
            s_portal_requested = false;
            refresh_connected_ip();

            if (s_network_state != NETWORK_SERVICE_STATE_SERVICE_READY)
            {
                // 拿到 Wi-Fi 后仍需确认关键业务依赖可达。
                s_network_state = NETWORK_SERVICE_STATE_WIFI_READY;
                if (probe_network_services_ready() == ESP_OK)
                {
                    s_network_state = NETWORK_SERVICE_STATE_SERVICE_READY;
                }
                else
                {
                    s_network_state = NETWORK_SERVICE_STATE_WIFI_READY;
                }
            }
        }
        else if (wifi_provision_has_credentials())
        {
            s_portal_requested = false;
            if (s_network_state != NETWORK_SERVICE_STATE_CONNECTING)
            {
                ESP_LOGI(TAG, "Wi-Fi not connected yet, waiting in background");
            }
            s_network_ip[0] = '\0';
            s_network_state = NETWORK_SERVICE_STATE_CONNECTING;
        }
        else if (s_portal_requested || wifi_provision_is_ap_active())
        {
            // 一旦已经切到 AP 门户，就保持该语义，直到上层再次显式切换模式。
            s_portal_requested = true;
            s_network_ip[0] = '\0';
            s_network_state = NETWORK_SERVICE_STATE_PORTAL_REQUIRED;
        }
        else if (wifi_provision_is_ble_active())
        {
            s_network_ip[0] = '\0';
            s_network_state = NETWORK_SERVICE_STATE_BLE_PROVISIONING;
        }
        else
        {
            // BLE 既未运行又没有凭据时，自动重新拉起配网。
            ret = wifi_provision_start_blecfg();
            s_network_ip[0] = '\0';
            if (ret == ESP_OK)
            {
                s_network_state = NETWORK_SERVICE_STATE_BLE_PROVISIONING;
            }
            else
            {
                ESP_LOGW(TAG, "restart BLE provisioning failed, fallback to AP: %s",
                         esp_err_to_name(ret));
                network_service_enter_portal_required_state();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief 启动网络服务后台任务。
 * @return `ESP_OK` 表示任务已启动或之前已启动；失败表示无法创建后台状态机任务。
 */
esp_err_t network_service_start(void)
{
    if (s_network_task_handle != NULL)
    {
        return ESP_OK;
    }

    // 固定在 core0，和本项目其他系统服务保持一致，避免和 UI 主线程争抢。
    BaseType_t result =
        xTaskCreatePinnedToCore(network_service_task, "network_service",
                                1024 * 6, NULL, 5, &s_network_task_handle, 0);
    if (result != pdPASS)
    {
        s_network_task_handle = NULL;
        s_network_state = NETWORK_SERVICE_STATE_ERROR;
        return ESP_FAIL;
    }

    return ESP_OK;
}

/**
 * @brief 获取当前网络服务状态。
 * @return 统一的服务层状态枚举。
 */
network_service_state_t network_service_get_state(void)
{
    return s_network_state;
}

/**
 * @brief 判断云端业务依赖是否已经可用。
 * @return true 表示 Wi-Fi 已连通且关键域名探测通过。
 */
bool network_service_is_service_ready(void)
{
    return s_network_state == NETWORK_SERVICE_STATE_SERVICE_READY;
}

/**
 * @brief 获取当前缓存的 IPv4 地址字符串。
 * @param[out] ip_str 输出缓冲区。
 * @param[in] ip_str_len 输出缓冲区长度，单位为字节。
 * @return `ESP_OK` 表示成功复制；
 *         `ESP_ERR_INVALID_ARG` 表示参数非法；
 *         `ESP_ERR_INVALID_STATE` 表示当前尚未拿到有效 IP。
 */
esp_err_t network_service_get_ip(char *ip_str, size_t ip_str_len)
{
    if (ip_str == NULL || ip_str_len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_network_ip[0] == '\0')
    {
        return ESP_ERR_INVALID_STATE;
    }

    strncpy(ip_str, s_network_ip, ip_str_len - 1);
    ip_str[ip_str_len - 1] = '\0';
    return ESP_OK;
}

/**
 * @brief 主动请求切换到 AP 配网门户。
 *
 * 适合 UI 中“重新配网”或 BLE 不适合当前场景的交互入口。
 *
 * @return 无返回值。
 */
void network_service_request_portal(void)
{
    network_service_enter_portal_required_state();
}

/**
 * @brief 主动请求切换到 BLE 配网。
 *
 * 该接口不会阻塞等待成功，只负责触发底层配网模式并更新服务层语义。
 *
 * @return 无返回值。
 */
void network_service_request_ble(void)
{
    esp_err_t ret = wifi_provision_start_blecfg();

    s_portal_requested = false;
    s_network_ip[0] = '\0';
    if (ret == ESP_OK)
    {
        s_network_state = NETWORK_SERVICE_STATE_BLE_PROVISIONING;
    }
    else
    {
        ESP_LOGW(TAG, "explicit BLE provisioning start failed: %s",
                 esp_err_to_name(ret));
    }
}
