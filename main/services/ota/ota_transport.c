#include "services/ota/ota_transport.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/md5.h"
#include "mbedtls/sha256.h"

static const char *TAG = "ota_transport";
static esp_https_ota_handle_t s_staged_handle = NULL;
static const size_t kOtaSlotMaxBytes = 0xC00000U;
/* 缓冲必须位于片内 RAM，4 KiB 可避免在低剩余内存时挤压 Wi-Fi/lwIP/TLS。 */
static const int kOtaDownloadBufferBytes = 4 * 1024;
/* Cloudflare 长响应在弱网下可能停在 TLS 空读；按 Range 取得 256 KiB 可让每次
 * HTTP 传输有界，同时避免 11 MiB 镜像因过多小请求超过 OTA 测试时限。
 * 服务端已声明 Accept-Ranges，且不需要额外 Flash 分区。 */
static const int kOtaPartialRequestBytes = 256 * 1024;
/* 连续 60 秒没有收到新固件字节则结束本次传输，避免 TLS EAGAIN 无限循环。 */
static const int64_t kOtaNoProgressTimeoutUs = 60LL * 1000LL * 1000LL;
static const uint32_t kOtaReconnectMax = 3U;
static const uint32_t kOtaReconnectDelayMs = 5000U;
static const time_t kMinimumTlsTime = 1577836800; /* 2020-01-01 UTC。 */
static const char *s_download_authorization = NULL;

static esp_err_t ota_transport_http_client_init_cb(
    esp_http_client_handle_t client)
{
    if (s_download_authorization == NULL ||
        s_download_authorization[0] == '\0')
    {
        return ESP_OK;
    }
    return esp_http_client_set_header(client, "Authorization",
                                      s_download_authorization);
}

static bool ota_transport_tls_configured(const char *root_ca_pem,
                                         bool use_cert_bundle)
{
    return use_cert_bundle || (root_ca_pem != NULL && root_ca_pem[0] != '\0');
}

static bool ota_transport_fault_reached(ota_transport_fault_mode_t mode,
                                        size_t received, size_t total)
{
    size_t threshold = 0U;
    switch (mode)
    {
    case OTA_TRANSPORT_FAULT_ABORT_AT_20_PERCENT:
        threshold = 20U;
        break;
    case OTA_TRANSPORT_FAULT_ABORT_AT_50_PERCENT:
        threshold = 50U;
        break;
    case OTA_TRANSPORT_FAULT_ABORT_AT_90_PERCENT:
        threshold = 90U;
        break;
    case OTA_TRANSPORT_FAULT_NONE:
    default:
        return false;
    }
    return total > 0U && received >= (total * threshold) / 100U;
}

static bool ota_transport_is_retryable_download_error(esp_err_t error)
{
    return error == ESP_FAIL || error == ESP_ERR_HTTP_CONNECT ||
           error == ESP_ERR_TIMEOUT;
}

static bool ota_transport_is_https_url(const char *url)
{
    return url != NULL && strncmp(url, "https://", 8U) == 0 &&
           url[8] != '\0';
}

