#include "network_service.h"

#include <lwip/netdb.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
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
static const char *kBlePrefNamespace = "network_svc";                       // BLE 偏好单独存入网络服务命名空间。
static const char *kBlePrefKey = "ble_enabled";                             // BLE 配网入口持久化键。
static const char *kTransportPrefKey = "prov_transport";                    // 默认配网方式持久化键。

static TaskHandle_t s_network_task_handle = NULL; // 后台网络状态机任务句柄。
static volatile network_service_state_t s_network_state =
    NETWORK_SERVICE_STATE_OFFLINE;
static char s_network_ip[16] = {0};     // 当前 STA IPv4 字符串缓存，仅服务任务更新。
static bool s_portal_requested = false; // 是否已被上层显式请求 AP 门户。
static bool s_ble_enabled = true;       // BLE 配网入口偏好，默认开启以兼容当前项目行为。
static bool s_ble_pref_loaded = false;  // 是否已从 NVS 读取过 BLE 偏好。
static bool s_transport_pref_loaded = false; // 是否已从 NVS 读取过默认 transport。
static bool s_user_disconnect_latched = false; // 是否为用户主动断开。
static bool s_reprovision_requested = false;   // 是否已主动请求重新配网。
static network_service_provision_transport_t s_default_transport =
    NETWORK_SERVICE_PROVISION_TRANSPORT_AUTO;

static const char *network_service_state_name(network_service_state_t state);
static void network_service_set_state(network_service_state_t state,
                                      const char *reason);
static esp_err_t network_service_load_transport_pref(void);
static esp_err_t network_service_store_transport_pref(
    network_service_provision_transport_t transport);
static esp_err_t network_service_start_selected_provision_transport(void);
static void network_service_update_state_for_active_transport(void);

/**
 * @brief 清空网络服务层缓存的 IP。
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
 * @param[in] reason 迁移原因，可为 NULL。
 * @return 无返回值。
 */
static void network_service_set_state(network_service_state_t state,
                                      const char *reason)
{
    network_service_state_t old_state = s_network_state;

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
 * @brief 从 NVS 加载 BLE 配网偏好。
 * @return `ESP_OK` 表示已加载或使用默认值继续运行。
 */
static esp_err_t network_service_load_ble_pref(void)
{
    nvs_handle_t nvs_handle = 0;
    uint8_t ble_enabled_raw = 1;
    esp_err_t ret = ESP_OK;

    if (s_ble_pref_loaded)
    {
        return ESP_OK;
    }

    ret = nvs_open(kBlePrefNamespace, NVS_READONLY, &nvs_handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        s_ble_enabled = true;
        s_ble_pref_loaded = true;
        ESP_LOGI(TAG, "BLE preference loaded: enabled=%d source=default", 1);
        return ESP_OK;
    }
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "load BLE preference namespace failed, use default: %s",
                 esp_err_to_name(ret));
        s_ble_enabled = true;
        s_ble_pref_loaded = true;
        ESP_LOGI(TAG, "BLE preference loaded: enabled=%d source=fallback", 1);
        return ret;
    }

    ret = nvs_get_u8(nvs_handle, kBlePrefKey, &ble_enabled_raw);
    if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        s_ble_enabled = true;
        ret = ESP_OK;
    }
    else if (ret == ESP_OK)
    {
        s_ble_enabled = (ble_enabled_raw != 0U);
    }
    else
    {
        ESP_LOGW(TAG, "load BLE preference failed, use default: %s",
                 esp_err_to_name(ret));
        s_ble_enabled = true;
    }

    nvs_close(nvs_handle);
    s_ble_pref_loaded = true;
    ESP_LOGI(TAG, "BLE preference loaded: enabled=%d", s_ble_enabled ? 1 : 0);
    return ret;
}

/**
 * @brief 将 BLE 配网偏好写入 NVS。
 * @param[in] enabled 目标偏好。
 * @return `ESP_OK` 表示写入成功。
 */
