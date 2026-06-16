#include "services/memory_watch_voice_client.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "memory_watch_http";

static const char kVoiceCommandPath[] = "/v1/watch/voice-command";
static const char kTextCommandPath[] = "/v1/watch/text-command";
static const char kHealthPathPrefix[] = "/v1/watch/health?device_id=";
static const char kCancelPathPrefix[] = "/v1/watch/request/";
static const char kCancelPathSuffix[] = "/cancel";
static const char kBoundaryPrefix[] = "ai-memory-watch-";
static const char kLocaleZhCn[] = "zh-CN";
static const char kTimezoneShanghai[] = "Asia/Shanghai";
static const char kSourceWatchHermesPage[] = "watch_hermes_page";
static const char kDefaultUiState[] = "ready";
static const size_t kMaxResponseBytes = 4096U;
static const int kHttpBufferSize = 1024;

static esp_err_t memory_watch_voice_client_write_all(
    esp_http_client_handle_t client, const uint8_t *data, size_t len);

static void *memory_watch_voice_client_alloc(size_t len)
{
    void *ptr = heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == NULL)
    {
        ptr = heap_caps_malloc(len, MALLOC_CAP_8BIT);
    }
    return ptr;
}

static void memory_watch_voice_client_free(void *ptr)
{
    if (ptr != NULL)
    {
        heap_caps_free(ptr);
    }
}

static bool memory_watch_voice_client_is_safe_form_text(const char *value)
{
    if (value == NULL)
    {
        return true;
    }

    for (const char *p = value; *p != '\0'; ++p)
    {
        if (*p == '\r' || *p == '\n')
        {
            return false;
        }
    }
    return true;
}

static bool memory_watch_voice_client_is_request_id_valid(
    const char *request_id)
{
    if (request_id == NULL || request_id[0] == '\0')
    {
        return false;
    }

    size_t len = 0;
    for (const char *p = request_id; *p != '\0'; ++p)
    {
        const char c = *p;
        const bool allowed =
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '.' || c == '_' || c == ':' || c == '-';
        if (!allowed)
        {
            return false;
        }

        ++len;
        if (len > MEMORY_WATCH_VOICE_CLIENT_REQUEST_ID_MAX_LEN)
        {
            return false;
        }
    }
    return true;
}

static uint32_t memory_watch_voice_client_timeout_ms(
    const memory_watch_voice_client_config_t *config)
{
    if (config->timeout_ms == 0U)
    {
        return MEMORY_WATCH_VOICE_CLIENT_DEFAULT_TIMEOUT_MS;
    }
    return config->timeout_ms;
}