static esp_err_t ota_transport_begin_download_session(
    const ota_transport_manifest_t *manifest,
    const ota_transport_download_config_t *config, size_t resume_bytes,
    esp_https_ota_handle_t *out_handle, const esp_partition_t **out_target)
{
    esp_http_client_config_t http_config = {
        .url = manifest->url,
        .cert_pem = config->use_cert_bundle ? NULL : config->root_ca_pem,
        .crt_bundle_attach = config->use_cert_bundle ? esp_crt_bundle_attach : NULL,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
        .buffer_size = kOtaDownloadBufferBytes,
    };
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
        .http_client_init_cb = config->authorization == NULL
                                   ? NULL
                                   : ota_transport_http_client_init_cb,
        /* OneNET Fuse OTA rejects HEAD with 406; its full GET still supports
         * Range for reconnects, so only the OneNET path skips ESP-IDF's HEAD
         * probe while the self-hosted manifest keeps partial downloads. */
        .partial_http_download = config->authorization == NULL,
        .max_http_request_size = kOtaPartialRequestBytes,
        .buffer_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT,
        .ota_resumption = resume_bytes >= (size_t)kOtaDownloadBufferBytes,
        .ota_image_bytes_written = resume_bytes,
    };

    esp_https_ota_handle_t handle = NULL;
    s_download_authorization = config->authorization;
    esp_err_t ret = esp_https_ota_begin(&ota_config, &handle);
    s_download_authorization = NULL;
    if (ret != ESP_OK)
    {
        return ret;
    }

    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    const int content_length = esp_https_ota_get_image_size(handle);
    if (target == NULL || content_length <= 0 ||
        (size_t)content_length != manifest->size ||
        manifest->size > target->size)
    {
        (void)esp_https_ota_abort(handle);
        return ESP_ERR_INVALID_SIZE;
    }

    *out_handle = handle;
    *out_target = target;
    return ESP_OK;
}

static bool ota_transport_host_matches(const char *url, const char *allowed)
{
    if (allowed == NULL || allowed[0] == '\0')
    {
        return false;
    }

    const char *host_start = url + 8U;
    const char *host_end = strpbrk(host_start, "/?#");
    const size_t host_len = host_end == NULL
                                ? strlen(host_start)
                                : (size_t)(host_end - host_start);
    return strlen(allowed) == host_len &&
           strncmp(host_start, allowed, host_len) == 0;
}

static bool ota_transport_is_hex_sha256(const char *sha256)
{
    if (sha256 == NULL || strlen(sha256) != OTA_TRANSPORT_SHA256_HEX_LEN)
    {
        return false;
    }
    for (size_t i = 0; i < OTA_TRANSPORT_SHA256_HEX_LEN; ++i)
    {
        if (!isxdigit((unsigned char)sha256[i]))
        {
            return false;
        }
    }
    return true;
}

static unsigned int ota_transport_version_component(const char **cursor)
{
    unsigned int value = 0U;
    const char *p = *cursor;
    while (*p != '\0' && isdigit((unsigned char)*p))
    {
        value = value * 10U + (unsigned int)(*p - '0');
        ++p;
    }
    *cursor = p;
    return value;
}

static bool ota_transport_version_is_newer(const char *current,
                                           const char *candidate)
{
    if (current == NULL || current[0] == '\0' || candidate == NULL ||
        candidate[0] == '\0')
    {
        return false;
    }

    const char *current_cursor = current;
    const char *candidate_cursor = candidate;
    for (size_t i = 0; i < 3U; ++i)
    {
        const unsigned int current_value =
            ota_transport_version_component(&current_cursor);
        const unsigned int candidate_value =
            ota_transport_version_component(&candidate_cursor);
        if (candidate_value != current_value)
        {
            return candidate_value > current_value;
        }
        if (*current_cursor == '.')
        {
            ++current_cursor;
        }
        if (*candidate_cursor == '.')
        {
            ++candidate_cursor;
        }
    }
    return false;
}

