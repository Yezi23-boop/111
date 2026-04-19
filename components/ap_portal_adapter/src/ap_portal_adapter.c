/**
 * @file ap_portal_adapter.c
 * @brief AP 门户适配层，负责最小 HTTPD 与 SoftAP provisioning handle 复用。
 */

#include "ap_portal_adapter.h"

#include "ap_portal_routes.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include "network_provisioning/scheme_softap.h"

/** @brief 组件日志标签。 */
static const char *TAG = "ap_portal";
/** @brief 当前门户 HTTPD 句柄；官方 SoftAP provisioning 会复用这个实例。 */
static httpd_handle_t s_portal_server = NULL;
/** @brief AP 门户句柄保护 mutex 的静态存储。 */
static StaticSemaphore_t s_portal_mutex_buffer;
/** @brief 串行化门户 HTTPD 启停与句柄发布的 mutex。 */
static SemaphoreHandle_t s_portal_mutex = NULL;
/** @brief 保护门户 mutex 首次创建路径的最小临界区锁。 */
static portMUX_TYPE s_portal_bootstrap_lock = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t ap_portal_adapter_ensure_mutex(void);

/**
 * @brief 确保 AP 门户运行态 mutex 已创建。
 *
 * 该 mutex 用来串行化门户 HTTPD 的启动、停止和句柄查询，避免不同任务同时改写
 * `s_portal_server` 时把官方 SoftAP provisioning 绑定到过期或半停止的 handle。
 *
 * @return `ESP_OK` 表示 mutex 已可用；其他错误表示创建失败。
 */
static esp_err_t ap_portal_adapter_ensure_mutex(void)
{
    if (s_portal_mutex != NULL)
    {
        return ESP_OK;
    }

    portENTER_CRITICAL(&s_portal_bootstrap_lock);
    if (s_portal_mutex == NULL)
    {
        s_portal_mutex = xSemaphoreCreateMutexStatic(&s_portal_mutex_buffer);
    }
    portEXIT_CRITICAL(&s_portal_bootstrap_lock);

    return s_portal_mutex != NULL ? ESP_OK : ESP_FAIL;
}

/**
 * @brief 启动 AP 门户适配层的最小 HTTP 服务器。
 *
 * 服务器启动成功后，当前函数会立刻把 `httpd_handle_t` 交给
 * `network_prov_scheme_softap_set_httpd_handle()`，从而确保官方 SoftAP provisioning
 * 后续不会重复启动第二个 HTTPD 实例。
 *
 * @return `ESP_OK` 表示启动成功；其他错误表示 HTTP server 或路由注册失败。
 */
esp_err_t ap_portal_adapter_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    esp_err_t ret = ESP_OK;

    ret = ap_portal_adapter_ensure_mutex();
    if (ret != ESP_OK)
    {
        return ret;
    }
    if (xSemaphoreTake(s_portal_mutex, portMAX_DELAY) != pdTRUE)
    {
        return ESP_FAIL;
    }

    if (s_portal_server != NULL)
    {
        network_prov_scheme_softap_set_httpd_handle((void *)s_portal_server);
        xSemaphoreGive(s_portal_mutex);
        return ESP_OK;
    }

    /* AP 门户页面后续会继续承接配置交互与浏览器请求，因此这里先提升 URI handler 容量，
     * 避免后续叠加 provisioning endpoint 时过早碰到 handler 数量上限。 */
    config.max_uri_handlers = 8;
    config.stack_size = 8192;

    ret = httpd_start(&s_portal_server, &config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "启动 AP 门户 HTTPD 失败: %s", esp_err_to_name(ret));
        xSemaphoreGive(s_portal_mutex);
        return ret;
    }

    ret = ap_portal_routes_register(s_portal_server);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "注册 AP 门户路由失败: %s", esp_err_to_name(ret));
        httpd_stop(s_portal_server);
        s_portal_server = NULL;
        xSemaphoreGive(s_portal_mutex);
        return ret;
    }

    network_prov_scheme_softap_set_httpd_handle((void *)s_portal_server);
    ESP_LOGI(TAG, "AP 门户 HTTPD 已启动并复用给 SoftAP provisioning");
    xSemaphoreGive(s_portal_mutex);
    return ESP_OK;
}

/**
 * @brief 停止 AP 门户适配层的 HTTP 服务器。
 *
 * @return `ESP_OK` 表示停止成功或服务器本就未启动；其他错误表示停止失败。
 */
esp_err_t ap_portal_adapter_stop(void)
{
    esp_err_t ret = ap_portal_adapter_ensure_mutex();
    if (ret != ESP_OK)
    {
        return ret;
    }
    if (xSemaphoreTake(s_portal_mutex, portMAX_DELAY) != pdTRUE)
    {
        return ESP_FAIL;
    }

    if (s_portal_server == NULL)
    {
        network_prov_scheme_softap_set_httpd_handle(NULL);
        xSemaphoreGive(s_portal_mutex);
        return ESP_OK;
    }

    ret = httpd_stop(s_portal_server);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "停止 AP 门户 HTTPD 失败: %s", esp_err_to_name(ret));
        xSemaphoreGive(s_portal_mutex);
        return ret;
    }

    network_prov_scheme_softap_set_httpd_handle(NULL);
    s_portal_server = NULL;
    xSemaphoreGive(s_portal_mutex);
    return ESP_OK;
}

/**
 * @brief 获取当前 AP 门户 HTTPD 句柄。
 *
 * @return 当前 HTTPD 句柄；若门户未启动则返回 `NULL`。
 */
httpd_handle_t ap_portal_adapter_get_httpd_handle(void)
{
    httpd_handle_t server = NULL;

    if (ap_portal_adapter_ensure_mutex() != ESP_OK)
    {
        return NULL;
    }
    if (xSemaphoreTake(s_portal_mutex, portMAX_DELAY) != pdTRUE)
    {
        return NULL;
    }

    server = s_portal_server;
    xSemaphoreGive(s_portal_mutex);
    return server;
}