static esp_err_t memory_watch_voice_client_validate_config(
    const memory_watch_voice_client_config_t *config)
{
    if (config == NULL || config->base_url == NULL ||
        config->device_id == NULL || config->device_token == NULL ||
        config->base_url[0] == '\0' || config->device_id[0] == '\0' ||
        config->device_token[0] == '\0')
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!memory_watch_voice_client_is_safe_form_text(config->base_url) ||
        !memory_watch_voice_client_is_safe_form_text(config->device_id) ||
        !memory_watch_voice_client_is_safe_form_text(config->device_token))
    {
        return ESP_ERR_INVALID_ARG;
    }
    const bool uses_http = strncmp(config->base_url, "http://", 7) == 0;
    const bool uses_https = strncmp(config->base_url, "https://", 8) == 0;
    if (!uses_http && !uses_https)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (uses_http && !config->allow_insecure_http)
    {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static bool memory_watch_voice_client_request_id_matches_device(
    const char *request_id, const char *device_id)
{
    const size_t device_len = strlen(device_id);
    return strncmp(request_id, device_id, device_len) == 0 &&
           request_id[device_len] == '-';
}

static esp_err_t memory_watch_voice_client_validate_request(
    const memory_watch_voice_client_request_t *request)
{
    if (request == NULL || request->audio == NULL ||
        request->audio_len == 0U ||
        request->audio_len > MEMORY_WATCH_VOICE_CLIENT_MAX_AUDIO_BYTES ||
        !memory_watch_voice_client_is_request_id_valid(request->request_id))
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (request->has_battery_percent &&
        (request->battery_percent < 0 || request->battery_percent > 100))
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (request->has_rssi && (request->rssi < -127 || request->rssi > 0))
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!memory_watch_voice_client_is_safe_form_text(
            request->clarification_id) ||
        !memory_watch_voice_client_is_safe_form_text(
            request->firmware_version) ||
        !memory_watch_voice_client_is_safe_form_text(request->ui_state))
    {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t memory_watch_voice_client_validate_text_request(
    const memory_watch_voice_client_text_request_t *request)
{
    if (request == NULL || request->text == NULL ||
        request->text[0] == '\0' ||
        strlen(request->text) >= MEMORY_WATCH_VOICE_CLIENT_MAX_TEXT_BYTES ||
        !memory_watch_voice_client_is_request_id_valid(request->request_id))
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (request->has_battery_percent &&
        (request->battery_percent < 0 || request->battery_percent > 100))
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (request->has_rssi && (request->rssi < -127 || request->rssi > 0))
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!memory_watch_voice_client_is_safe_form_text(request->text) ||
        !memory_watch_voice_client_is_safe_form_text(
            request->clarification_id) ||
        !memory_watch_voice_client_is_safe_form_text(
            request->firmware_version) ||
        !memory_watch_voice_client_is_safe_form_text(request->ui_state))
    {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t memory_watch_voice_client_build_url(
    const char *base_url, const char *path, char *out_url, size_t out_url_len)
{
    size_t base_len = strlen(base_url);
    while (base_len > 0U && base_url[base_len - 1U] == '/')
    {
        --base_len;
    }

    const int written = snprintf(out_url, out_url_len, "%.*s%s",
                                 (int)base_len, base_url, path);
    if (written < 0 || (size_t)written >= out_url_len)
    {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static bool memory_watch_voice_client_is_url_unreserved(char c)
{
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.' || c == '~';
}

static esp_err_t memory_watch_voice_client_append_url_encoded(
    char *dst, size_t dst_len, size_t *offset, const char *value)
{
    static const char kHex[] = "0123456789ABCDEF";
    for (const char *p = value; *p != '\0'; ++p)
    {
        const unsigned char c = (unsigned char)*p;
        if (memory_watch_voice_client_is_url_unreserved((char)c))
        {
            if (*offset + 1U >= dst_len)
            {
                return ESP_ERR_INVALID_SIZE;
            }
            dst[*offset] = (char)c;
            *offset += 1U;
            continue;
        }

        if (*offset + 3U >= dst_len)
        {
            return ESP_ERR_INVALID_SIZE;
        }
        dst[*offset] = '%';
        dst[*offset + 1U] = kHex[(c >> 4U) & 0x0FU];
        dst[*offset + 2U] = kHex[c & 0x0FU];
        *offset += 3U;
    }
    dst[*offset] = '\0';
    return ESP_OK;
}

static esp_err_t memory_watch_voice_client_build_health_path(
    const char *device_id, char *out_path, size_t out_path_len)
{
    const size_t prefix_len = strlen(kHealthPathPrefix);
    if (prefix_len >= out_path_len)
    {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(out_path, kHealthPathPrefix, prefix_len);
    size_t offset = prefix_len;
    return memory_watch_voice_client_append_url_encoded(
        out_path, out_path_len, &offset, device_id);
}

static esp_err_t memory_watch_voice_client_build_cancel_path(
    const char *request_id, char *out_path, size_t out_path_len)
{
    const int written = snprintf(out_path, out_path_len, "%s%s%s",
                                 kCancelPathPrefix, request_id,
                                 kCancelPathSuffix);
    if (written < 0 || (size_t)written >= out_path_len)
    {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static esp_err_t memory_watch_voice_client_build_bearer(
    const char *device_token, char **out_header)
{
    const char *prefix = "Bearer ";
    const size_t prefix_len = strlen(prefix);
    const size_t token_len = strlen(device_token);
    char *header =
        (char *)memory_watch_voice_client_alloc(prefix_len + token_len + 1U);
    if (header == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    memcpy(header, prefix, prefix_len);
    memcpy(header + prefix_len, device_token, token_len + 1U);
    *out_header = header;
    return ESP_OK;
}

static size_t memory_watch_voice_client_text_part_len(
    const char *boundary, const char *name, const char *value)
{
    return strlen("--") + strlen(boundary) + strlen("\r\n") +
           strlen("Content-Disposition: form-data; name=\"") +
           strlen(name) + strlen("\"\r\n\r\n") +
           strlen(value) + strlen("\r\n");
}

static size_t memory_watch_voice_client_audio_part_len(
    const char *boundary, size_t audio_len)
{
    return strlen("--") + strlen(boundary) + strlen("\r\n") +
           strlen("Content-Disposition: form-data; name=\"audio\"; "
                  "filename=\"command.ogg\"\r\n") +
           strlen("Content-Type: audio/ogg\r\n\r\n") +
           audio_len + strlen("\r\n");
}

static size_t memory_watch_voice_client_finish_len(const char *boundary)
{
    return strlen("--") + strlen(boundary) + strlen("--\r\n");
}

static esp_err_t memory_watch_voice_client_add_text_part_len(
    const char *boundary, const char *name, const char *value,
    size_t *body_len)
{
    const size_t part_len =
        memory_watch_voice_client_text_part_len(boundary, name, value);
    if (part_len > SIZE_MAX - *body_len)
    {
        return ESP_ERR_INVALID_SIZE;
    }
    *body_len += part_len;
    return ESP_OK;
}

static esp_err_t memory_watch_voice_client_compute_body_len(
    const memory_watch_voice_client_config_t *config,
    const memory_watch_voice_client_request_t *request,
    const char *boundary,
    size_t *out_body_len)
{
    char battery_text[12];
    char charging_text[6];
    char rssi_text[12];
    const char *ui_state =
        (request->ui_state != NULL && request->ui_state[0] != '\0')
            ? request->ui_state
            : kDefaultUiState;
    const char *clarification_id =
        request->clarification_id != NULL ? request->clarification_id : "";

    size_t body_len = 0;
    esp_err_t err = memory_watch_voice_client_add_text_part_len(
        boundary, "request_id", request->request_id, &body_len);
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_add_text_part_len(
            boundary, "device_id", config->device_id, &body_len);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_add_text_part_len(
            boundary, "clarification_id", clarification_id, &body_len);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_add_text_part_len(
            boundary, "locale", kLocaleZhCn, &body_len);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_add_text_part_len(
            boundary, "timezone", kTimezoneShanghai, &body_len);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_add_text_part_len(
            boundary, "source", kSourceWatchHermesPage, &body_len);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_add_text_part_len(
            boundary, "ui_state", ui_state, &body_len);
    }
    if (err != ESP_OK)
    {
        return err;
    }

    if (request->has_battery_percent)
    {
        snprintf(battery_text, sizeof(battery_text), "%d",
                 request->battery_percent);
        err = memory_watch_voice_client_add_text_part_len(
            boundary, "battery_percent", battery_text, &body_len);
        if (err != ESP_OK)
        {
            return err;
        }
    }
    if (request->has_charging)
    {
        snprintf(charging_text, sizeof(charging_text), "%s",
                 request->charging ? "true" : "false");
        err = memory_watch_voice_client_add_text_part_len(
            boundary, "charging", charging_text, &body_len);
        if (err != ESP_OK)
        {
            return err;
        }
    }
    if (request->has_rssi)
    {
        snprintf(rssi_text, sizeof(rssi_text), "%d", request->rssi);
        err = memory_watch_voice_client_add_text_part_len(
            boundary, "rssi", rssi_text, &body_len);
        if (err != ESP_OK)
        {
            return err;
        }
    }
    if (request->firmware_version != NULL &&
        request->firmware_version[0] != '\0')
    {
        err = memory_watch_voice_client_add_text_part_len(
            boundary, "firmware_version", request->firmware_version,
            &body_len);
        if (err != ESP_OK)
        {
            return err;
        }
    }

    const size_t audio_part_len =
        memory_watch_voice_client_audio_part_len(boundary, request->audio_len);
    if (audio_part_len > SIZE_MAX - body_len)
    {
        return ESP_ERR_INVALID_SIZE;
    }
    body_len += audio_part_len;

    const size_t finish_len = memory_watch_voice_client_finish_len(boundary);
    if (finish_len > SIZE_MAX - body_len)
    {
        return ESP_ERR_INVALID_SIZE;
    }
    body_len += finish_len;

    *out_body_len = body_len;
    return ESP_OK;
}

static esp_err_t memory_watch_voice_client_compute_text_body_len(
    const memory_watch_voice_client_config_t *config,
    const memory_watch_voice_client_text_request_t *request,
    const char *boundary,
    size_t *out_body_len)
{
    char battery_text[12];
    char charging_text[6];
    char rssi_text[12];
    const char *ui_state =
        (request->ui_state != NULL && request->ui_state[0] != '\0')
            ? request->ui_state
            : kDefaultUiState;
    const char *clarification_id =
        request->clarification_id != NULL ? request->clarification_id : "";

    size_t body_len = 0;
    esp_err_t err = memory_watch_voice_client_add_text_part_len(
        boundary, "request_id", request->request_id, &body_len);
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_add_text_part_len(
            boundary, "device_id", config->device_id, &body_len);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_add_text_part_len(
            boundary, "text", request->text, &body_len);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_add_text_part_len(
            boundary, "clarification_id", clarification_id, &body_len);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_add_text_part_len(
            boundary, "locale", kLocaleZhCn, &body_len);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_add_text_part_len(
            boundary, "timezone", kTimezoneShanghai, &body_len);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_add_text_part_len(
            boundary, "source", kSourceWatchHermesPage, &body_len);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_add_text_part_len(
            boundary, "ui_state", ui_state, &body_len);
    }
    if (err != ESP_OK)
    {
        return err;
    }

    if (request->has_battery_percent)
    {
        snprintf(battery_text, sizeof(battery_text), "%d",
                 request->battery_percent);
        err = memory_watch_voice_client_add_text_part_len(
            boundary, "battery_percent", battery_text, &body_len);
        if (err != ESP_OK)
        {
            return err;
        }
    }
    if (request->has_charging)
    {
        snprintf(charging_text, sizeof(charging_text), "%s",
                 request->charging ? "true" : "false");
        err = memory_watch_voice_client_add_text_part_len(
            boundary, "charging", charging_text, &body_len);
        if (err != ESP_OK)
        {
            return err;
        }
    }
    if (request->has_rssi)
    {
        snprintf(rssi_text, sizeof(rssi_text), "%d", request->rssi);
        err = memory_watch_voice_client_add_text_part_len(
            boundary, "rssi", rssi_text, &body_len);
        if (err != ESP_OK)
        {
            return err;
        }
    }
    if (request->firmware_version != NULL &&
        request->firmware_version[0] != '\0')
    {
        err = memory_watch_voice_client_add_text_part_len(
            boundary, "firmware_version", request->firmware_version,
            &body_len);
        if (err != ESP_OK)
        {
            return err;
        }
    }

    const size_t finish_len = memory_watch_voice_client_finish_len(boundary);
    if (finish_len > SIZE_MAX - body_len)
    {
        return ESP_ERR_INVALID_SIZE;
    }
    body_len += finish_len;

    *out_body_len = body_len;
    return ESP_OK;
}

static esp_err_t memory_watch_voice_client_write_cstr(
    esp_http_client_handle_t client, const char *text)
{
    return memory_watch_voice_client_write_all(
        client, (const uint8_t *)text, strlen(text));
}

static esp_err_t memory_watch_voice_client_write_text_part(
    esp_http_client_handle_t client, const char *boundary,
    const char *name, const char *value)
{
    esp_err_t err = memory_watch_voice_client_write_cstr(client, "--");
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_cstr(client, boundary);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_cstr(client, "\r\n");
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_cstr(
            client, "Content-Disposition: form-data; name=\"");
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_cstr(client, name);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_cstr(client, "\"\r\n\r\n");
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_cstr(client, value);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_cstr(client, "\r\n");
    }
    return err;
}

static esp_err_t memory_watch_voice_client_write_audio_part(
    esp_http_client_handle_t client, const char *boundary,
    const uint8_t *audio, size_t audio_len)
{
    esp_err_t err = memory_watch_voice_client_write_cstr(client, "--");
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_cstr(client, boundary);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_cstr(client, "\r\n");
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_cstr(
            client,
            "Content-Disposition: form-data; name=\"audio\"; "
            "filename=\"command.ogg\"\r\n");
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_cstr(
            client, "Content-Type: audio/ogg\r\n\r\n");
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_all(client, audio, audio_len);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_cstr(client, "\r\n");
    }
    return err;
}

static esp_err_t memory_watch_voice_client_write_finish(
    esp_http_client_handle_t client, const char *boundary)
{
    esp_err_t err = memory_watch_voice_client_write_cstr(client, "--");
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_cstr(client, boundary);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_cstr(client, "--\r\n");
    }
    return err;
}

static esp_err_t memory_watch_voice_client_write_body(
    esp_http_client_handle_t client,
    const memory_watch_voice_client_config_t *config,
    const memory_watch_voice_client_request_t *request,
    const char *boundary)
{
    char battery_text[12];
    char charging_text[6];
    char rssi_text[12];
    const char *ui_state =
        (request->ui_state != NULL && request->ui_state[0] != '\0')
            ? request->ui_state
            : kDefaultUiState;
    const char *clarification_id =
        request->clarification_id != NULL ? request->clarification_id : "";

    esp_err_t err = memory_watch_voice_client_write_text_part(
        client, boundary, "request_id", request->request_id);
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_text_part(
            client, boundary, "device_id", config->device_id);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_text_part(
            client, boundary, "clarification_id", clarification_id);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_text_part(
            client, boundary, "locale", kLocaleZhCn);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_text_part(
            client, boundary, "timezone", kTimezoneShanghai);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_text_part(
            client, boundary, "source", kSourceWatchHermesPage);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_text_part(
            client, boundary, "ui_state", ui_state);
    }
    if (err == ESP_OK && request->has_battery_percent)
    {
        snprintf(battery_text, sizeof(battery_text), "%d",
                 request->battery_percent);
        err = memory_watch_voice_client_write_text_part(
            client, boundary, "battery_percent", battery_text);
    }
    if (err == ESP_OK && request->has_charging)
    {
        snprintf(charging_text, sizeof(charging_text), "%s",
                 request->charging ? "true" : "false");
        err = memory_watch_voice_client_write_text_part(
            client, boundary, "charging", charging_text);
    }
    if (err == ESP_OK && request->has_rssi)
    {
        snprintf(rssi_text, sizeof(rssi_text), "%d", request->rssi);
        err = memory_watch_voice_client_write_text_part(
            client, boundary, "rssi", rssi_text);
    }
    if (err == ESP_OK && request->firmware_version != NULL &&
        request->firmware_version[0] != '\0')
    {
        err = memory_watch_voice_client_write_text_part(
            client, boundary, "firmware_version",
            request->firmware_version);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_audio_part(
            client, boundary, request->audio, request->audio_len);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_finish(client, boundary);
    }
    return err;
}

static esp_err_t memory_watch_voice_client_write_text_body(
    esp_http_client_handle_t client,
    const memory_watch_voice_client_config_t *config,
    const memory_watch_voice_client_text_request_t *request,
    const char *boundary)
{
    char battery_text[12];
    char charging_text[6];
    char rssi_text[12];
    const char *ui_state =
        (request->ui_state != NULL && request->ui_state[0] != '\0')
            ? request->ui_state
            : kDefaultUiState;
    const char *clarification_id =
        request->clarification_id != NULL ? request->clarification_id : "";

    esp_err_t err = memory_watch_voice_client_write_text_part(
        client, boundary, "request_id", request->request_id);
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_text_part(
            client, boundary, "device_id", config->device_id);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_text_part(
            client, boundary, "text", request->text);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_text_part(
            client, boundary, "clarification_id", clarification_id);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_text_part(
            client, boundary, "locale", kLocaleZhCn);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_text_part(
            client, boundary, "timezone", kTimezoneShanghai);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_text_part(
            client, boundary, "source", kSourceWatchHermesPage);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_text_part(
            client, boundary, "ui_state", ui_state);
    }
    if (err == ESP_OK && request->has_battery_percent)
    {
        snprintf(battery_text, sizeof(battery_text), "%d",
                 request->battery_percent);
        err = memory_watch_voice_client_write_text_part(
            client, boundary, "battery_percent", battery_text);
    }
    if (err == ESP_OK && request->has_charging)
    {
        snprintf(charging_text, sizeof(charging_text), "%s",
                 request->charging ? "true" : "false");
        err = memory_watch_voice_client_write_text_part(
            client, boundary, "charging", charging_text);
    }
    if (err == ESP_OK && request->has_rssi)
    {
        snprintf(rssi_text, sizeof(rssi_text), "%d", request->rssi);
        err = memory_watch_voice_client_write_text_part(
            client, boundary, "rssi", rssi_text);
    }
    if (err == ESP_OK && request->firmware_version != NULL &&
        request->firmware_version[0] != '\0')
    {
        err = memory_watch_voice_client_write_text_part(
            client, boundary, "firmware_version",
            request->firmware_version);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_finish(client, boundary);
    }
    return err;
}

static void memory_watch_voice_client_copy_text(
    char *dst, size_t dst_len, const char *src)
{
    if (dst_len == 0U)
    {
        return;
    }
    if (src == NULL)
    {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_len - 1U);
    dst[dst_len - 1U] = '\0';
}

static esp_err_t memory_watch_voice_client_copy_json_string(
    const cJSON *root, const char *key, char *dst, size_t dst_len,
    bool allow_truncate)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(item) || item->valuestring == NULL)
    {
        return ESP_FAIL;
    }
    if (!allow_truncate && strlen(item->valuestring) >= dst_len)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    memory_watch_voice_client_copy_text(dst, dst_len, item->valuestring);
    return ESP_OK;
}

