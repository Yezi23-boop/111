#include "ble_provision_protocol.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

static esp_err_t ble_provision_protocol_copy_json(cJSON *root, char *buffer,
                                                  size_t buffer_len) {
    char *json = NULL;

    if (root == NULL || buffer == NULL || buffer_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    json = cJSON_PrintUnformatted(root);
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (strlen(json) + 1 > buffer_len) {
        cJSON_free(json);
        return ESP_ERR_INVALID_SIZE;
    }

    snprintf(buffer, buffer_len, "%s", json);
    cJSON_free(json);
    return ESP_OK;
}

esp_err_t ble_provision_protocol_parse_request(const char *data,
                                               ble_prov_request_t *request) {
    cJSON *root = NULL;
    cJSON *cmd = NULL;
    cJSON *ssid = NULL;
    cJSON *password = NULL;

    if (data == NULL || request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(request, 0, sizeof(*request));

    root = cJSON_Parse(data);
    if (root == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cmd = cJSON_GetObjectItem(root, "cmd");
    if (!cJSON_IsString(cmd) || cmd->valuestring == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    if (strcmp(cmd->valuestring, "hello") == 0) {
        request->cmd = BLE_PROV_CMD_HELLO;
    } else if (strcmp(cmd->valuestring, "status") == 0) {
        request->cmd = BLE_PROV_CMD_STATUS;
    } else if (strcmp(cmd->valuestring, "start_ap_fallback") == 0) {
        request->cmd = BLE_PROV_CMD_START_AP_FALLBACK;
    } else if (strcmp(cmd->valuestring, "set_wifi") == 0) {
        request->cmd = BLE_PROV_CMD_SET_WIFI;
        ssid = cJSON_GetObjectItem(root, "ssid");
        password = cJSON_GetObjectItem(root, "password");
        if (!cJSON_IsString(ssid) || ssid->valuestring == NULL ||
            ssid->valuestring[0] == '\0' || !cJSON_IsString(password) ||
            password->valuestring == NULL) {
            cJSON_Delete(root);
            return ESP_ERR_INVALID_ARG;
        }

        snprintf(request->ssid, sizeof(request->ssid), "%s", ssid->valuestring);
        snprintf(request->password, sizeof(request->password), "%s",
                 password->valuestring);
    } else {
        cJSON_Delete(root);
        return ESP_ERR_NOT_SUPPORTED;
    }

    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t ble_provision_protocol_format_hello(char *buffer, size_t buffer_len,
                                              const char *device_name) {
    cJSON *root = NULL;
    esp_err_t ret = ESP_FAIL;

    if (buffer == NULL || buffer_len == 0 || device_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
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

esp_err_t ble_provision_protocol_format_status(char *buffer, size_t buffer_len,
                                               const char *state,
                                               const char *ssid,
                                               const char *ip,
                                               const char *reason,
                                               const char *url) {
    cJSON *root = NULL;
    esp_err_t ret = ESP_FAIL;

    if (buffer == NULL || buffer_len == 0 || state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "evt", "status");
    cJSON_AddStringToObject(root, "state", state);
    if (ssid != NULL && ssid[0] != '\0') {
        cJSON_AddStringToObject(root, "ssid", ssid);
    }
    if (ip != NULL && ip[0] != '\0') {
        cJSON_AddStringToObject(root, "ip", ip);
    }
    if (reason != NULL && reason[0] != '\0') {
        cJSON_AddStringToObject(root, "reason", reason);
    }
    if (url != NULL && url[0] != '\0') {
        cJSON_AddStringToObject(root, "url", url);
    }

    ret = ble_provision_protocol_copy_json(root, buffer, buffer_len);
    cJSON_Delete(root);
    return ret;
}
