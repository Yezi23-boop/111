#include "music_http_client.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "mbedtls/base64.h"
static const size_t kResponseBytes = 4096U;
static const size_t kQrResponseBytes = 8192U;

struct music_http_stream
{
    esp_http_client_handle_t client;
};

static bool music_http_is_insecure_allowed(
    const music_http_client_config_t *config)
{
    return config->allow_insecure_http ||
           strncmp(config->base_url, "https://", 8U) == 0;
}

static esp_err_t music_http_build_url(const music_http_client_config_t *config,
                                      const char *path, char *url,
                                      size_t url_size)
{
    if (config == NULL || path == NULL || url == NULL ||
        config->base_url[0] == '\0' || config->device_id[0] == '\0' ||
        config->device_token[0] == '\0')
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!music_http_is_insecure_allowed(config))
    {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t base_len = strlen(config->base_url);
    const bool has_slash = base_len > 0U && config->base_url[base_len - 1U] == '/';
    /* music-service 以 query 中的设备 ID 决定设备白名单；所有请求统一携带。 */
    const char *const query_separator = strchr(path, '?') != NULL ? "&" : "?";
    const int written = snprintf(url, url_size, "%s%s%s%sdevice_id=%s",
                                 config->base_url, has_slash ? "" : "/",
                                 path[0] == '/' ? path + 1 : path,
                                 query_separator, config->device_id);
    return written > 0 && (size_t)written < url_size ? ESP_OK
                                                       : ESP_ERR_INVALID_SIZE;
}

static esp_err_t music_http_set_auth(esp_http_client_handle_t client,
                                      const char *token)
{
    char authorization[MUSIC_SERVICE_DEVICE_TOKEN_MAX_BYTES + 8U];
    const int written = snprintf(authorization, sizeof(authorization),
                                 "Bearer %s", token);
    if (written <= 0 || (size_t)written >= sizeof(authorization))
    {
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t ret = esp_http_client_set_header(client, "Authorization",
                                               authorization);
    if (ret == ESP_OK)
    {
        ret = esp_http_client_set_header(client, "Accept", "application/json");
    }
    return ret;
}

static esp_err_t music_http_read_json_response(esp_http_client_handle_t client,
                                               char *response,
                                               size_t response_capacity,
                                               size_t *out_length)
{
    size_t offset = 0U;
    while (true)
    {
        if (offset + 1U >= response_capacity)
        {
            return ESP_ERR_INVALID_SIZE;
        }
        const int read = esp_http_client_read(
            client, response + offset, (int)(response_capacity - offset - 1U));
        if (read < 0)
        {
            return ESP_FAIL;
        }
        if (read == 0)
        {
            break;
        }
        offset += (size_t)read;
    }
    response[offset] = '\0';
    if (out_length != NULL)
    {
        *out_length = offset;
    }
    return ESP_OK;
}

static void music_http_copy_json_string(const cJSON *root, const char *key,
                                         char *output, size_t output_size)
{
    if (output == NULL || output_size == 0U)
    {
        return;
    }
    output[0] = '\0';
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(item) && item->valuestring != NULL)
    {
        snprintf(output, output_size, "%s", item->valuestring);
    }
}

static music_service_state_t music_http_parse_state(const cJSON *item)
{
    if (!cJSON_IsString(item) || item->valuestring == NULL)
    {
        return MUSIC_SERVICE_STATE_ERROR;
    }
    if (strcmp(item->valuestring, "buffering") == 0)
        return MUSIC_SERVICE_STATE_BUFFERING;
    if (strcmp(item->valuestring, "playing") == 0 ||
        strcmp(item->valuestring, "streaming") == 0)
        return MUSIC_SERVICE_STATE_PLAYING;
    if (strcmp(item->valuestring, "paused") == 0)
        return MUSIC_SERVICE_STATE_PAUSED;
    if (strcmp(item->valuestring, "stopped") == 0)
        return MUSIC_SERVICE_STATE_STOPPED;
    return MUSIC_SERVICE_STATE_ERROR;
}