static esp_err_t network_service_store_ble_pref(bool enabled)
{
    nvs_handle_t nvs_handle = 0;
    esp_err_t ret = nvs_open(kBlePrefNamespace, NVS_READWRITE, &nvs_handle);

    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "open BLE preference namespace failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_u8(nvs_handle, kBlePrefKey, enabled ? 1U : 0U);
    if (ret == ESP_OK)
    {
        ret = nvs_commit(nvs_handle);
    }
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "store BLE preference failed: %s", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "BLE preference stored: enabled=%d", enabled ? 1 : 0);
    }

    nvs_close(nvs_handle);
    return ret;
}

/**
 * @brief 从 NVS 加载默认配网 transport。
 * @return `ESP_OK` 表示成功或已回退到默认值。
 */
static esp_err_t network_service_load_transport_pref(void)
{
    nvs_handle_t nvs_handle = 0;
    uint8_t transport_raw = (uint8_t)NETWORK_SERVICE_PROVISION_TRANSPORT_AUTO;
    esp_err_t ret = ESP_OK;

    if (s_transport_pref_loaded)
    {
        return ESP_OK;
    }

    ret = nvs_open(kBlePrefNamespace, NVS_READONLY, &nvs_handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        s_default_transport = NETWORK_SERVICE_PROVISION_TRANSPORT_AUTO;
        s_transport_pref_loaded = true;
        return ESP_OK;
    }
    if (ret != ESP_OK)
    {
        s_default_transport = NETWORK_SERVICE_PROVISION_TRANSPORT_AUTO;
        s_transport_pref_loaded = true;
        return ret;
    }

    ret = nvs_get_u8(nvs_handle, kTransportPrefKey, &transport_raw);
    if (ret == ESP_OK &&
        transport_raw <= (uint8_t)NETWORK_SERVICE_PROVISION_TRANSPORT_AP)
    {
        s_default_transport =
            (network_service_provision_transport_t)transport_raw;
    }
    else
    {
        s_default_transport = NETWORK_SERVICE_PROVISION_TRANSPORT_AUTO;
        ret = ESP_OK;
    }

    nvs_close(nvs_handle);
    s_transport_pref_loaded = true;
    ESP_LOGI(TAG, "default provision transport loaded: %d",
             (int)s_default_transport);
    return ret;
}

/**
 * @brief 保存默认配网 transport。
 * @param[in] transport 目标 transport。
 * @return `ESP_OK` 表示写入成功。
 */
static esp_err_t network_service_store_transport_pref(
    network_service_provision_transport_t transport)
{
    nvs_handle_t nvs_handle = 0;
    esp_err_t ret = nvs_open(kBlePrefNamespace, NVS_READWRITE, &nvs_handle);

    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = nvs_set_u8(nvs_handle, kTransportPrefKey, (uint8_t)transport);
    if (ret == ESP_OK)
    {
        ret = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);
    return ret;
}

/**
 * @brief 按当前默认 transport 启动重新配网。
 * @return 底层启动结果。
 */
static esp_err_t network_service_start_selected_provision_transport(void)
{
    esp_err_t ret = ESP_OK;

    switch (s_default_transport)
    {
    case NETWORK_SERVICE_PROVISION_TRANSPORT_AP:
        return wifi_provision_start_apcfg();
    case NETWORK_SERVICE_PROVISION_TRANSPORT_BLE:
        return wifi_provision_start_blecfg();
    case NETWORK_SERVICE_PROVISION_TRANSPORT_AUTO:
    default:
        ret = wifi_provision_start_blecfg();
        if (ret == ESP_OK)
        {
            return ESP_OK;
        }

        ESP_LOGW(TAG,
                 "AUTO provisioning fell back to AP after BLE start failure: %s",
                 esp_err_to_name(ret));
        return wifi_provision_start_apcfg();
    }
}

/**
 * @brief 按当前活动 transport 刷新状态。
 * @return 无返回值。
 */