static esp_err_t memory_watch_voice_client_copy_json_nullable_string(
    const cJSON *root, const char *key, char *dst, size_t dst_len,
    bool allow_truncate)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsNull(item))
    {
        memory_watch_voice_client_copy_text(dst, dst_len, "");
        return ESP_OK;
    }
    if (!cJSON_IsString(item) || item->valuestring == NULL)
    {
        return ESP_FAIL;
    }
    if (!allow_truncate && strlen(item->valuestring) >= dst_len)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    memory_watch_voice_client_copy_text(dst, dst_len, item->valuestring);
    return ESP_OK;
}

static bool memory_watch_voice_client_string_in_list(
    const char *value, const char *const *allowed, size_t allowed_count)
{
    for (size_t i = 0; i < allowed_count; ++i)
    {
        if (strcmp(value, allowed[i]) == 0)
        {
            return true;
        }
    }
    return false;
}

static bool memory_watch_voice_client_is_status_allowed(const char *status)
{
    static const char *const allowed[] = {
        "done",
        "error",
        "timeout",
        "canceled",
    };
    return memory_watch_voice_client_string_in_list(
        status, allowed, sizeof(allowed) / sizeof(allowed[0]));
}

static bool memory_watch_voice_client_is_action_allowed(const char *action)
{
    static const char *const allowed[] = {
        "memory_saved",
        "reminder_created",
        "question_answered",
        "clarification_needed",
        "no_action",
        "error",
    };
    return memory_watch_voice_client_string_in_list(
        action, allowed, sizeof(allowed) / sizeof(allowed[0]));
}

