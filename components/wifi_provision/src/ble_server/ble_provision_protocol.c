#include "ble_provision_protocol.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

/**
 * @brief 将 cJSON 对象序列化到输出缓冲区。
 *
 * BLE 链路负载预算很紧张，因此这里使用紧凑 JSON，避免冗余空白进一步压缩有效载荷。
 *
 * @param[in] root 待序列化的 cJSON 根对象。
 * @param[out] buffer 输出缓冲区。
 * @param[in] buffer_len 输出缓冲区长度，单位为字节。
 * @return `ESP_OK` 表示成功；其他错误表示参数非法、内存不足或缓冲区长度不够。
 */
static esp_err_t ble_provision_protocol_copy_json(cJSON *root, char *buffer,
                                                  size_t buffer_len)
{
    char *json = NULL;

    if (root == NULL || buffer == NULL || buffer_len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    json = cJSON_PrintUnformatted(root); // 生成紧凑 JSON，降低 BLE 传输负担
    if (json == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    if (strlen(json) + 1 > buffer_len)
    {
        cJSON_free(json);
        return ESP_ERR_INVALID_SIZE;
    }

    snprintf(buffer, buffer_len, "%s", json);
    cJSON_free(json);
    return ESP_OK;
}

/**
 * @brief 创建 Wi-Fi 扫描响应的公共 JSON 根对象。
 * @param[out] buffer 输出缓冲区，仅用于参数合法性检查。
 * @param[in] buffer_len 输出缓冲区长度，单位为字节。
 * @param[in] state 扫描状态字符串。
 * @param[out] root_out 输出创建好的 cJSON 根对象。
 * @return `ESP_OK` 表示成功；其他错误表示参数非法或内存不足。
 */
static esp_err_t ble_provision_protocol_create_wifi_scan_root(
    char *buffer, size_t buffer_len, const char *state, cJSON **root_out)
{
    cJSON *root = NULL;
    esp_err_t ret = ESP_FAIL;

    if (buffer == NULL || buffer_len == 0 || state == NULL || root_out == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    root = cJSON_CreateObject();
    if (root == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "evt", "wifi_scan");
    cJSON_AddStringToObject(root, "state", state); // started/batch/done/failed

    *root_out = root;
    ret = ESP_OK;
    return ret;
}

/**
 * @brief 解析一帧 BLE 配网 JSON 请求。
 * @param[in] data JSON 文本。
 * @param[out] request 解析后的请求结构。
 * @return `ESP_OK` 表示成功；其他错误表示字段缺失、类型错误或 JSON 非法。
 */
esp_err_t ble_provision_protocol_parse_request(const char *data,
                                               ble_prov_request_t *request)
{
    cJSON *root = NULL;
    cJSON *cmd = NULL;
    cJSON *ssid = NULL;
    cJSON *password = NULL;

    if (data == NULL || request == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    memset(request, 0, sizeof(*request));

    root = cJSON_Parse(data);
    if (root == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    cmd = cJSON_GetObjectItem(root, "cmd");
    if (!cJSON_IsString(cmd) || cmd->valuestring == NULL)
    {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    if (strcmp(cmd->valuestring, "hello") == 0)
    {
        request->cmd = BLE_PROV_CMD_HELLO;
    }
    else if (strcmp(cmd->valuestring, "status") == 0)
    {
        request->cmd = BLE_PROV_CMD_STATUS;
    }
    else if (strcmp(cmd->valuestring, "scan_wifi") == 0)
    {
        request->cmd = BLE_PROV_CMD_SCAN_WIFI;
    }
    else if (strcmp(cmd->valuestring, "start_ap_fallback") == 0)
    {
        request->cmd = BLE_PROV_CMD_START_AP_FALLBACK;
    }
    else if (strcmp(cmd->valuestring, "set_wifi") == 0)
    {
        request->cmd = BLE_PROV_CMD_SET_WIFI;
        ssid = cJSON_GetObjectItem(root, "ssid");
        password = cJSON_GetObjectItem(root, "password");
        if (!cJSON_IsString(ssid) || ssid->valuestring == NULL ||
            ssid->valuestring[0] == '\0' || !cJSON_IsString(password) ||
            password->valuestring == NULL)
        {
            cJSON_Delete(root);
            return ESP_ERR_INVALID_ARG;
        }

        snprintf(request->ssid, sizeof(request->ssid), "%s", ssid->valuestring);
        snprintf(request->password, sizeof(request->password), "%s",
                 password->valuestring);
    }
    else
    {
        cJSON_Delete(root);
        return ESP_ERR_NOT_SUPPORTED;
    }

    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief 格式化 BLE hello 响应。
 * @param[out] buffer 输出缓冲区。
 * @param[in] buffer_len 输出缓冲区长度，单位为字节。
 * @param[in] device_name 当前 BLE 广播名。
 * @return `ESP_OK` 表示成功；其他错误表示参数非法、内存不足或缓冲区不够。
 */
esp_err_t ble_provision_protocol_format_hello(char *buffer, size_t buffer_len,
                                              const char *device_name)
{
    cJSON *root = NULL;
    esp_err_t ret = ESP_FAIL;

    if (buffer == NULL || buffer_len == 0 || device_name == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    root = cJSON_CreateObject();
    if (root == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "evt", "hello");
    cJSON_AddStringToObject(root, "name", device_name);
    cJSON_AddNumberToObject(root, "ver", 1);
    cJSON_AddStringToObject(root, "fallback", "ap");

    ret = ble_provision_protocol_copy_json(root, buffer, buffer_len);
    cJSON_Delete(root);
    return ret;
}

/**
 * @brief 格式化“开始扫描 Wi-Fi”响应。
 * @param[out] buffer 输出缓冲区。
 * @param[in] buffer_len 输出缓冲区长度，单位为字节。
 * @return `ESP_OK` 表示成功；其他错误表示参数非法、内存不足或缓冲区不够。
 */
esp_err_t ble_provision_protocol_format_wifi_scan_started(char *buffer,
                                                          size_t buffer_len)
{
    cJSON *root = NULL;
    esp_err_t ret = ble_provision_protocol_create_wifi_scan_root(
        buffer, buffer_len, "started", &root);

    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = ble_provision_protocol_copy_json(root, buffer, buffer_len);
    cJSON_Delete(root);
    return ret;
}

/**
 * @brief 格式化一批 Wi-Fi 扫描结果。
 * @param[out] buffer 输出缓冲区。
 * @param[in] buffer_len 输出缓冲区长度，单位为字节。
 * @param[in] items 扫描结果数组。
 * @param[in] item_count 当前批次条目数。
 * @param[in] more true 表示后续仍有更多批次。
 * @return `ESP_OK` 表示成功；其他错误表示参数非法、内存不足或缓冲区不够。
 */
esp_err_t ble_provision_protocol_format_wifi_scan_batch(
    char *buffer, size_t buffer_len, const ble_prov_wifi_scan_item_t *items,
    size_t item_count, bool more)
{
    cJSON *root = NULL;
    cJSON *item_array = NULL;
    esp_err_t ret = ble_provision_protocol_create_wifi_scan_root(
        buffer, buffer_len, "batch", &root);

    if (ret != ESP_OK)
    {
        return ret;
    }

    item_array = cJSON_AddArrayToObject(root, "items");
    if (item_array == NULL)
    {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    for (size_t index = 0; index < item_count; ++index)
    {
        cJSON *item = cJSON_CreateObject();
        if (item == NULL)
        {
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }

        cJSON_AddStringToObject(item, "ssid", items[index].ssid);
        cJSON_AddNumberToObject(item, "rssi", items[index].rssi);
        cJSON_AddBoolToObject(item, "encrypted", items[index].encrypted);
        cJSON_AddItemToArray(item_array, item);
    }

    cJSON_AddBoolToObject(root, "more", more);
    ret = ble_provision_protocol_copy_json(root, buffer, buffer_len);
    cJSON_Delete(root);
    return ret;
}

/**
 * @brief 格式化 Wi-Fi 扫描完成响应。
 * @param[out] buffer 输出缓冲区。
 * @param[in] buffer_len 输出缓冲区长度，单位为字节。
 * @param[in] total 扫描结果总数。
 * @return `ESP_OK` 表示成功；其他错误表示参数非法、内存不足或缓冲区不够。
 */
esp_err_t ble_provision_protocol_format_wifi_scan_done(char *buffer,
                                                       size_t buffer_len,
                                                       size_t total)
{
    cJSON *root = NULL;
    esp_err_t ret = ble_provision_protocol_create_wifi_scan_root(
        buffer, buffer_len, "done", &root);

    if (ret != ESP_OK)
    {
        return ret;
    }

    cJSON_AddNumberToObject(root, "total", (double)total);
    ret = ble_provision_protocol_copy_json(root, buffer, buffer_len);
    cJSON_Delete(root);
    return ret;
}

/**
 * @brief 格式化 Wi-Fi 扫描失败响应。
 * @param[out] buffer 输出缓冲区。
 * @param[in] buffer_len 输出缓冲区长度，单位为字节。
 * @param[in] reason 失败原因。
 * @return `ESP_OK` 表示成功；其他错误表示参数非法、内存不足或缓冲区不够。
 */
esp_err_t ble_provision_protocol_format_wifi_scan_failed(char *buffer,
                                                         size_t buffer_len,
                                                         const char *reason)
{
    cJSON *root = NULL;
    esp_err_t ret = ble_provision_protocol_create_wifi_scan_root(
        buffer, buffer_len, "failed", &root);

    if (ret != ESP_OK)
    {
        return ret;
    }

    if (reason != NULL && reason[0] != '\0')
    {
        cJSON_AddStringToObject(root, "reason", reason);
    }

    ret = ble_provision_protocol_copy_json(root, buffer, buffer_len);
    cJSON_Delete(root);
    return ret;
}

/**
 * @brief 格式化 BLE 状态响应。
 * @param[out] buffer 输出缓冲区。
 * @param[in] buffer_len 输出缓冲区长度，单位为字节。
 * @param[in] state 状态字符串。
 * @param[in] ssid 当前 SSID，可为 NULL。
 * @param[in] ip 当前 IP，可为 NULL。
 * @param[in] reason 失败原因，可为 NULL。
 * @param[in] url AP 兜底 URL，可为 NULL。
 * @return `ESP_OK` 表示成功；其他错误表示参数非法、内存不足或缓冲区不够。
 */
esp_err_t ble_provision_protocol_format_status(char *buffer, size_t buffer_len,
                                               const char *state,
                                               const char *ssid,
                                               const char *ip,
                                               const char *reason,
                                               const char *url)
{
    cJSON *root = NULL;
    esp_err_t ret = ESP_FAIL;

    if (buffer == NULL || buffer_len == 0 || state == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    root = cJSON_CreateObject();
    if (root == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "evt", "status");
    cJSON_AddStringToObject(root, "state", state); // idle/connecting/connected/failed/ap_fallback
    if (ssid != NULL && ssid[0] != '\0')
    {
        cJSON_AddStringToObject(root, "ssid", ssid);
    }
    if (ip != NULL && ip[0] != '\0')
    {
        cJSON_AddStringToObject(root, "ip", ip);
    }
    if (reason != NULL && reason[0] != '\0')
    {
        cJSON_AddStringToObject(root, "reason", reason);
    }
    if (url != NULL && url[0] != '\0')
    {
        cJSON_AddStringToObject(root, "url", url);
    }

    ret = ble_provision_protocol_copy_json(root, buffer, buffer_len);
    cJSON_Delete(root);
    return ret;
}
