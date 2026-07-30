#include "services/runtime_gate/foreground_runtime_gate.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static const char *TAG = "fg_runtime_gate";

static portMUX_TYPE s_gate_lock = portMUX_INITIALIZER_UNLOCKED;
static foreground_runtime_owner_t s_current_owner =
    FOREGROUND_RUNTIME_OWNER_NONE;
static bool s_initialized = false;

const char *foreground_runtime_gate_owner_text(
    foreground_runtime_owner_t owner)
{
    switch (owner)
    {
    case FOREGROUND_RUNTIME_OWNER_HERMES:
        return "HERMES";
    case FOREGROUND_RUNTIME_OWNER_OFFICIAL_CHAT:
        return "OFFICIAL_CHAT";
    case FOREGROUND_RUNTIME_OWNER_BLE_PROVISIONING:
        return "BLE_PROVISIONING";
    case FOREGROUND_RUNTIME_OWNER_OTA:
        return "OTA";
    case FOREGROUND_RUNTIME_OWNER_NONE:
    default:
        return "NONE";
    }
}
esp_err_t foreground_runtime_gate_init(void)
{
    portENTER_CRITICAL(&s_gate_lock);
    s_initialized = true;
    portEXIT_CRITICAL(&s_gate_lock);
    return ESP_OK;
}

esp_err_t foreground_runtime_gate_try_acquire(foreground_runtime_owner_t owner)
{
    if (owner == FOREGROUND_RUNTIME_OWNER_NONE)
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;
    foreground_runtime_owner_t blocking_owner = FOREGROUND_RUNTIME_OWNER_NONE;
    portENTER_CRITICAL(&s_gate_lock);
    if (!s_initialized)
    {
        s_initialized = true;
    }

    if (s_current_owner != FOREGROUND_RUNTIME_OWNER_NONE &&
             s_current_owner != owner)
    {
        blocking_owner = s_current_owner;
        ret = ESP_ERR_INVALID_STATE;
    }
    else
    {
        s_current_owner = owner;
    }
    portEXIT_CRITICAL(&s_gate_lock);

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "foreground acquired: owner=%s",
                 foreground_runtime_gate_owner_text(owner));
    }
    else
    {
        ESP_LOGW(TAG, "foreground acquire denied: owner=%s current=%s",
                 foreground_runtime_gate_owner_text(owner),
                 foreground_runtime_gate_owner_text(blocking_owner));
    }
    return ret;
}

esp_err_t foreground_runtime_gate_release(foreground_runtime_owner_t owner)
{
    if (owner == FOREGROUND_RUNTIME_OWNER_NONE)
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;
    foreground_runtime_owner_t current = FOREGROUND_RUNTIME_OWNER_NONE;

    portENTER_CRITICAL(&s_gate_lock);
    current = s_current_owner;
    if (s_current_owner == owner)
    {
        s_current_owner = FOREGROUND_RUNTIME_OWNER_NONE;
    }
    else
    {
        ret = ESP_ERR_INVALID_STATE;
    }
    portEXIT_CRITICAL(&s_gate_lock);

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "foreground released: owner=%s",
                 foreground_runtime_gate_owner_text(owner));
    }
    else
    {
        ESP_LOGW(TAG, "foreground release denied: owner=%s current=%s",
                 foreground_runtime_gate_owner_text(owner),
                 foreground_runtime_gate_owner_text(current));
    }
    return ret;
}

bool foreground_runtime_gate_is_active(void)
{
    bool active = false;
    portENTER_CRITICAL(&s_gate_lock);
    active = s_current_owner != FOREGROUND_RUNTIME_OWNER_NONE;
    portEXIT_CRITICAL(&s_gate_lock);
    return active;
}

foreground_runtime_owner_t foreground_runtime_gate_current_owner(void)
{
    foreground_runtime_owner_t owner = FOREGROUND_RUNTIME_OWNER_NONE;
    portENTER_CRITICAL(&s_gate_lock);
    owner = s_current_owner;
    portEXIT_CRITICAL(&s_gate_lock);
    return owner;
}