static bool memory_watch_voice_client_is_health_status_allowed(
    const char *status)
{
    static const char *const allowed[] = {
        "ok",
        "error",
    };
    return memory_watch_voice_client_string_in_list(
        status, allowed, sizeof(allowed) / sizeof(allowed[0]));
}

static bool memory_watch_voice_client_is_hermes_status_allowed(
    const char *status)
{
    static const char *const allowed[] = {
        "online",
        "offline",
    };
    return memory_watch_voice_client_string_in_list(
        status, allowed, sizeof(allowed) / sizeof(allowed[0]));
}

static esp_err_t memory_watch_voice_client_parse_health(
    const char *json_text, size_t json_len, const char *expected_device_id,
    memory_watch_voice_client_health_t *out_health)
{
    cJSON *root = cJSON_ParseWithLength(json_text, json_len);
    if (root == NULL)
    {
        return ESP_FAIL;
    }

    esp_err_t err = ESP_OK;
    if (!cJSON_IsObject(root) || cJSON_GetArraySize(root) != 3)
    {
        err = ESP_FAIL;
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_copy_json_string(
            root, "status", out_health->status,
            sizeof(out_health->status), false);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_copy_json_string(
            root, "hermes_status", out_health->hermes_status,
            sizeof(out_health->hermes_status), false);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_copy_json_string(
            root, "device_id", out_health->device_id,
            sizeof(out_health->device_id), false);
    }
    if (err == ESP_OK &&
        !memory_watch_voice_client_is_health_status_allowed(
            out_health->status))
    {
        err = ESP_FAIL;
    }
    if (err == ESP_OK &&
        !memory_watch_voice_client_is_hermes_status_allowed(
            out_health->hermes_status))
    {
        err = ESP_FAIL;
    }
    if (err == ESP_OK &&
        strcmp(out_health->device_id, expected_device_id) != 0)
    {
        err = ESP_FAIL;
    }

    cJSON_Delete(root);
    return err;
}

