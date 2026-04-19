#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "network_credentials.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief `network_manager` 对外暴露的主状态机。
 *
 * 该状态机表达的是项目网络产品语义，而不是底层 Wi-Fi/BLE driver 细节。
 */
typedef enum
{
    NETWORK_MANAGER_STATE_IDLE = 0, /**< 尚未启动网络编排。 */
    NETWORK_MANAGER_STATE_CONNECTING_LATEST, /**< 正在尝试最近一次成功连接的 Wi-Fi。 */
    NETWORK_MANAGER_STATE_CONNECTED, /**< 当前已成功连接 Wi-Fi。 */
    NETWORK_MANAGER_STATE_PROVISIONING_BLE, /**< 当前正在走 BLE 配网 transport。 */
    NETWORK_MANAGER_STATE_PROVISIONING_SOFTAP, /**< 当前正在走 SoftAP 配网 transport。 */
    NETWORK_MANAGER_STATE_DISCONNECTED_BY_USER, /**< 用户主动断网后暂停自动重连。 */
    NETWORK_MANAGER_STATE_ERROR, /**< 当前网络编排进入错误态。 */
} network_manager_state_t;

/**
 * @brief `network_manager` 可选择的配网 transport。
 *
 * 当前长期架构只保留 `BLE / SOFTAP`，不再保留 `AUTO`。
 */
typedef enum
{
    NETWORK_MANAGER_PROVISIONING_TRANSPORT_BLE = 0, /**< 默认走 BLE 配网 transport。 */
    NETWORK_MANAGER_PROVISIONING_TRANSPORT_SOFTAP, /**< 默认走 SoftAP 配网 transport。 */
} network_manager_provisioning_transport_t;

/**
 * @brief 对 UI / 业务层暴露的网络状态快照。
 */
typedef struct
{
    network_manager_state_t state; /**< 当前主状态机状态。 */
    bool wifi_connected; /**< 当前是否已连上 Wi-Fi。 */
    bool ble_enabled; /**< BLE 总开关偏好是否开启。 */
    bool ble_active; /**< BLE transport 当前是否活跃。 */
    network_manager_provisioning_transport_t default_transport; /**< 当前默认配网 transport。 */
    char ip[16]; /**< 当前 IPv4 字符串，无 IP 时为空字符串。 */
} network_manager_status_t;

/**
 * @brief 启动统一网络编排层。
 *
 * 当前阶段该接口先冻结 façade 入口，后续再接入真实状态机与底层组件。
 *
 * @return `ESP_OK` 表示入口调用成功；其他错误表示底层编排初始化失败。
 */
esp_err_t network_manager_start(void);

/**
 * @brief 获取当前网络主状态。
 * @return 当前 `network_manager` 状态。
 */
network_manager_state_t network_manager_get_state(void);

/**
 * @brief 获取当前网络状态快照。
 *
 * @param[out] status 输出状态结构。
 * @return `ESP_OK` 表示成功；`ESP_ERR_INVALID_ARG` 表示参数非法。
 */
esp_err_t network_manager_get_status(network_manager_status_t *status);

/**
 * @brief 再次尝试最近一次成功连接的 Wi-Fi。
 *
 * @return `ESP_OK` 表示请求已接收；其他错误表示当前条件不允许启动该流程。
 */
esp_err_t network_manager_use_latest_wifi(void);

/**
 * @brief 主动断开当前网络连接，并进入用户断开态。
 *
 * @return `ESP_OK` 表示请求已接收；其他错误表示底层断开失败。
 */
esp_err_t network_manager_disconnect(void);

/**
 * @brief 重新进入当前默认 provisioning transport。
 *
 * @return `ESP_OK` 表示请求已接收；其他错误表示 transport 启动失败。
 */
esp_err_t network_manager_reprovision(void);

/**
 * @brief 设置 BLE 总开关偏好。
 *
 * @param[in] enabled true 表示允许 BLE；false 表示关闭 BLE。
 * @return `ESP_OK` 表示更新成功；其他错误表示更新失败。
 */
esp_err_t network_manager_set_ble_enabled(bool enabled);

/**
 * @brief 查询 BLE 总开关偏好。
 * @return true 表示允许 BLE。
 */
bool network_manager_is_ble_enabled(void);

/**
 * @brief 查询 BLE transport 当前是否活跃。
 * @return true 表示 BLE transport 当前活跃。
 */
bool network_manager_is_ble_active(void);

/**
 * @brief 设置默认 provisioning transport。
 *
 * @param[in] transport 目标 provisioning transport。
 * @return `ESP_OK` 表示更新成功；其他错误表示参数非法或持久化失败。
 */
esp_err_t network_manager_set_default_transport(
    network_manager_provisioning_transport_t transport);

/**
 * @brief 获取默认 provisioning transport。
 * @return 当前默认 transport。
 */
network_manager_provisioning_transport_t network_manager_get_default_transport(void);

/**
 * @brief 读取当前保存的 recent Wi-Fi 列表。
 *
 * @param[out] entries 输出数组，可为 `NULL`。
 * @param[in] max_entries 调用方可接收的最大条目数。
 * @param[out] out_count 当前真实 recent 条目数。
 * @return `ESP_OK` 表示成功；其他错误表示 recent 列表读取失败。
 */
esp_err_t network_manager_get_recent_networks(
    network_credentials_entry_t *entries, size_t max_entries, size_t *out_count);

/**
 * @brief 按 recent 列表索引发起连接。
 *
 * 该接口为后续 Wi-Fi 管理页“手动点选最近网络”预留。
 *
 * @param[in] index recent 列表索引，`0` 表示最新一条。
 * @return `ESP_OK` 表示请求已接收；其他错误表示索引非法或当前不支持。
 */
esp_err_t network_manager_connect_recent_by_index(size_t index);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_MANAGER_H */