static music_service_mode_t music_http_parse_mode(const cJSON *item)
{
    if (cJSON_IsString(item) && item->valuestring != NULL)
    {
        if (strcmp(item->valuestring, "repeat_one") == 0)
            return MUSIC_SERVICE_MODE_REPEAT_ONE;
        if (strcmp(item->valuestring, "shuffle") == 0)
            return MUSIC_SERVICE_MODE_SHUFFLE;
    }
    return MUSIC_SERVICE_MODE_REPEAT_ALL;
}

static esp_err_t music_http_parse_session(const char *payload, int status,
                                          music_http_session_result_t *result)
{
    cJSON *root = cJSON_Parse(payload);
    if (root == NULL)
    {
        return ESP_ERR_INVALID_RESPONSE;
    }
    result->http_status = status;
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "state");
    result->state = music_http_parse_state(state);
    result->mode = music_http_parse_mode(
        cJSON_GetObjectItemCaseSensitive(root, "mode"));
    const cJSON *position = cJSON_GetObjectItemCaseSensitive(root, "position_ms");
    result->position_ms = cJSON_IsNumber(position) && position->valuedouble >= 0
                              ? (uint32_t)position->valuedouble
                              : 0U;
    music_http_copy_json_string(root, "music_session_id",
                                result->music_session_id,
                                sizeof(result->music_session_id));
    music_http_copy_json_string(root, "stream_id", result->stream_id,
                                sizeof(result->stream_id));
    music_http_copy_json_string(root, "source_id", result->source_id,
                                sizeof(result->source_id));
    music_http_copy_json_string(root, "track_id", result->track_id,
                                sizeof(result->track_id));
    music_http_copy_json_string(root, "error_code", result->error_code,
                                sizeof(result->error_code));
    const cJSON *track = cJSON_GetObjectItemCaseSensitive(root, "track");
    if (cJSON_IsObject(track))
    {
        music_http_copy_json_string(track, "track_id", result->track_id,
                                    sizeof(result->track_id));
        music_http_copy_json_string(track, "title", result->title,
                                    sizeof(result->title));
        music_http_copy_json_string(track, "artist", result->artist,
                                    sizeof(result->artist));
    }
    cJSON_Delete(root);
    return status >= 200 && status < 300 && result->state != MUSIC_SERVICE_STATE_ERROR
               ? ESP_OK
               : ESP_FAIL;
}