static esp_err_t memory_watch_voice_client_parse_response(
    const char *json_text, size_t json_len, const char *expected_request_id,
    memory_watch_voice_client_response_t *out_response)
{
    cJSON *root = cJSON_ParseWithLength(json_text, json_len);
    if (root == NULL)
    {
        return ESP_FAIL;
    }

    esp_err_t err = ESP_OK;
    if (!cJSON_IsObject(root) || cJSON_GetArraySize(root) != 7)
    {
        err = ESP_FAIL;
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_copy_json_string(
            root, "request_id", out_response->request_id,
            sizeof(out_response->request_id), false);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_copy_json_string(
            root, "status", out_response->status,
            sizeof(out_response->status), false);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_copy_json_string(
            root, "action", out_response->action,
            sizeof(out_response->action), false);
    }
    if (err == ESP_OK &&
        strcmp(out_response->request_id, expected_request_id) != 0)
    {
        err = ESP_FAIL;
    }
    if (err == ESP_OK &&
        !memory_watch_voice_client_is_status_allowed(out_response->status))
    {
        err = ESP_FAIL;
    }
    if (err == ESP_OK &&
        !memory_watch_voice_client_is_action_allowed(out_response->action))
    {
        err = ESP_FAIL;
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_copy_json_string(
            root, "asr_text", out_response->asr_text,
            sizeof(out_response->asr_text), true);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_copy_json_string(
            root, "reply_text", out_response->reply_text,
            sizeof(out_response->reply_text), true);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_copy_json_nullable_string(
            root, "clarification_id", out_response->clarification_id,
            sizeof(out_response->clarification_id), false);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_copy_json_nullable_string(
            root, "error_code", out_response->error_code,
            sizeof(out_response->error_code), false);
    }

    cJSON_Delete(root);
    return err;
}

