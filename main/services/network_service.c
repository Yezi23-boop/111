#include "network_service.h"

#include <lwip/netdb.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_manager.h"

/*
 * `network_service` 当前是旧接口兼容层：
 * - 联网控制统一桥接给 `network_manager`
 * - 本文件只额外补“关键云端依赖是否真正可用”的探测
 * - 因此不再直接操作 `wifi_provision` 或底层 transport 生命周期
 */

static const char *TAG = "NETWORK_SERVICE";
static const char *kProbeHosts[] = {"api.tenclass.net", "mqtt.xiaozhi.me"}; // 关键云端依赖域名列表。
static const uint32_t kProbeAttemptMax = 15;                                // 单个域名的最大探测次数。
static const uint32_t kServicePollPeriodMs = 1000U;                         // 服务层轮询 network_manager 的周期。

static TaskHandle_t s_network_task_handle = NULL; // 网络服务后台任务句柄。
static volatile network_service_state_t s_network_state =
    NETWORK_SERVICE_STATE_OFFLINE;
static char s_network_ip[16] = {0}; // 当前缓存的 IPv4 字符串，仅由服务层更新。

static const char *network_service_state_name(network_service_state_t state);
static void network_service_set_state(network_service_state_t state,
                                      const char *reason);
static void network_service_clear_cached_ip(void);
static bool network_service_has_saved_credentials(void);
static void network_service_sync_cached_ip(
    const network_manager_status_t *status);
static network_service_state_t network_service_map_manager_state(
    const network_manager_status_t *status);
static network_service_provision_transport_t
network_service_map_transport_from_manager(
    network_manager_provisioning_transport_t transport);
static network_manager_provisioning_transport_t
network_service_map_transport_to_manager(
    network_service_provision_transport_t transport);
static bool resolve_hostname_once(const char *hostname);
static esp_err_t probe_network_services_ready(void);
static void network_service_task(void *pv_parameter);

/**
 * @brief 清空服务层缓存的 IPv4 地址。
 * @return 无返回值。
 */
static void network_service_clear_cached_ip(void)
{
    s_network_ip[0] = '\0';
}

/**
 * @brief 返回网络服务状态的可读字符串。
 * @param[in] state 目标状态。
 * @return 状态名字符串。
 */
