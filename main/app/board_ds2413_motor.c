#include "app/board_ds2413_motor.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "ds2413.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "onewire_bus.h"
#include "onewire_bus_impl_rmt.h"
#include "onewire_bus_impl_uart.h"

static const char *TAG = "board_ds2413_motor";

// DS2413 只有一条 GPIO18 1-Wire 总线，所有访问通过互斥锁串行化。
static onewire_bus_handle_t s_bus;
static ds2413_device_t s_device;
static SemaphoreHandle_t s_lock;
static bool s_initialized;

// PIOB 当前不用，始终 release；PIOA pull-low 才是本板马达关闭态。
static const ds2413_latch_state_t k_motor_off_latch = {
    .pioa_release = false,
    .piob_release = true,
};

// PIOA release 后由板上上拉/驱动级让马达开启。
static const ds2413_latch_state_t k_motor_on_latch = {
    .pioa_release = true,
    .piob_release = true,
};

static esp_err_t board_ds2413_motor_write_locked(
    const ds2413_latch_state_t *latch_state, ds2413_state_t *out_state)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG,
                        "DS2413 motor is not initialized");
    return ds2413_write_latch(&s_device, latch_state, out_state);
}

static void board_ds2413_motor_cleanup_bus(void)
{
    if (s_bus != NULL)
    {
        esp_err_t ret = onewire_bus_del(s_bus);
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "delete 1-Wire bus failed: %s", esp_err_to_name(ret));
        }
        s_bus = NULL;
    }
    s_initialized = false;
}

static esp_err_t board_ds2413_motor_new_bus_rmt(onewire_bus_handle_t *out_bus)
{
    onewire_bus_config_t bus_cfg = {
        .bus_gpio_num = GPIO_NUM_18,
        .flags = {
            .en_pull_up = false, // GPIO18 已有外部 R22 4.7k 上拉。
        },
    };
    onewire_bus_rmt_config_t rmt_cfg = {
        .max_rx_bytes = 10,
    };
    return onewire_new_bus_rmt(&bus_cfg, &rmt_cfg, out_bus);
}

static esp_err_t board_ds2413_motor_new_bus_uart(onewire_bus_handle_t *out_bus)
{
    onewire_bus_config_t bus_cfg = {
        .bus_gpio_num = GPIO_NUM_18,
        .flags = {
            .en_pull_up = false, // GPIO18 已有外部 R22 4.7k 上拉。
        },
    };
    onewire_bus_uart_config_t uart_cfg = {
        .uart_port_num = UART_NUM_1,
    };
    return onewire_new_bus_uart(&bus_cfg, &uart_cfg, out_bus);
}

static esp_err_t board_ds2413_motor_probe_bus(const char *backend_name)
{
    esp_err_t ret = ds2413_find_first(s_bus, &s_device);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "%s backend did not find DS2413: %s",
                 backend_name, esp_err_to_name(ret));
        return ret;
    }

    const uint8_t *rom = (const uint8_t *)&s_device.address;
    ESP_LOGI(TAG, "DS2413 ROM via %s: %02X %02X %02X %02X %02X %02X %02X %02X",
             backend_name,
             rom[0], rom[1], rom[2], rom[3], rom[4], rom[5], rom[6], rom[7]);
    return ESP_OK;
}

esp_err_t board_ds2413_motor_init(void)
{
    if (s_lock == NULL)
    {
        s_lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_NO_MEM, TAG,
                            "create motor mutex failed");
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);

    if (s_initialized)
    {
        ds2413_state_t state = {0};
        esp_err_t ret = board_ds2413_motor_write_locked(&k_motor_off_latch, &state);
        xSemaphoreGive(s_lock);
        return ret;
    }

    gpio_reset_pin(GPIO_NUM_18);
    gpio_set_direction(GPIO_NUM_18, GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_NUM_18, GPIO_FLOATING);
    vTaskDelay(pdMS_TO_TICKS(2));
    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_FALSE(gpio_get_level(GPIO_NUM_18) == 1, ESP_ERR_INVALID_STATE,
                      fail, TAG, "GPIO18 1-Wire bus is held low");

    ret = board_ds2413_motor_new_bus_rmt(&s_bus);
    if (ret == ESP_OK)
    {
        ret = board_ds2413_motor_probe_bus("RMT");
        if (ret != ESP_OK)
        {
            board_ds2413_motor_cleanup_bus();
        }
    }

    if (ret != ESP_OK)
    {
        ret = board_ds2413_motor_new_bus_uart(&s_bus);
        ESP_GOTO_ON_ERROR(ret, fail, TAG, "create UART 1-Wire bus failed");
        ret = board_ds2413_motor_probe_bus("UART1");
        ESP_GOTO_ON_ERROR(ret, fail, TAG, "probe DS2413 on UART failed");
    }

    s_initialized = true;
    ds2413_state_t state = {0};
    ret = board_ds2413_motor_write_locked(&k_motor_off_latch, &state);
    ESP_GOTO_ON_ERROR(ret, fail, TAG, "set motor default off failed");
    ESP_LOGI(TAG, "DS2413 motor default off: raw=0x%02X PIOA(state=%d latch=%d)",
             state.raw, state.pioa_state, state.pioa_latch);

    xSemaphoreGive(s_lock);
    return ESP_OK;

fail:
    board_ds2413_motor_cleanup_bus();
    xSemaphoreGive(s_lock);
    return ret;
}

esp_err_t board_ds2413_motor_set_enabled(bool enabled)
{
    ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "DS2413 motor lock is not created");

    xSemaphoreTake(s_lock, portMAX_DELAY);
    ds2413_state_t state = {0};
    const ds2413_latch_state_t *latch =
        enabled ? &k_motor_on_latch : &k_motor_off_latch;
    esp_err_t ret = board_ds2413_motor_write_locked(latch, &state);
    if (ret == ESP_OK)
    {
        ESP_LOGD(TAG, "motor %s: raw=0x%02X PIOA(state=%d latch=%d)",
                 enabled ? "on" : "off",
                 state.raw, state.pioa_state, state.pioa_latch);
    }
    xSemaphoreGive(s_lock);
    return ret;
}

esp_err_t board_ds2413_motor_pulse(uint32_t on_ms)
{
    esp_err_t ret = board_ds2413_motor_set_enabled(true);
    if (ret != ESP_OK)
    {
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(on_ms));

    return board_ds2413_motor_set_enabled(false);
}