static esp_err_t memory_watch_voice_client_write_all(
    esp_http_client_handle_t client, const uint8_t *data, size_t len)
{
    size_t offset = 0;
    while (offset < len)
    {
        const size_t chunk_len =
            (len - offset) > (size_t)INT_MAX ? (size_t)INT_MAX
                                             : (len - offset);
        const int written = esp_http_client_write(
            client, (const char *)(data + offset), (int)chunk_len);
        if (written <= 0)
        {
            return ESP_FAIL;
        }
        offset += (size_t)written;
    }
    return ESP_OK;
}

static esp_err_t memory_watch_voice_client_read_response(
    esp_http_client_handle_t client, char *response, size_t response_len,
    size_t *out_len)
{
    size_t offset = 0;
    while (true)
    {
        const int read = esp_http_client_read(
            client, response + offset, response_len - offset - 1U);
        if (read < 0)
        {
            return ESP_FAIL;
        }
        if (read == 0)
        {
            break;
        }
        offset += (size_t)read;
        if (offset >= response_len - 1U)
        {
            return ESP_ERR_NO_MEM;
        }
    }
    response[offset] = '\0';
    *out_len = offset;
    return ESP_OK;
}

typedef esp_err_t (*memory_watch_voice_client_http_writer_t)(
    esp_http_client_handle_t client, void *user_ctx);

static esp_err_t memory_watch_voice_client_perform_http_json(
    const memory_watch_voice_client_config_t *config,
    const char *path,
    esp_http_client_method_t method,
    const char *content_type,
    size_t body_len,
    memory_watch_voice_client_http_writer_t writer,
    void *writer_ctx,
    int *out_http_status,
    char *response,
    size_t response_size,
    size_t *out_response_len)
{
    if (body_len > (size_t)INT_MAX)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    char url[384];
    esp_err_t err = memory_watch_voice_client_build_url(
        config->base_url, path, url, sizeof(url));
    if (err != ESP_OK)
    {
        return err;
    }

    char *auth_header = NULL;
    err = memory_watch_voice_client_build_bearer(config->device_token,
                                                &auth_header);
    if (err != ESP_OK)
    {
        return err;
    }

    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = (int)memory_watch_voice_client_timeout_ms(config),
        .buffer_size = kHttpBufferSize,
        .buffer_size_tx = kHttpBufferSize,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL)
    {
        memory_watch_voice_client_free(auth_header);
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_method(client, method);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Accept", "application/json");
    if (content_type != NULL)
    {
        esp_http_client_set_header(client, "Content-Type", content_type);
    }

    err = esp_http_client_open(client, (int)body_len);
    if (err == ESP_OK && writer != NULL)
    {
        err = writer(client, writer_ctx);
    }
    if (err == ESP_OK)
    {
        const int fetch_result = esp_http_client_fetch_headers(client);
        if (fetch_result < 0)
        {
            err = ESP_FAIL;
        }
        *out_http_status = esp_http_client_get_status_code(client);
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_read_response(
            client, response, response_size, out_response_len);
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    memory_watch_voice_client_free(auth_header);
    return err;
}

typedef struct
{
    const char *boundary;
    const char *device_id;
} memory_watch_voice_client_cancel_body_t;

typedef struct
{
    const char *boundary;
    const memory_watch_voice_client_config_t *config;
    const memory_watch_voice_client_text_request_t *request;
} memory_watch_voice_client_text_body_t;

static esp_err_t memory_watch_voice_client_write_cancel_body(
    esp_http_client_handle_t client, void *user_ctx)
{
    const memory_watch_voice_client_cancel_body_t *ctx =
        (const memory_watch_voice_client_cancel_body_t *)user_ctx;
    esp_err_t err = memory_watch_voice_client_write_text_part(
        client, ctx->boundary, "device_id", ctx->device_id);
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_finish(client, ctx->boundary);
    }
    return err;
}

static esp_err_t memory_watch_voice_client_write_text_command_body(
    esp_http_client_handle_t client, void *user_ctx)
{
    const memory_watch_voice_client_text_body_t *ctx =
        (const memory_watch_voice_client_text_body_t *)user_ctx;
    return memory_watch_voice_client_write_text_body(
        client, ctx->config, ctx->request, ctx->boundary);
}

esp_err_t memory_watch_voice_client_get_health(
    const memory_watch_voice_client_config_t *config,
    memory_watch_voice_client_health_t *out_health)
{
    if (out_health == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_health, 0, sizeof(*out_health));

    esp_err_t err = memory_watch_voice_client_validate_config(config);
    if (err != ESP_OK)
    {
        out_health->transport_error = err;
        return err;
    }

    char path[256];
    err = memory_watch_voice_client_build_health_path(
        config->device_id, path, sizeof(path));
    if (err != ESP_OK)
    {
        out_health->transport_error = err;
        return err;
    }

    char *response =
        (char *)memory_watch_voice_client_alloc(kMaxResponseBytes + 1U);
    if (response == NULL)
    {
        out_health->transport_error = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }

    size_t response_len = 0;
    err = memory_watch_voice_client_perform_http_json(
        config, path, HTTP_METHOD_GET, NULL, 0U, NULL, NULL,
        &out_health->http_status, response, kMaxResponseBytes + 1U,
        &response_len);
    if (err == ESP_OK && (out_health->http_status < 200 ||
                          out_health->http_status >= 300))
    {
        err = ESP_FAIL;
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_parse_health(
            response, response_len, config->device_id, out_health);
    }
    if (err == ESP_OK &&
        (strcmp(out_health->status, "ok") != 0 ||
         strcmp(out_health->hermes_status, "online") != 0))
    {
        err = ESP_FAIL;
    }

    memory_watch_voice_client_free(response);
    out_health->transport_error = err;
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "health check failed: status=%d err=%s",
                 out_health->http_status, esp_err_to_name(err));
    }
    return err;
}