static void network_service_update_state_for_active_transport(void)
{
    network_service_clear_cached_ip();
    if (wifi_provision_is_ap_active())
    {
        s_portal_requested = true;
        network_service_set_state(NETWORK_SERVICE_STATE_PORTAL_REQUIRED,
                                  "AP transport active");
        return;
    }

    if (wifi_provision_is_ble_active())
    {
        network_service_set_state(NETWORK_SERVICE_STATE_BLE_PROVISIONING,
                                  "BLE transport active");
        return;
    }

    network_service_set_state(NETWORK_SERVICE_STATE_OFFLINE,
                              "provisioning transport not active yet");
}

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

    network_service_clear_cached_ip();
    if (ret == ESP_OK)
    {
        s_portal_requested = true;
        network_service_set_state(NETWORK_SERVICE_STATE_PORTAL_REQUIRED,
                                  "AP fallback active");
    }
    else
    {
        ESP_LOGE(TAG, "start AP fallback failed: %s", esp_err_to_name(ret));
        s_portal_requested = false;
        network_service_set_state(NETWORK_SERVICE_STATE_ERROR,
                                  "AP fallback start failed");
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
        network_service_clear_cached_ip();
    }
}

/**
 * @brief 根据当前 BLE 偏好决定无凭据时的启动路径。
 * @return 启动结果。
 */