static const char *network_service_state_name(network_service_state_t state)
{
    switch (state)
    {
    case NETWORK_SERVICE_STATE_OFFLINE:
        return "OFFLINE";
    case NETWORK_SERVICE_STATE_BLE_PROVISIONING:
        return "BLE_PROVISIONING";
    case NETWORK_SERVICE_STATE_BLE_DISABLED:
        return "BLE_DISABLED";
    case NETWORK_SERVICE_STATE_CONNECTING:
        return "CONNECTING";
    case NETWORK_SERVICE_STATE_WIFI_READY:
        return "WIFI_READY";
    case NETWORK_SERVICE_STATE_SERVICE_READY:
        return "SERVICE_READY";
    case NETWORK_SERVICE_STATE_PORTAL_REQUIRED:
        return "PORTAL_REQUIRED";
    case NETWORK_SERVICE_STATE_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

/**
 * @brief 更新网络服务状态，并在发生迁移时输出原因日志。
 * @param[in] state 目标状态。
 * @param[in] reason 迁移原因，可为 `NULL`。
 * @return 无返回值。
 */
static void network_service_set_state(network_service_state_t state,
                                      const char *reason)
{
    const network_service_state_t old_state = s_network_state;

    if (old_state != state)
    {
        ESP_LOGI(TAG, "network state: %s -> %s (%s)",
                 network_service_state_name(old_state),
                 network_service_state_name(state),
                 reason != NULL ? reason : "no-reason");
    }

    s_network_state = state;
}

/**
 * @brief 判断当前是否存在 recent Wi-Fi 凭据。
 *
 * `network_manager` 当前已经把 recent Wi-Fi 列表作为“下次自动尝试”的
 * 统一凭据来源，因此兼容层以 recent 列表是否为空，来近似表达
 * “是否存在保存凭据”。
 *
 * @return true 表示至少存在 1 条 recent Wi-Fi 记录。
 */
static bool network_service_has_saved_credentials(void)
{
    size_t count = 0;

    if (network_manager_get_recent_networks(NULL, 0, &count) != ESP_OK)
    {
        return false;
    }

    return count > 0U;
}

/**
 * @brief 从 `network_manager` 状态快照同步缓存 IP。
 *
 * 只有在 Wi-Fi 已连接且 IP 发生变化时才输出日志，避免后台轮询刷屏。
 *
 * @param[in] status 当前 `network_manager` 状态快照。
 * @return 无返回值。
 */
static void network_service_sync_cached_ip(
    const network_manager_status_t *status)
{
    if (status == NULL || !status->wifi_connected || status->ip[0] == '\0')
    {
        network_service_clear_cached_ip();
        return;
    }

    if (strcmp(s_network_ip, status->ip) != 0)
    {
        strncpy(s_network_ip, status->ip, sizeof(s_network_ip) - 1U);
        s_network_ip[sizeof(s_network_ip) - 1U] = '\0';
        ESP_LOGI(TAG, "Wi-Fi connected, IP: %s", s_network_ip);
    }
}

/**
 * @brief 将 `network_manager` 的主状态映射为兼容层状态。
 *
 * @param[in] status 当前 `network_manager` 状态快照。
 * @return 对应的 `network_service` 兼容状态。
 */
static network_service_state_t network_service_map_manager_state(
    const network_manager_status_t *status)
{
    const bool has_credentials = network_service_has_saved_credentials();

    if (status == NULL)
    {
        return NETWORK_SERVICE_STATE_ERROR;
    }

    switch (status->state)
    {
    case NETWORK_MANAGER_STATE_CONNECTING_LATEST:
        return NETWORK_SERVICE_STATE_CONNECTING;
    case NETWORK_MANAGER_STATE_CONNECTED:
        return NETWORK_SERVICE_STATE_WIFI_READY;
    case NETWORK_MANAGER_STATE_PROVISIONING_BLE:
        return NETWORK_SERVICE_STATE_BLE_PROVISIONING;
    case NETWORK_MANAGER_STATE_PROVISIONING_SOFTAP:
        return NETWORK_SERVICE_STATE_PORTAL_REQUIRED;
    case NETWORK_MANAGER_STATE_DISCONNECTED_BY_USER:
        return NETWORK_SERVICE_STATE_OFFLINE;
    case NETWORK_MANAGER_STATE_ERROR:
        return NETWORK_SERVICE_STATE_ERROR;
    case NETWORK_MANAGER_STATE_IDLE:
    default:
        if (!status->ble_enabled && !has_credentials)
        {
            return NETWORK_SERVICE_STATE_BLE_DISABLED;
        }
        return NETWORK_SERVICE_STATE_OFFLINE;
    }
}

/**
 * @brief 将新架构 transport 枚举映射为兼容层枚举。
 * @param[in] transport `network_manager` transport。
 * @return 对应的 `network_service` transport。
 */
static network_service_provision_transport_t
network_service_map_transport_from_manager(
    network_manager_provisioning_transport_t transport)
{
    switch (transport)
    {
    case NETWORK_MANAGER_PROVISIONING_TRANSPORT_SOFTAP:
        return NETWORK_SERVICE_PROVISION_TRANSPORT_AP;
    case NETWORK_MANAGER_PROVISIONING_TRANSPORT_BLE:
    default:
        return NETWORK_SERVICE_PROVISION_TRANSPORT_BLE;
    }
}

/**
 * @brief 将兼容层 transport 枚举映射为新架构 transport。
 *
 * 旧的 `AUTO` 只为兼容保留，当前内部直接退化为 `BLE`，避免继续保留
 * 两套自动选择语义。
 *
 * @param[in] transport `network_service` transport。
 * @return 对应的 `network_manager` transport。
 */
static network_manager_provisioning_transport_t
network_service_map_transport_to_manager(
    network_service_provision_transport_t transport)
{
    switch (transport)
    {
    case NETWORK_SERVICE_PROVISION_TRANSPORT_AP:
        return NETWORK_MANAGER_PROVISIONING_TRANSPORT_SOFTAP;
    case NETWORK_SERVICE_PROVISION_TRANSPORT_AUTO:
        ESP_LOGW(TAG, "AUTO transport is deprecated, fallback to BLE");
        return NETWORK_MANAGER_PROVISIONING_TRANSPORT_BLE;
    case NETWORK_SERVICE_PROVISION_TRANSPORT_BLE:
    default:
        return NETWORK_MANAGER_PROVISIONING_TRANSPORT_BLE;
    }
}

/**
 * @brief 对单个域名执行一次解析探测。
 *
 * 这里继续选择 DNS 解析而不是业务请求，是为了用更轻量的方式同时验证
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

    const int err = getaddrinfo(hostname, "443", &hints, &result);
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
 * 当前策略要求所有关键域名都能成功解析，才把状态提升到
 * `SERVICE_READY`，以避免页面在“仅连上路由器但云端仍不可用”时误判。
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
 * @brief 网络服务后台任务。
 * @param[in] pv_parameter 未使用，保留任务签名。
 * @return 无返回值。
 *
 * 该任务不再推进联网动作，只做两件事：
 * 1. 轮询 `network_manager` 并把状态映射为兼容层语义；
 * 2. 在 Wi-Fi 连通后继续执行关键云端依赖探测。
 */
static void network_service_task(void *pv_parameter)
{
    (void)pv_parameter;

    while (1)
    {
        network_manager_status_t status = {0};
        const esp_err_t ret = network_manager_get_status(&status);

        if (ret != ESP_OK)
        {
            network_service_clear_cached_ip();
            network_service_set_state(NETWORK_SERVICE_STATE_ERROR,
                                      "network manager status unavailable");
            vTaskDelay(pdMS_TO_TICKS(kServicePollPeriodMs));
            continue;
        }

        network_service_sync_cached_ip(&status);

        if (status.wifi_connected)
        {
            if (s_network_state != NETWORK_SERVICE_STATE_SERVICE_READY)
            {
                network_service_set_state(
                    NETWORK_SERVICE_STATE_WIFI_READY,
                    "STA connected, probe cloud dependencies");
                if (probe_network_services_ready() == ESP_OK)
                {
                    network_service_set_state(
                        NETWORK_SERVICE_STATE_SERVICE_READY,
                        "critical hosts resolved");
                }
                else
                {
                    network_service_set_state(
                        NETWORK_SERVICE_STATE_WIFI_READY,
                        "cloud probe still pending");
                }
            }
        }
        else
        {
            network_service_clear_cached_ip();
            network_service_set_state(
                network_service_map_manager_state(&status),
                "mirrored from network manager");
        }

        vTaskDelay(pdMS_TO_TICKS(kServicePollPeriodMs));
    }
}

/**
 * @brief 启动网络服务后台任务。
 *
 * 当前会先确保 `network_manager` 已启动，再创建自己的轻量监控任务。
 *
 * @return `ESP_OK` 表示任务已启动或之前已启动；失败表示无法创建后台任务。
 */
esp_err_t network_service_start(void)
{
    esp_err_t ret = ESP_OK;

    if (s_network_task_handle != NULL)
    {
        return ESP_OK;
    }

    ret = network_manager_start();
    if (ret != ESP_OK)
    {
        network_service_set_state(NETWORK_SERVICE_STATE_ERROR,
                                  "network manager start failed");
        return ret;
    }

    const BaseType_t result =
        xTaskCreatePinnedToCore(network_service_task, "network_service",
                                1024 * 6, NULL, 5, &s_network_task_handle, 0);
    if (result != pdPASS)
    {
        s_network_task_handle = NULL;
        network_service_set_state(NETWORK_SERVICE_STATE_ERROR,
                                  "network service task create failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/**
 * @brief 获取当前网络服务状态。
 * @return 当前兼容层状态。
 */
network_service_state_t network_service_get_state(void)
{
    return s_network_state;
}

/**
 * @brief 设置 BLE 配网总开关。
 * @param[in] enabled 目标开关值。
 * @return `network_manager` 的执行结果。
 */
esp_err_t network_service_set_ble_enabled(bool enabled)
{
    ESP_LOGI(TAG, "bridge BLE enable request to network_manager: enabled=%d",
             enabled ? 1 : 0);
    return network_manager_set_ble_enabled(enabled);
}

/**
 * @brief 查询 BLE 偏好是否开启。
 * @return true 表示当前允许 BLE。
 */
bool network_service_is_ble_enabled(void)
{
    return network_manager_is_ble_enabled();
}

/**
 * @brief 查询 BLE transport 当前是否活动。
 * @return true 表示 BLE transport 当前活跃。
 */
bool network_service_is_ble_active(void)
{
    return network_manager_is_ble_active();
}

/**
 * @brief 查询当前 Wi-Fi 是否已连接。
 * @return true 表示当前已经拿到有效 Wi-Fi 连接。
 */
bool network_service_is_wifi_connected(void)
{
    network_manager_status_t status = {0};

    if (network_manager_get_status(&status) != ESP_OK)
    {
        return false;
    }

    return status.wifi_connected;
}

/**
 * @brief 获取 Wi-Fi 管理页兼容状态快照。
 *
 * 该结构只为旧调用方保留，真实状态来源已经收敛到 `network_manager`。
 *
 * @param[out] status 输出结构。
 * @return `ESP_OK` 表示成功；`ESP_ERR_INVALID_ARG` 表示参数非法。
 */
esp_err_t network_service_get_wifi_status(network_service_wifi_status_t *status)
{
    network_manager_status_t manager_status = {0};

    if (status == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (network_manager_get_status(&manager_status) != ESP_OK)
    {
        return ESP_FAIL;
    }

    memset(status, 0, sizeof(*status));
    status->wifi_connected = manager_status.wifi_connected;
    status->has_credentials = network_service_has_saved_credentials();
    status->user_disconnect_latched =
        (manager_status.state == NETWORK_MANAGER_STATE_DISCONNECTED_BY_USER);
    status->ble_active = manager_status.ble_active;
    status->ap_active =
        (manager_status.state == NETWORK_MANAGER_STATE_PROVISIONING_SOFTAP);
    status->provisioning_active =
        (manager_status.state == NETWORK_MANAGER_STATE_PROVISIONING_BLE) ||
        (manager_status.state == NETWORK_MANAGER_STATE_PROVISIONING_SOFTAP);
    status->default_transport =
        network_service_map_transport_from_manager(
            manager_status.default_transport);

    if (manager_status.ip[0] != '\0')
    {
        strncpy(status->ip, manager_status.ip, sizeof(status->ip) - 1U);
        status->ip[sizeof(status->ip) - 1U] = '\0';
    }

    return ESP_OK;
}

/**
 * @brief 再次使用最近一次成功连接的 Wi-Fi。
 * @return `network_manager` 的执行结果。
 */
esp_err_t network_service_request_connect_with_saved_credentials(void)
{
    ESP_LOGI(TAG, "bridge saved Wi-Fi retry request to network_manager");
    return network_manager_use_latest_wifi();
}

/**
 * @brief 主动断开当前网络连接，并暂停自动重连。
 * @return `network_manager` 的执行结果。
 */
esp_err_t network_service_request_disconnect(void)
{
    ESP_LOGI(TAG, "bridge disconnect request to network_manager");
    return network_manager_disconnect();
}

/**
 * @brief 重新进入当前默认 provisioning transport。
 * @return `network_manager` 的执行结果。
 */
esp_err_t network_service_request_reprovision(void)
{
    ESP_LOGI(TAG, "bridge reprovision request to network_manager");
    return network_manager_reprovision();
}

/**
 * @brief 设置默认配网 transport。
 *
 * 兼容层仍接受旧枚举，但真实持久化与调度已经交给 `network_manager`。
 *
 * @param[in] transport 兼容层 transport 枚举。
 * @return `network_manager` 的执行结果。
 */
esp_err_t network_service_set_default_provision_transport(
    network_service_provision_transport_t transport)
{
    const network_manager_provisioning_transport_t manager_transport =
        network_service_map_transport_to_manager(transport);

    ESP_LOGI(TAG, "bridge transport set request to network_manager: transport=%d",
             (int)transport);
    return network_manager_set_default_transport(manager_transport);
}

/**
 * @brief 获取当前默认配网 transport。
 * @return 兼容层 transport 枚举。
 */
network_service_provision_transport_t
network_service_get_default_provision_transport(void)
{
    return network_service_map_transport_from_manager(
        network_manager_get_default_transport());
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
 *
 * 若缓存为空，会尝试从 `network_manager` 再同步一遍状态，以兼容
 * “调用方先查 IP，再等服务层下一轮轮询”的时序。
 *
 * @param[out] ip_str 输出缓冲区。
 * @param[in] ip_str_len 输出缓冲区长度，单位为字节。
 * @return `ESP_OK` 表示成功复制；
 *         `ESP_ERR_INVALID_ARG` 表示参数非法；
 *         `ESP_ERR_INVALID_STATE` 表示当前尚未拿到有效 IP。
 */
esp_err_t network_service_get_ip(char *ip_str, size_t ip_str_len)
{
    if (ip_str == NULL || ip_str_len == 0U)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_network_ip[0] == '\0')
    {
        network_manager_status_t status = {0};
        if (network_manager_get_status(&status) == ESP_OK)
        {
            network_service_sync_cached_ip(&status);
        }
    }

    if (s_network_ip[0] == '\0')
    {
        return ESP_ERR_INVALID_STATE;
    }

    strncpy(ip_str, s_network_ip, ip_str_len - 1U);
    ip_str[ip_str_len - 1U] = '\0';
    return ESP_OK;
}

/**
 * @brief 主动请求切换到 SoftAP 配网。
 *
 * 兼容层直接桥接到 `network_manager` 的显式 SoftAP 入口，避免把
 * “切默认 transport + reprovision”的两步旧 UI 语义继续扩散。
 *
 * @return 无返回值。
 */
void network_service_request_portal(void)
{
    esp_err_t ret = network_manager_start_softap_provisioning();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "explicit SoftAP provisioning failed: %s",
                 esp_err_to_name(ret));
    }
}

/**
 * @brief 主动请求切换到 BLE 配网。
 *
 * 兼容层只桥接到显式 BLE 配网入口；是否允许 BLE 由主界面蓝牙总开关控制，
 * 本函数不会偷偷打开蓝牙，避免把“蓝牙开关”和“小程序配网”再次耦合。
 *
 * @return 无返回值。
 */
void network_service_request_ble(void)
{
    esp_err_t ret = network_manager_start_ble_provisioning();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "explicit BLE provisioning failed: %s",
                 esp_err_to_name(ret));
    }
}