esp_err_t memory_watch_voice_client_cancel_request(
    const memory_watch_voice_client_config_t *config,
    const char *request_id,
    memory_watch_voice_client_response_t *out_response)
{
    if (out_response == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_response, 0, sizeof(*out_response));

    esp_err_t err = memory_watch_voice_client_validate_config(config);
    if (err == ESP_OK &&
        !memory_watch_voice_client_is_request_id_valid(request_id))
    {
        err = ESP_ERR_INVALID_ARG;
    }
    if (err == ESP_OK &&
        !memory_watch_voice_client_request_id_matches_device(
            request_id, config->device_id))
    {
        err = ESP_ERR_INVALID_ARG;
    }
    if (err != ESP_OK)
    {
        out_response->transport_error = err;
        return err;
    }

    char path[160];
    err = memory_watch_voice_client_build_cancel_path(
        request_id, path, sizeof(path));
    if (err != ESP_OK)
    {
        out_response->transport_error = err;
        return err;
    }

    char boundary[sizeof(kBoundaryPrefix) +
                  MEMORY_WATCH_VOICE_CLIENT_REQUEST_ID_MAX_LEN + 1U];
    const int boundary_len = snprintf(boundary, sizeof(boundary), "%s%s",
                                      kBoundaryPrefix, request_id);
    if (boundary_len < 0 || (size_t)boundary_len >= sizeof(boundary))
    {
        out_response->transport_error = ESP_ERR_INVALID_SIZE;
        return ESP_ERR_INVALID_SIZE;
    }

    char content_type[160];
    const int content_type_len = snprintf(
        content_type, sizeof(content_type),
        "multipart/form-data; boundary=%s", boundary);
    if (content_type_len < 0 ||
        (size_t)content_type_len >= sizeof(content_type))
    {
        out_response->transport_error = ESP_ERR_INVALID_SIZE;
        return ESP_ERR_INVALID_SIZE;
    }

    const size_t body_len =
        memory_watch_voice_client_text_part_len(
            boundary, "device_id", config->device_id) +
        memory_watch_voice_client_finish_len(boundary);

    memory_watch_voice_client_cancel_body_t writer_ctx = {
        .boundary = boundary,
        .device_id = config->device_id,
    };

    char *response =
        (char *)memory_watch_voice_client_alloc(kMaxResponseBytes + 1U);
    if (response == NULL)
    {
        out_response->transport_error = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }

    size_t response_len = 0;
    err = memory_watch_voice_client_perform_http_json(
        config, path, HTTP_METHOD_POST, content_type, body_len,
        memory_watch_voice_client_write_cancel_body, &writer_ctx,
        &out_response->http_status, response, kMaxResponseBytes + 1U,
        &response_len);
    if (err == ESP_OK && (out_response->http_status < 200 ||
                          out_response->http_status >= 300))
    {
        err = ESP_FAIL;
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_parse_response(
            response, response_len, request_id, out_response);
    }

    memory_watch_voice_client_free(response);
    out_response->transport_error = err;
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "cancel request failed: status=%d err=%s",
                 out_response->http_status, esp_err_to_name(err));
    }
    return err;
}