static esp_err_t ota_transport_read_manifest_body(esp_http_client_handle_t client,
                                                  char *body,
                                                  size_t body_size)
{
    const int content_length = esp_http_client_fetch_headers(client);
    if (content_length <= 0 || (size_t)content_length >= body_size)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t received = 0U;
    while (received < (size_t)content_length)
    {
        const int read_bytes = esp_http_client_read(
            client, body + received, (int)(body_size - received - 1U));
        if (read_bytes <= 0)
        {
            return ESP_ERR_HTTP_EAGAIN;
        }
        received += (size_t)read_bytes;
    }
    body[received] = '\0';
    return received == (size_t)content_length ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t ota_transport_fetch_manifest(
    const ota_transport_manifest_request_t *request,
    ota_transport_manifest_t *out_manifest)
{
    if (request == NULL || out_manifest == NULL ||
        !ota_transport_is_https_url(request->manifest_url) ||
        !ota_transport_tls_configured(request->root_ca_pem,
                                      request->use_cert_bundle) ||
        !ota_transport_host_matches(request->manifest_url,
                                    request->allowed_host) ||
        request->current_version == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    const time_t now = time(NULL);
    if (now < kMinimumTlsTime)
    {
        ESP_LOGW(TAG, "system time is not valid for TLS: %ld", (long)now);
        return ESP_ERR_INVALID_STATE;
    }

    esp_http_client_config_t http_config = {
        .url = request->manifest_url,
        .cert_pem = request->use_cert_bundle ? NULL : request->root_ca_pem,
        .crt_bundle_attach = request->use_cert_bundle ? esp_crt_bundle_attach : NULL,
        .timeout_ms = 10000,
        .keep_alive_enable = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    char body[OTA_TRANSPORT_MANIFEST_BUFFER_MAX] = {0};
    esp_err_t ret = esp_http_client_open(client, 0);
    if (ret == ESP_OK)
    {
        ret = ota_transport_read_manifest_body(client, body, sizeof(body));
    }
    const int status = esp_http_client_get_status_code(client);
    if (ret == ESP_OK && status != 200)
    {
        ret = ESP_ERR_INVALID_RESPONSE;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (ret != ESP_OK)
    {
        return ret;
    }

    cJSON *root = cJSON_ParseWithLength(body, strlen(body));
    if (root == NULL)
    {
        return ESP_ERR_INVALID_RESPONSE;
    }
    cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    cJSON *url = cJSON_GetObjectItemCaseSensitive(root, "url");
    cJSON *size = cJSON_GetObjectItemCaseSensitive(root, "size");
    cJSON *sha256 = cJSON_GetObjectItemCaseSensitive(root, "sha256");
    const bool valid_fields = cJSON_IsString(version) && cJSON_IsString(url) &&
                              cJSON_IsNumber(size) && cJSON_IsString(sha256);
    const bool valid_values = valid_fields && version->valuestring[0] != '\0' &&
                              url->valuestring[0] != '\0' &&
                              strlen(version->valuestring) <
                                  OTA_TRANSPORT_VERSION_MAX &&
                              strlen(url->valuestring) < OTA_TRANSPORT_URL_MAX &&
                              size->valuedouble > 0.0 &&
                              size->valuedouble <= (double)kOtaSlotMaxBytes &&
                              ota_transport_is_https_url(url->valuestring) &&
                              ota_transport_host_matches(url->valuestring,
                                                          request->allowed_host) &&
                              ota_transport_is_hex_sha256(sha256->valuestring) &&
                              ota_transport_version_is_newer(
                                  request->current_version, version->valuestring);
    const esp_partition_t *slot = esp_ota_get_next_update_partition(NULL);
    if (!valid_values || slot == NULL || size->valuedouble > slot->size)
    {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_VERSION;
    }

    memset(out_manifest, 0, sizeof(*out_manifest));
    strncpy(out_manifest->version, version->valuestring,
            sizeof(out_manifest->version) - 1U);
    strncpy(out_manifest->url, url->valuestring, sizeof(out_manifest->url) - 1U);
    out_manifest->size = (size_t)size->valuedouble;
    strncpy(out_manifest->sha256, sha256->valuestring,
            sizeof(out_manifest->sha256) - 1U);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t ota_transport_hash_partition(const esp_partition_t *partition,
                                               size_t image_size,
                                               char out_sha256[65])
{
    uint8_t buffer[4096];
    uint8_t digest[32];
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    int ret = mbedtls_sha256_starts(&context, 0);
    size_t offset = 0U;
    while (ret == 0 && offset < image_size)
    {
        const size_t chunk = (image_size - offset) > sizeof(buffer)
                                 ? sizeof(buffer)
                                 : image_size - offset;
        if (esp_partition_read(partition, offset, buffer, chunk) != ESP_OK)
        {
            ret = -1;
            break;
        }
        ret = mbedtls_sha256_update(&context, buffer, chunk);
        offset += chunk;
    }
    if (ret == 0)
    {
        ret = mbedtls_sha256_finish(&context, digest);
    }
    mbedtls_sha256_free(&context);
    if (ret != 0)
    {
        return ESP_FAIL;
    }
    for (size_t i = 0; i < sizeof(digest); ++i)
    {
        snprintf(out_sha256 + i * 2U, 3U, "%02x", digest[i]);
    }
    return ESP_OK;
}

static esp_err_t ota_transport_hash_partition_md5(const esp_partition_t *partition,
                                                   size_t image_size,
                                                   char out_md5[33])
{
    uint8_t buffer[4096];
    uint8_t digest[16];
    mbedtls_md5_context context;
    mbedtls_md5_init(&context);
    int ret = mbedtls_md5_starts(&context);
    size_t offset = 0U;
    while (ret == 0 && offset < image_size)
    {
        const size_t chunk = (image_size - offset) > sizeof(buffer)
                                 ? sizeof(buffer)
                                 : image_size - offset;
        if (esp_partition_read(partition, offset, buffer, chunk) != ESP_OK)
        {
            ret = -1;
            break;
        }
        ret = mbedtls_md5_update(&context, buffer, chunk);
        offset += chunk;
    }
    if (ret == 0)
    {
        ret = mbedtls_md5_finish(&context, digest);
    }
    mbedtls_md5_free(&context);
    if (ret != 0)
    {
        return ESP_FAIL;
    }
    for (size_t index = 0U; index < sizeof(digest); ++index)
    {
        snprintf(out_md5 + index * 2U, 3U, "%02x", digest[index]);
    }
    return ESP_OK;
}

esp_err_t ota_transport_download_to_staging(
    const ota_transport_manifest_t *manifest,
    const ota_transport_download_config_t *config)
{
    if (manifest == NULL || config == NULL ||
        !ota_transport_tls_configured(config->root_ca_pem,
                                      config->use_cert_bundle) ||
        !ota_transport_is_https_url(manifest->url) || manifest->size == 0U ||
        s_staged_handle != NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_https_ota_handle_t handle = NULL;
    const esp_partition_t *target = NULL;
    size_t resume_bytes = 0U;
    uint32_t reconnect_count = 0U;
    esp_err_t ret = ESP_OK;
    while (true)
    {
        ret = ota_transport_begin_download_session(
            manifest, config, resume_bytes, &handle, &target);
        if (ret != ESP_OK)
        {
            /* Wi-Fi may recover after the TLS socket attempt fails. Keep the
             * staged bytes and retry the same Range instead of abandoning the
             * OneNET task on the first reconnect failure. */
            if (ota_transport_is_retryable_download_error(ret) &&
                reconnect_count < kOtaReconnectMax)
            {
                ++reconnect_count;
                ESP_LOGW(TAG,
                         "download reconnect: attempt=%u offset=%u err=%s",
                         (unsigned int)reconnect_count,
                         (unsigned int)resume_bytes, esp_err_to_name(ret));
                vTaskDelay(pdMS_TO_TICKS(kOtaReconnectDelayMs));
                continue;
            }
            return ret;
        }

        int last_received = (int)resume_bytes;
        int64_t last_progress_us = esp_timer_get_time();
        while (true)
        {
            if (config->cancel_cb != NULL &&
                config->cancel_cb(config->user_ctx))
            {
                (void)esp_https_ota_abort(handle);
                return ESP_ERR_INVALID_STATE;
            }
            ret = esp_https_ota_perform(handle);
            const int received = esp_https_ota_get_image_len_read(handle);
            if (received > last_received)
            {
                last_received = received;
                last_progress_us = esp_timer_get_time();
            }
            if (config->progress_cb != NULL && received >= 0)
            {
                config->progress_cb((size_t)received, manifest->size,
                                    config->user_ctx);
            }
            if (received >= 0 && ota_transport_fault_reached(
                                     config->fault_mode, (size_t)received,
                                     manifest->size))
            {
                ESP_LOGW(TAG, "fault injection abort: received=%d total=%u",
                         received, (unsigned int)manifest->size);
                (void)esp_https_ota_abort(handle);
                return ESP_ERR_INVALID_STATE;
            }
            if (ret != ESP_ERR_HTTPS_OTA_IN_PROGRESS)
            {
                break;
            }
            if (esp_timer_get_time() - last_progress_us >=
                kOtaNoProgressTimeoutUs)
            {
                ESP_LOGE(TAG, "download stalled: received=%d total=%u",
                         received, (unsigned int)manifest->size);
                ret = ESP_ERR_TIMEOUT;
                break;
            }
        }

        const int received = esp_https_ota_get_image_len_read(handle);
        if (ret == ESP_OK && received >= 0 &&
            (size_t)received == manifest->size &&
            esp_https_ota_is_complete_data_received(handle))
        {
            break;
        }

        const size_t retry_offset = received > 0 ? (size_t)received
                                                  : resume_bytes;
        (void)esp_https_ota_abort(handle);
        handle = NULL;
        if (!ota_transport_is_retryable_download_error(ret) ||
            reconnect_count >= kOtaReconnectMax)
        {
            return ret == ESP_OK ? ESP_ERR_INVALID_SIZE : ret;
        }

        ++reconnect_count;
        resume_bytes = retry_offset;
        ESP_LOGW(TAG, "download reconnect: attempt=%u offset=%u err=%s",
                 (unsigned int)reconnect_count, (unsigned int)resume_bytes,
                 esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(kOtaReconnectDelayMs));
    }

    const int received = esp_https_ota_get_image_len_read(handle);
    if (ret != ESP_OK || received < 0 || (size_t)received != manifest->size ||
        !esp_https_ota_is_complete_data_received(handle))
    {
        (void)esp_https_ota_abort(handle);
        return ret == ESP_OK ? ESP_ERR_INVALID_SIZE : ret;
    }

    bool checksum_valid = false;
    if (manifest->checksum_type == OTA_TRANSPORT_CHECKSUM_MD5)
    {
        char calculated_md5[OTA_TRANSPORT_MD5_HEX_LEN + 1U] = {0};
        ret = ota_transport_hash_partition_md5(target, manifest->size,
                                               calculated_md5);
        checksum_valid = ret == ESP_OK &&
                         strcasecmp(calculated_md5, manifest->md5) == 0;
    }
    else
    {
        char calculated_sha256[OTA_TRANSPORT_SHA256_HEX_LEN + 1U] = {0};
        ret = ota_transport_hash_partition(target, manifest->size,
                                           calculated_sha256);
        checksum_valid = ret == ESP_OK &&
                         strcasecmp(calculated_sha256, manifest->sha256) == 0;
    }
    if (!checksum_valid)
    {
        (void)esp_https_ota_abort(handle);
        return ret == ESP_OK ? ESP_ERR_INVALID_CRC : ret;
    }

    s_staged_handle = handle;
    return ESP_OK;
}

esp_err_t ota_transport_activate_staging(void)
{
    if (s_staged_handle == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t ret = esp_https_ota_finish(s_staged_handle);
    s_staged_handle = NULL;
    return ret;
}

esp_err_t ota_transport_abort_staging(void)
{
    if (s_staged_handle == NULL)
    {
        return ESP_OK;
    }

    const esp_err_t ret = esp_https_ota_abort(s_staged_handle);
    s_staged_handle = NULL;
    return ret;
}

bool ota_transport_has_staged_image(void)
{
    return s_staged_handle != NULL;
}