static esp_err_t network_service_start_provisioning_if_allowed(void)
{
    network_service_clear_cached_ip();
    if (!s_ble_enabled)
    {
        ESP_LOGI(TAG,
                 "skip BLE provisioning bootstrap because BLE preference is disabled");
        network_service_set_state(NETWORK_SERVICE_STATE_BLE_DISABLED,
                                  "BLE preference disabled");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "start BLE provisioning bootstrap: ble_enabled=%d",
             s_ble_enabled ? 1 : 0);
    network_service_set_state(NETWORK_SERVICE_STATE_BLE_PROVISIONING,
                              "start BLE provisioning");
    return wifi_provision_start_blecfg();
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

    (void)network_service_load_ble_pref();
    (void)network_service_load_transport_pref();

    // 首次启动时优先尝试已有凭据自动联网，否则进入 BLE 配网。
    if (wifi_provision_has_credentials())
    {
        ESP_LOGI(TAG, "initial bootstrap: has_credentials=1 ble_enabled=%d",
                 s_ble_enabled ? 1 : 0);
        network_service_set_state(NETWORK_SERVICE_STATE_CONNECTING,
                                  "saved Wi-Fi credentials available");
        ret = wifi_provision_start_auto();
    }
    else
    {
        ESP_LOGI(TAG, "initial bootstrap: has_credentials=0 ble_enabled=%d",
                 s_ble_enabled ? 1 : 0);
        ret = network_service_start_provisioning_if_allowed();
    }

    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "initial network bootstrap failed, fallback to AP: %s",
                 esp_err_to_name(ret));
        network_service_enter_portal_required_state();
    }

    while (1)
    {
        if (wifi_provision_is_connected())
        {
            s_portal_requested = false;
            s_reprovision_requested = false;
            s_user_disconnect_latched = false;
            refresh_connected_ip();

            if (s_network_state != NETWORK_SERVICE_STATE_SERVICE_READY)
            {
                // 拿到 Wi-Fi 后仍需确认关键业务依赖可达。
                network_service_set_state(NETWORK_SERVICE_STATE_WIFI_READY,
                                          "STA connected, probe cloud dependencies");
                if (probe_network_services_ready() == ESP_OK)
                {
                    network_service_set_state(NETWORK_SERVICE_STATE_SERVICE_READY,
                                              "critical hosts resolved");
                }
                else
                {
                    network_service_set_state(NETWORK_SERVICE_STATE_WIFI_READY,
                                              "cloud probe still pending");
                }
            }
        }
        else if (s_reprovision_requested)
        {
            if (wifi_provision_is_ap_active() || wifi_provision_is_ble_active())
            {
                network_service_update_state_for_active_transport();
            }
            else
            {
                ret = network_service_start_selected_provision_transport();
                if (ret == ESP_OK)
                {
                    network_service_update_state_for_active_transport();
                }
                else
                {
                    ESP_LOGW(TAG,
                             "restart selected provisioning failed: %s",
                             esp_err_to_name(ret));
                    network_service_set_state(NETWORK_SERVICE_STATE_ERROR,
                                              "restart provisioning failed");
                }
            }
        }
        else if (s_user_disconnect_latched)
        {
            network_service_clear_cached_ip();
            network_service_set_state(NETWORK_SERVICE_STATE_OFFLINE,
                                      "user disconnected");
        }
        else if (wifi_provision_has_credentials())
        {
            s_portal_requested = false;
            if (s_network_state != NETWORK_SERVICE_STATE_CONNECTING)
            {
                ESP_LOGI(TAG, "Wi-Fi not connected yet, waiting in background");
            }
            network_service_clear_cached_ip();
            network_service_set_state(NETWORK_SERVICE_STATE_CONNECTING,
                                      "waiting for STA reconnect");
        }
        else if (s_portal_requested || wifi_provision_is_ap_active())
        {
            // 一旦已经切到 AP 门户，就保持该语义，直到上层再次显式切换模式。
            s_portal_requested = true;
            network_service_clear_cached_ip();
            network_service_set_state(NETWORK_SERVICE_STATE_PORTAL_REQUIRED,
                                      "portal requested or AP still active");
        }
        else if (wifi_provision_is_ble_active())
        {
            network_service_clear_cached_ip();
            network_service_set_state(NETWORK_SERVICE_STATE_BLE_PROVISIONING,
                                      "BLE transport active");
        }
        else if (!s_ble_enabled)
        {
            network_service_clear_cached_ip();
            network_service_set_state(NETWORK_SERVICE_STATE_BLE_DISABLED,
                                      "BLE preference disabled");
        }
        else
        {
            // BLE 既未运行又没有凭据时，自动重新拉起配网。
            ret = wifi_provision_start_blecfg();
            network_service_clear_cached_ip();
            if (ret == ESP_OK)
            {
                network_service_set_state(NETWORK_SERVICE_STATE_BLE_PROVISIONING,
                                          "restart BLE provisioning");
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

    (void)network_service_load_ble_pref();
    (void)network_service_load_transport_pref();

    // 固定在 core0，和本项目其他系统服务保持一致，避免和 UI 主线程争抢。
    BaseType_t result =
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
 * @return 统一的服务层状态枚举。
 */
network_service_state_t network_service_get_state(void)
{
    return s_network_state;
}

/**
 * @brief 设置 BLE 配网总开关。
 * @param[in] enabled 目标开关值。
 * @return 执行结果。
 */
esp_err_t network_service_set_ble_enabled(bool enabled)
{
    esp_err_t pref_ret = ESP_OK;
    esp_err_t ret = ESP_OK;
    const bool ble_active = wifi_provision_is_ble_active();
    const bool has_credentials = wifi_provision_has_credentials();

    (void)network_service_load_ble_pref();
    ESP_LOGI(TAG,
             "set BLE enabled request: enabled=%d active=%d has_credentials=%d portal_requested=%d",
             enabled ? 1 : 0, ble_active ? 1 : 0, has_credentials ? 1 : 0,
             s_portal_requested ? 1 : 0);

    if (enabled)
    {
        if (has_credentials)
        {
            ESP_LOGW(TAG,
                     "reject BLE enable request because saved Wi-Fi credentials already exist");
            return ESP_ERR_INVALID_STATE;
        }

        s_ble_enabled = enabled;
        pref_ret = network_service_store_ble_pref(enabled);
        s_portal_requested = false;
        network_service_clear_cached_ip();

        ret = wifi_provision_start_blecfg();
        if (ret == ESP_OK)
        {
            network_service_set_state(NETWORK_SERVICE_STATE_BLE_PROVISIONING,
                                      "explicit BLE enable request");
        }
        else
        {
            ESP_LOGW(TAG, "enable BLE provisioning failed: %s",
                     esp_err_to_name(ret));
        }

        if (pref_ret != ESP_OK)
        {
            return pref_ret;
        }
        return ret;
    }

    s_ble_enabled = enabled;
    pref_ret = network_service_store_ble_pref(enabled);
    ret = wifi_provision_stop_blecfg();
    if (!has_credentials && !wifi_provision_is_ap_active() &&
        !s_portal_requested)
    {
        network_service_clear_cached_ip();
        network_service_set_state(NETWORK_SERVICE_STATE_BLE_DISABLED,
                                  "explicit BLE disable request");
    }

    if (pref_ret != ESP_OK)
    {
        return pref_ret;
    }
    return ret;
}

/**
 * @brief 查询 BLE 偏好是否开启。
 * @return true 表示允许后台拉起 BLE。
 */
bool network_service_is_ble_enabled(void)
{
    (void)network_service_load_ble_pref();
    return s_ble_enabled;
}

/**
 * @brief 查询 BLE 是否真的活动。
 * @return true 表示 BLE 广播或连接存在。
 */
bool network_service_is_ble_active(void)
{
    return wifi_provision_is_ble_active();
}

bool network_service_is_wifi_connected(void)
{
    return wifi_provision_is_connected();
}

esp_err_t network_service_get_wifi_status(network_service_wifi_status_t *status)
{
    if (status == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    memset(status, 0, sizeof(*status));
    status->wifi_connected = wifi_provision_is_connected();
    status->has_credentials = wifi_provision_has_credentials();
    status->user_disconnect_latched = s_user_disconnect_latched;
    status->ble_active = wifi_provision_is_ble_active();
    status->ap_active = wifi_provision_is_ap_active();
    status->provisioning_active = status->ble_active || status->ap_active;
    status->default_transport = s_default_transport;
    if (network_service_get_ip(status->ip, sizeof(status->ip)) != ESP_OK)
    {
        status->ip[0] = '\0';
    }
    return ESP_OK;
}

esp_err_t network_service_request_connect_with_saved_credentials(void)
{
    s_user_disconnect_latched = false;
    s_reprovision_requested = false;
    s_portal_requested = false;
    wifi_provision_set_auto_reconnect_enabled(true);
    (void)wifi_provision_stop_active_transport();
    network_service_clear_cached_ip();
    network_service_set_state(NETWORK_SERVICE_STATE_CONNECTING,
                              "manual connect with saved credentials");
    return wifi_provision_connect_saved();
}

esp_err_t network_service_request_disconnect(void)
{
    s_user_disconnect_latched = true;
    s_reprovision_requested = false;
    s_portal_requested = false;
    wifi_provision_set_auto_reconnect_enabled(false);
    (void)wifi_provision_stop_active_transport();
    network_service_clear_cached_ip();
    network_service_set_state(NETWORK_SERVICE_STATE_OFFLINE,
                              "manual disconnect request");
    return wifi_provision_disconnect_sta();
}

esp_err_t network_service_request_reprovision(void)
{
    esp_err_t ret = ESP_OK;

    s_user_disconnect_latched = false;
    s_reprovision_requested = true;
    s_portal_requested = false;
    wifi_provision_set_auto_reconnect_enabled(false);
    (void)wifi_provision_disconnect_sta();
    (void)wifi_provision_stop_active_transport();

    ret = network_service_start_selected_provision_transport();
    if (ret == ESP_OK)
    {
        network_service_update_state_for_active_transport();
    }
    else
    {
        network_service_set_state(NETWORK_SERVICE_STATE_ERROR,
                                  "manual reprovision start failed");
    }
    return ret;
}

esp_err_t network_service_set_default_provision_transport(
    network_service_provision_transport_t transport)
{
    if (transport > NETWORK_SERVICE_PROVISION_TRANSPORT_AP)
    {
        return ESP_ERR_INVALID_ARG;
    }

    s_default_transport = transport;
    s_transport_pref_loaded = true;
    ESP_LOGI(TAG, "default provision transport set: %d", (int)transport);
    return network_service_store_transport_pref(transport);
}

network_service_provision_transport_t
network_service_get_default_provision_transport(void)
{
    (void)network_service_load_transport_pref();
    return s_default_transport;
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
    s_user_disconnect_latched = false;
    s_reprovision_requested = true;
    wifi_provision_set_auto_reconnect_enabled(false);
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
    esp_err_t ret = network_service_set_ble_enabled(true);

    s_user_disconnect_latched = false;
    s_reprovision_requested = true;

    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "explicit BLE provisioning start failed: %s",
                 esp_err_to_name(ret));
    }
}