esp_err_t memory_watch_voice_client_post_voice_command(
    const memory_watch_voice_client_config_t *config,
    const memory_watch_voice_client_request_t *request,
    memory_watch_voice_client_response_t *out_response)
{
    if (out_response == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_response, 0, sizeof(*out_response));

    esp_err_t err = memory_watch_voice_client_validate_config(config);
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_validate_request(request);
    }
    if (err == ESP_OK &&
        !memory_watch_voice_client_request_id_matches_device(
            request->request_id, config->device_id))
    {
        err = ESP_ERR_INVALID_ARG;
    }
    if (err != ESP_OK)
    {
        out_response->transport_error = err;
        return err;
    }

    char url[384];
    err = memory_watch_voice_client_build_url(config->base_url,
                                             kVoiceCommandPath, url,
                                             sizeof(url));
    if (err != ESP_OK)
    {
        out_response->transport_error = err;
        return err;
    }

    char boundary[sizeof(kBoundaryPrefix) +
                  MEMORY_WATCH_VOICE_CLIENT_REQUEST_ID_MAX_LEN + 1U];
    const int boundary_len = snprintf(boundary, sizeof(boundary), "%s%s",
                                      kBoundaryPrefix, request->request_id);
    if (boundary_len < 0 || (size_t)boundary_len >= sizeof(boundary))
    {
        out_response->transport_error = ESP_ERR_INVALID_SIZE;
        return ESP_ERR_INVALID_SIZE;
    }

    size_t body_len = 0;
    err = memory_watch_voice_client_compute_body_len(config, request,
                                                    boundary, &body_len);
    if (err != ESP_OK)
    {
        out_response->transport_error = err;
        return err;
    }
    if (body_len > (size_t)INT_MAX)
    {
        out_response->transport_error = ESP_ERR_INVALID_SIZE;
        return ESP_ERR_INVALID_SIZE;
    }

    char *auth_header = NULL;
    err = memory_watch_voice_client_build_bearer(config->device_token,
                                                &auth_header);
    if (err != ESP_OK)
    {
        out_response->transport_error = err;
        return err;
    }

    char content_type[160];
    const int content_type_len = snprintf(
        content_type, sizeof(content_type),
        "multipart/form-data; boundary=%s", boundary);
    if (content_type_len < 0 ||
        (size_t)content_type_len >= sizeof(content_type))
    {
        memory_watch_voice_client_free(auth_header);
        out_response->transport_error = ESP_ERR_INVALID_SIZE;
        return ESP_ERR_INVALID_SIZE;
    }

    char *response =
        (char *)memory_watch_voice_client_alloc(kMaxResponseBytes + 1U);
    if (response == NULL)
    {
        memory_watch_voice_client_free(auth_header);
        out_response->transport_error = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = (int)memory_watch_voice_client_timeout_ms(config),
        .buffer_size = kHttpBufferSize,
        .buffer_size_tx = kHttpBufferSize,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL)
    {
        memory_watch_voice_client_free(response);
        memory_watch_voice_client_free(auth_header);
        out_response->transport_error = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", content_type);
    esp_http_client_set_header(client, "Accept", "application/json");

    err = esp_http_client_open(client, (int)body_len);
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_write_body(client, config, request,
                                                  boundary);
    }
    if (err == ESP_OK)
    {
        const int fetch_result = esp_http_client_fetch_headers(client);
        if (fetch_result < 0)
        {
            err = ESP_FAIL;
        }
        out_response->http_status = esp_http_client_get_status_code(client);
    }
    size_t response_len = 0;
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_read_response(
            client, response, kMaxResponseBytes + 1U, &response_len);
    }
    if (err == ESP_OK && (out_response->http_status < 200 ||
                          out_response->http_status >= 300))
    {
        err = ESP_FAIL;
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_parse_response(
            response, response_len, request->request_id, out_response);
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    memory_watch_voice_client_free(response);
    memory_watch_voice_client_free(auth_header);

    out_response->transport_error = err;
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "voice command failed: status=%d err=%s",
                 out_response->http_status, esp_err_to_name(err));
    }
    return err;
}

esp_err_t memory_watch_voice_client_post_text_command(
    const memory_watch_voice_client_config_t *config,
    const memory_watch_voice_client_text_request_t *request,
    memory_watch_voice_client_response_t *out_response)
{
    if (out_response == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_response, 0, sizeof(*out_response));

    esp_err_t err = memory_watch_voice_client_validate_config(config);
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_validate_text_request(request);
    }
    if (err == ESP_OK &&
        !memory_watch_voice_client_request_id_matches_device(
            request->request_id, config->device_id))
    {
        err = ESP_ERR_INVALID_ARG;
    }
    if (err != ESP_OK)
    {
        out_response->transport_error = err;
        return err;
    }

    char boundary[sizeof(kBoundaryPrefix) +
                  MEMORY_WATCH_VOICE_CLIENT_REQUEST_ID_MAX_LEN + 1U];
    const int boundary_len = snprintf(boundary, sizeof(boundary), "%s%s",
                                      kBoundaryPrefix, request->request_id);
    if (boundary_len < 0 || (size_t)boundary_len >= sizeof(boundary))
    {
        out_response->transport_error = ESP_ERR_INVALID_SIZE;
        return ESP_ERR_INVALID_SIZE;
    }

    size_t body_len = 0;
    err = memory_watch_voice_client_compute_text_body_len(
        config, request, boundary, &body_len);
    if (err != ESP_OK)
    {
        out_response->transport_error = err;
        return err;
    }

    char content_type[160];
    const int content_type_len = snprintf(
        content_type, sizeof(content_type),
        "multipart/form-data; boundary=%s", boundary);
    if (content_type_len < 0 ||
        (size_t)content_type_len >= sizeof(content_type))
    {
        out_response->transport_error = ESP_ERR_INVALID_SIZE;
        return ESP_ERR_INVALID_SIZE;
    }

    char *response =
        (char *)memory_watch_voice_client_alloc(kMaxResponseBytes + 1U);
    if (response == NULL)
    {
        out_response->transport_error = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }

    memory_watch_voice_client_text_body_t writer_ctx = {
        .boundary = boundary,
        .config = config,
        .request = request,
    };
    size_t response_len = 0;
    err = memory_watch_voice_client_perform_http_json(
        config, kTextCommandPath, HTTP_METHOD_POST, content_type, body_len,
        memory_watch_voice_client_write_text_command_body, &writer_ctx,
        &out_response->http_status, response, kMaxResponseBytes + 1U,
        &response_len);
    if (err == ESP_OK && (out_response->http_status < 200 ||
                          out_response->http_status >= 300))
    {
        err = ESP_FAIL;
    }
    if (err == ESP_OK)
    {
        err = memory_watch_voice_client_parse_response(
            response, response_len, request->request_id, out_response);
    }

    memory_watch_voice_client_free(response);
    out_response->transport_error = err;
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "text command failed: status=%d err=%s",
                 out_response->http_status, esp_err_to_name(err));
    }
    return err;
}