static esp_err_t music_http_perform_json(
    const music_http_client_config_t *config, const char *path,
    esp_http_client_method_t method, const char *body, const char *command_id,
    music_http_session_result_t *out_result)
{
    char url[384];
    esp_err_t ret = music_http_build_url(config, path, url, sizeof(url));
    if (ret != ESP_OK)
    {
        return ret;
    }

    char *response = heap_caps_calloc(1U, kResponseBytes,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (response == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t http_config = {
        .url = url,
        .method = method,
        .timeout_ms = config->timeout_ms > 0U ? (int)config->timeout_ms : 10000,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
        .keep_alive_enable = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL)
    {
        heap_caps_free(response);
        return ESP_ERR_NO_MEM;
    }
    ret = music_http_set_auth(client, config->device_token);
    if (ret == ESP_OK)
    {
        ret = esp_http_client_set_header(client, "Content-Type",
                                         "application/json");
    }
    if (ret == ESP_OK && command_id != NULL && command_id[0] != '\0')
    {
        ret = esp_http_client_set_header(client, "X-Command-Id", command_id);
    }
    const size_t body_len = body == NULL ? 0U : strlen(body);
    if (ret == ESP_OK)
    {
        ret = esp_http_client_open(client, body_len);
    }
    if (ret == ESP_OK && body_len > 0U)
    {
        ret = esp_http_client_write(client, body, (int)body_len) ==
                      (int)body_len
                  ? ESP_OK
                  : ESP_FAIL;
    }
    int status = 0;
    if (ret == ESP_OK)
    {
        (void)esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        size_t response_len = 0U;
        ret = music_http_read_json_response(client, response,
                                             kResponseBytes, &response_len);
        if (ret == ESP_OK && out_result != NULL)
        {
            memset(out_result, 0, sizeof(*out_result));
            out_result->transport_error = ESP_OK;
            ret = music_http_parse_session(response, status, out_result);
        }
    }
    if (out_result != NULL)
    {
        out_result->http_status = status;
        out_result->transport_error = ret;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    heap_caps_free(response);
    return ret;
}

static music_service_account_state_t music_http_parse_account_state(
    const cJSON *item)
{
    if (!cJSON_IsString(item) || item->valuestring == NULL)
    {
        return MUSIC_SERVICE_ACCOUNT_ERROR;
    }
    if (strcmp(item->valuestring, "logged_in") == 0)
        return MUSIC_SERVICE_ACCOUNT_LOGGED_IN;
    if (strcmp(item->valuestring, "logged_out") == 0)
        return MUSIC_SERVICE_ACCOUNT_LOGGED_OUT;
    if (strcmp(item->valuestring, "qr_pending") == 0)
        return MUSIC_SERVICE_ACCOUNT_QR_PENDING;
    if (strcmp(item->valuestring, "qr_confirming") == 0)
        return MUSIC_SERVICE_ACCOUNT_QR_CONFIRMING;
    if (strcmp(item->valuestring, "expired") == 0)
        return MUSIC_SERVICE_ACCOUNT_EXPIRED;
    return MUSIC_SERVICE_ACCOUNT_ERROR;
}

static esp_err_t music_http_parse_account(const char *payload, int status,
                                          music_http_account_result_t *result)
{
    cJSON *root = cJSON_Parse(payload);
    if (root == NULL)
    {
        return ESP_ERR_INVALID_RESPONSE;
    }
    result->http_status = status;
    result->state = music_http_parse_account_state(
        cJSON_GetObjectItemCaseSensitive(root, "state"));
    const cJSON *expires = cJSON_GetObjectItemCaseSensitive(root, "expires_at");
    if (cJSON_IsNumber(expires) && expires->valuedouble >= 0.0)
    {
        result->expires_at_ms = (uint64_t)expires->valuedouble;
    }
    music_http_copy_json_string(root, "login_id", result->login_id,
                                sizeof(result->login_id));
    music_http_copy_json_string(root, "error_code", result->error_code,
                                sizeof(result->error_code));
    const cJSON *qr = cJSON_GetObjectItemCaseSensitive(root, "qr");
    const cJSON *size = cJSON_IsObject(qr)
                           ? cJSON_GetObjectItemCaseSensitive(qr, "size")
                           : NULL;
    const cJSON *data = cJSON_IsObject(qr)
                            ? cJSON_GetObjectItemCaseSensitive(qr, "data")
                            : NULL;
    if (cJSON_IsNumber(size) && size->valuedouble > 0.0 &&
        size->valuedouble <= 255.0 && cJSON_IsString(data) &&
        data->valuestring != NULL && result->qr_data != NULL)
    {
        const uint16_t qr_size = (uint16_t)size->valuedouble;
        /* QR 模块按位打包；拒绝不完整数据，避免 UI 静默绘制截断二维码。 */
        const size_t qr_required_bytes =
            ((size_t)qr_size * (size_t)qr_size + 7U) / 8U;
        if ((double)qr_size != size->valuedouble ||
            qr_required_bytes > result->qr_capacity)
        {
            cJSON_Delete(root);
            return ESP_ERR_INVALID_RESPONSE;
        }
        size_t decoded = result->qr_capacity;
        const int ret = mbedtls_base64_decode(
            result->qr_data, result->qr_capacity, &decoded,
            (const unsigned char *)data->valuestring,
            strlen(data->valuestring));
        if (ret != 0 || decoded < qr_required_bytes)
        {
            cJSON_Delete(root);
            return ESP_ERR_INVALID_RESPONSE;
        }
        result->qr_size = qr_size;
        result->qr_bytes = decoded;
    }
    cJSON_Delete(root);
    if (result->state == MUSIC_SERVICE_ACCOUNT_ERROR)
    {
        return status == 409 || status == 410 ? ESP_ERR_INVALID_STATE : ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t music_http_request_account(
    const music_http_client_config_t *config, const char *path,
    esp_http_client_method_t method, music_http_account_result_t *result)
{
    if (config == NULL || path == NULL || result == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    char url[480];
    esp_err_t ret = music_http_build_url(config, path, url, sizeof(url));
    if (ret != ESP_OK)
    {
        return ret;
    }
    char *response = heap_caps_calloc(1U, kQrResponseBytes,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (response == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    const uint8_t *qr_data = result->qr_data;
    const size_t qr_capacity = result->qr_capacity;
    memset(result, 0, sizeof(*result));
    result->qr_data = (uint8_t *)qr_data;
    result->qr_capacity = qr_capacity;
    esp_http_client_config_t http_config = {
        .url = url,
        .method = method,
        .timeout_ms = config->timeout_ms > 0U ? (int)config->timeout_ms : 10000,
        .buffer_size = 4096,
        .keep_alive_enable = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL)
    {
        heap_caps_free(response);
        return ESP_ERR_NO_MEM;
    }
    ret = music_http_set_auth(client, config->device_token);
    if (ret == ESP_OK && method == HTTP_METHOD_POST)
    {
        ret = esp_http_client_set_header(client, "Content-Type",
                                         "application/json");
    }
    if (ret == ESP_OK)
    {
        ret = esp_http_client_open(client, 0);
    }
    int status = 0;
    if (ret == ESP_OK)
    {
        (void)esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        ret = music_http_read_json_response(client, response,
                                             kQrResponseBytes, NULL);
    }
    if (ret == ESP_OK)
    {
        ret = music_http_parse_account(response, status, result);
    }
    result->http_status = status;
    result->transport_error = ret;
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    heap_caps_free(response);
    return ret;
}

esp_err_t music_http_client_get_account(
    const music_http_client_config_t *config,
    music_http_account_result_t *out_result)
{
    return music_http_request_account(config, "/v1/music/account",
                                      HTTP_METHOD_GET, out_result);
}

esp_err_t music_http_client_create_qr(
    const music_http_client_config_t *config,
    music_http_account_result_t *out_result)
{
    return music_http_request_account(config, "/v1/music/account/qr",
                                      HTTP_METHOD_POST, out_result);
}

esp_err_t music_http_client_poll_qr(
    const music_http_client_config_t *config, const char *login_id,
    music_http_account_result_t *out_result)
{
    if (login_id == NULL || login_id[0] == '\0')
    {
        return ESP_ERR_INVALID_ARG;
    }
    char path[320];
    const int written = snprintf(path, sizeof(path),
                                 "/v1/music/account/qr/%s", login_id);
    if (written <= 0 || (size_t)written >= sizeof(path))
    {
        return ESP_ERR_INVALID_SIZE;
    }
    return music_http_request_account(config, path, HTTP_METHOD_GET,
                                      out_result);
}

static esp_err_t music_http_parse_catalog(const char *payload, int status,
                                          const char *source_id,
                                          uint32_t offset,
                                          music_service_catalog_snapshot_t *out)
{
    cJSON *root = cJSON_Parse(payload);
    if (root == NULL)
    {
        return ESP_ERR_INVALID_RESPONSE;
    }
    memset(out, 0, sizeof(*out));
    out->offset = offset;
    snprintf(out->source_id, sizeof(out->source_id), "%s",
             source_id != NULL ? source_id : "");
    const cJSON *total = cJSON_GetObjectItemCaseSensitive(root, "total");
    if (cJSON_IsNumber(total) && total->valuedouble >= 0.0)
    {
        out->total = (uint32_t)total->valuedouble;
    }
    const cJSON *tracks = cJSON_GetObjectItemCaseSensitive(root, "tracks");
    if (cJSON_IsArray(tracks))
    {
        const cJSON *item = NULL;
        cJSON_ArrayForEach(item, tracks)
        {
            if (out->track_count >= MUSIC_SERVICE_CATALOG_PAGE_SIZE ||
                !cJSON_IsObject(item))
            {
                break;
            }
            music_service_catalog_track_t *track =
                &out->tracks[out->track_count];
            music_http_copy_json_string(
                item, "track_id", track->track_id, sizeof(track->track_id));
            music_http_copy_json_string(item, "title", track->title,
                                        sizeof(track->title));
            music_http_copy_json_string(item, "artist", track->artist,
                                        sizeof(track->artist));
            if (track->track_id[0] != '\0')
            {
                ++out->track_count;
            }
        }
    }
    cJSON_Delete(root);
    out->valid = status >= 200 && status < 300;
    return out->valid ? ESP_OK : ESP_FAIL;
}

static esp_err_t music_http_build_session_body(
    const music_http_client_config_t *config, const char *source_id,
    const char *track_id, const char *mode, char **out_body)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "device_id", config->device_id);
    if (source_id != NULL)
        cJSON_AddStringToObject(root, "source_id", source_id);
    if (track_id != NULL)
        cJSON_AddStringToObject(root, "track_id", track_id);
    if (mode != NULL)
        cJSON_AddStringToObject(root, "mode", mode);
    *out_body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return *out_body == NULL ? ESP_ERR_NO_MEM : ESP_OK;
}

esp_err_t music_http_client_create_session(
    const music_http_client_config_t *config, const char *source_id,
    const char *track_id, const char *command_id,
    music_http_session_result_t *out_result)
{
    if (config == NULL || out_result == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    char *body = NULL;
    esp_err_t ret = music_http_build_session_body(config, source_id, track_id,
                                                  NULL, &body);
    if (ret == ESP_OK)
    {
        ret = music_http_perform_json(config, "/v1/music/sessions",
                                      HTTP_METHOD_POST, body, command_id,
                                      out_result);
    }
    cJSON_free(body);
    return ret;
}

esp_err_t music_http_client_fetch_tracks(
    const music_http_client_config_t *config, const char *source_id,
    uint32_t offset, music_service_catalog_snapshot_t *out_catalog)
{
    if (config == NULL || source_id == NULL || source_id[0] == '\0' ||
        out_catalog == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    char path[384];
    const int written = snprintf(
        path, sizeof(path), "/v1/music/sources/%s/tracks?device_id=%s&offset=%lu&limit=%u",
        source_id, config->device_id, (unsigned long)offset,
        (unsigned)MUSIC_SERVICE_CATALOG_PAGE_SIZE);
    if (written <= 0 || (size_t)written >= sizeof(path))
    {
        return ESP_ERR_INVALID_SIZE;
    }
    char url[480];
    esp_err_t ret = music_http_build_url(config, path, url, sizeof(url));
    if (ret != ESP_OK)
    {
        return ret;
    }
    char *response = heap_caps_calloc(1U, kResponseBytes,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (response == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t http_config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = config->timeout_ms > 0U ? (int)config->timeout_ms : 10000,
        .buffer_size = 4096,
        .keep_alive_enable = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL)
    {
        heap_caps_free(response);
        return ESP_ERR_NO_MEM;
    }
    ret = music_http_set_auth(client, config->device_token);
    if (ret == ESP_OK)
    {
        ret = esp_http_client_set_header(client, "Accept", "application/json");
    }
    if (ret == ESP_OK)
    {
        ret = esp_http_client_open(client, 0);
    }
    int status = 0;
    size_t response_len = 0U;
    if (ret == ESP_OK)
    {
        (void)esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        ret = music_http_read_json_response(client, response, kResponseBytes,
                                            &response_len);
    }
    if (ret == ESP_OK)
    {
        ret = music_http_parse_catalog(response, status, source_id, offset,
                                       out_catalog);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    heap_caps_free(response);
    return ret;
}

esp_err_t music_http_client_session_command(
    const music_http_client_config_t *config, const char *session_id,
    const char *action, const char *mode, const char *command_id,
    music_http_session_result_t *out_result)
{
    if (config == NULL || session_id == NULL || session_id[0] == '\0' ||
        action == NULL || action[0] == '\0' || out_result == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(action, "pause") != 0 && strcmp(action, "resume") != 0 &&
        strcmp(action, "previous") != 0 && strcmp(action, "next") != 0 &&
        strcmp(action, "mode") != 0 && strcmp(action, "destroy") != 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    char path[320];
    const int written = strcmp(action, "destroy") == 0
                            ? snprintf(path, sizeof(path),
                                       "/v1/music/sessions/%s?device_id=%s",
                                       session_id, config->device_id)
                            : snprintf(path, sizeof(path),
                                       "/v1/music/sessions/%s/%s?device_id=%s",
                                       session_id, action, config->device_id);
    if (written <= 0 || (size_t)written >= sizeof(path))
    {
        return ESP_ERR_INVALID_SIZE;
    }
    char *body = NULL;
    esp_err_t ret = music_http_build_session_body(config, NULL, NULL, mode,
                                                  &body);
    if (ret == ESP_OK)
    {
        ret = music_http_perform_json(
            config, path, strcmp(action, "destroy") == 0 ? HTTP_METHOD_DELETE
                                                          : HTTP_METHOD_POST,
            body, command_id, out_result);
    }
    cJSON_free(body);
    return ret;
}

esp_err_t music_http_client_open_stream(
    const music_http_client_config_t *config, const char *stream_id,
    music_http_stream_t **out_stream)
{
    if (config == NULL || stream_id == NULL || stream_id[0] == '\0' ||
        out_stream == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    *out_stream = NULL;
    char path[320];
    const int written = snprintf(path, sizeof(path),
                                 "/v1/music/streams/%s?device_id=%s",
                                 stream_id, config->device_id);
    if (written <= 0 || (size_t)written >= sizeof(path))
    {
        return ESP_ERR_INVALID_SIZE;
    }
    char url[384];
    esp_err_t ret = music_http_build_url(config, path, url, sizeof(url));
    if (ret != ESP_OK)
    {
        return ret;
    }
    music_http_stream_t *stream = heap_caps_calloc(
        1U, sizeof(*stream), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (stream == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t http_config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = config->timeout_ms > 0U ? (int)config->timeout_ms : 5000,
        .buffer_size = 4096,
        .keep_alive_enable = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    stream->client = esp_http_client_init(&http_config);
    if (stream->client == NULL)
    {
        heap_caps_free(stream);
        return ESP_ERR_NO_MEM;
    }
    ret = music_http_set_auth(stream->client, config->device_token);
    if (ret == ESP_OK)
    {
        ret = esp_http_client_set_header(stream->client, "Accept", "audio/mpeg");
    }
    if (ret == ESP_OK)
    {
        ret = esp_http_client_open(stream->client, 0);
    }
    if (ret == ESP_OK)
    {
        (void)esp_http_client_fetch_headers(stream->client);
        if (esp_http_client_get_status_code(stream->client) != 200)
        {
            ret = ESP_FAIL;
        }
    }
    if (ret != ESP_OK)
    {
        music_http_client_close_stream(stream);
        return ret;
    }
    *out_stream = stream;
    return ESP_OK;
}

esp_err_t music_http_client_read_stream(music_http_stream_t *stream,
                                         uint8_t *buffer, size_t capacity,
                                         size_t *out_bytes)
{
    if (out_bytes != NULL)
    {
        *out_bytes = 0U;
    }
    if (stream == NULL || stream->client == NULL || buffer == NULL ||
        capacity == 0U || out_bytes == NULL || capacity > (size_t)INT_MAX)
    {
        return ESP_ERR_INVALID_ARG;
    }
    const int read = esp_http_client_read(stream->client, (char *)buffer,
                                          (int)capacity);
    if (read < 0)
    {
        return ESP_FAIL;
    }
    if (read == 0)
    {
        return ESP_ERR_NOT_FOUND;
    }
    *out_bytes = (size_t)read;
    return ESP_OK;
}

void music_http_client_close_stream(music_http_stream_t *stream)
{
    if (stream == NULL)
    {
        return;
    }
    if (stream->client != NULL)
    {
        esp_http_client_close(stream->client);
        esp_http_client_cleanup(stream->client);
    }
    heap_caps_free(stream);
}
