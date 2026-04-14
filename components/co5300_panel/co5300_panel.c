/**
 * @file co5300_panel.c
 * @brief CO5300 LCD面板驱动实现 (410x502, QSPI接口)
 *
 * 主要功能：
 * - 面板初始化和配置
 * - TE信号同步（Mode 1: 仅 V-Blanking）
 * - 颜色传输完成回调管理
 * - 运行期亮度控制
 */

#include "co5300_panel.h"

#include "co5300_panel_defaults.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"

#if CO5300_PANEL_USE_TE_SIGNAL
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#endif

static const char *TAG = "co5300_panel";

/* ========== TE信号初始化命令 ========== */

#if CO5300_PANEL_USE_TE_SIGNAL
static const co5300_lcd_init_cmd_t te_enable_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x35, (uint8_t[]){CO5300_PANEL_TE_MODE}, 1, 0},
    {0x44, (uint8_t[]){0x00, 0x00}, 2, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x16, 0x01, 0xAF}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xF5}, 4, 0},
    {0x29, (uint8_t[]){0x00}, 0, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x58, (uint8_t[]){0x00}, 1, 0},
    {0x00, (uint8_t[]){0x00}, 0, 10},
};
#endif

/* ========== 全局变量 ========== */

static esp_lcd_panel_io_handle_t s_io_handle = NULL; // 面板 IO 句柄（发送命令/像素）
static esp_lcd_panel_handle_t s_panel_handle = NULL; // 面板设备句柄（reset/init/draw）

#if CO5300_PANEL_USE_TE_SIGNAL
static SemaphoreHandle_t s_te_semaphore = NULL;      // TE 中断到任务态的同步信号量
static volatile int64_t s_last_te_timestamp = 0;     // 最近一次 TE 上升沿时间戳（us）
static volatile uint32_t s_te_interrupt_counter = 0; // TE 中断计数
#define TE_FILTER_THRESHOLD 500
#endif

static bool s_initialized = false;  // 面板是否已完成初始化
static uint8_t s_brightness = 0xFF; // 亮度寄存器缓存值（0~255）

/* ========== 中断处理 ========== */

#if CO5300_PANEL_USE_TE_SIGNAL
static void IRAM_ATTR te_gpio_isr_handler(void *arg)
{
    (void)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    s_last_te_timestamp = esp_timer_get_time();
    s_te_interrupt_counter++;

    if (s_te_semaphore != NULL)
    {
        xSemaphoreGiveFromISR(s_te_semaphore, &xHigherPriorityTaskWoken);
    }

    if (xHigherPriorityTaskWoken == pdTRUE)
    {
        portYIELD_FROM_ISR();
    }
}
#endif

/* ========== 回调函数 ========== */

static bool default_color_trans_done_cb(esp_lcd_panel_io_handle_t panel_io,
                                        esp_lcd_panel_io_event_data_t *edata,
                                        void *user_ctx)
{
    (void)panel_io;
    (void)edata;
    (void)user_ctx;
    return false;
}

/* ========== 公共API ========== */

esp_err_t co5300_panel_set_brightness(uint8_t value)
{
    if (!s_initialized || s_panel_handle == NULL)
    {
        ESP_LOGE(TAG, "Panel not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t brightness_percent = (uint8_t)(((uint32_t)value * 100U + 127U) / 255U);
    return co5300_panel_set_brightness_percent(brightness_percent);
}

uint8_t co5300_panel_get_brightness(void)
{
    return s_brightness;
}

esp_err_t co5300_panel_set_brightness_percent(uint8_t percent)
{
    if (!s_initialized || s_panel_handle == NULL)
    {
        ESP_LOGE(TAG, "Panel not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (percent > 100U)
    {
        percent = 100U;
    }

    /*
     * CO5300 的 QSPI 命令需要通过驱动内部的 tx_param 编码 opcode。
     * 直接对 panel_io 裸发 0x51 在当前板上不会真正改亮度，
     * 因此统一复用官方组件提供的亮度接口。
     */
    esp_err_t ret = esp_lcd_panel_co5300_set_brightness(s_panel_handle, percent);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Set brightness failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_brightness = (uint8_t)(((uint32_t)percent * 255U + 50U) / 100U);
    return ESP_OK;
}

uint8_t co5300_panel_get_brightness_percent(void)
{
    return (uint8_t)(((uint32_t)s_brightness * 100U + 127U) / 255U);
}

esp_err_t co5300_panel_init(void)
{
    if (s_initialized)
    {
        ESP_LOGW(TAG, "Panel already initialized");
        return ESP_OK;
    }

#if CO5300_PANEL_USE_TE_SIGNAL
    ESP_LOGI(TAG, "Creating TE semaphore");
    s_te_semaphore = xSemaphoreCreateBinary();
    if (s_te_semaphore == NULL)
    {
        ESP_LOGE(TAG, "Failed to create TE semaphore");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Configure TE pin %d", CO5300_PANEL_PIN_TE);
    const gpio_config_t te_gpio_config = {
        .pin_bit_mask = (1ULL << CO5300_PANEL_PIN_TE),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&te_gpio_config), TAG, "TE GPIO config failed");

    esp_err_t isr_ret = gpio_isr_handler_add(CO5300_PANEL_PIN_TE, te_gpio_isr_handler, NULL);
    if (isr_ret == ESP_ERR_INVALID_STATE)
    {
        ESP_LOGD(TAG, "ISR service not installed, installing now...");
        isr_ret = gpio_install_isr_service(0);
        if (isr_ret != ESP_OK && isr_ret != ESP_ERR_INVALID_STATE)
        {
            ESP_LOGE(TAG, "GPIO ISR service install failed: %s", esp_err_to_name(isr_ret));
            return isr_ret;
        }
        ESP_RETURN_ON_ERROR(gpio_isr_handler_add(CO5300_PANEL_PIN_TE, te_gpio_isr_handler, NULL),
                            TAG, "TE ISR add failed");
    }
    else
    {
        ESP_RETURN_ON_ERROR(isr_ret, TAG, "TE ISR add failed");
    }

    ESP_LOGI(TAG, "TE configured (Mode: 0x%02X)", CO5300_PANEL_TE_MODE);
#endif

    ESP_LOGI(TAG, "Initialize QSPI bus on host %d", CO5300_PANEL_HOST);
    // max_transfer_sz 决定 DMA 分配上限，需要覆盖“屏宽 * 单次最大传输行 * 2字节”。
    const spi_bus_config_t buscfg = CO5300_PANEL_BUS_QSPI_CONFIG(
        CO5300_PANEL_PIN_PCLK,
        CO5300_PANEL_PIN_D0,
        CO5300_PANEL_PIN_D1,
        CO5300_PANEL_PIN_D2,
        CO5300_PANEL_PIN_D3,
        CO5300_PANEL_H_RES * CO5300_PANEL_MAX_TRANSFER_LINES * sizeof(uint16_t));
    ESP_RETURN_ON_ERROR(spi_bus_initialize(CO5300_PANEL_HOST, &buscfg, SPI_DMA_CH_AUTO),
                        TAG, "SPI init failed");

    ESP_LOGI(TAG, "Install panel IO (CS: %d)", CO5300_PANEL_PIN_CS);
    const esp_lcd_panel_io_spi_config_t io_config =
        CO5300_PANEL_IO_QSPI_CONFIG_OPTIMIZED(CO5300_PANEL_PIN_CS,
                                              default_color_trans_done_cb, NULL);
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)CO5300_PANEL_HOST,
                                 &io_config, &s_io_handle),
        TAG, "Panel IO failed");

    co5300_vendor_config_t vendor_config = {
#if CO5300_PANEL_USE_TE_SIGNAL
        .init_cmds = te_enable_init_cmds, // 启用 TE 的初始化命令表
        .init_cmds_size = sizeof(te_enable_init_cmds) / sizeof(co5300_lcd_init_cmd_t),
#else
        .init_cmds = NULL,
        .init_cmds_size = 0,
#endif
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = CO5300_PANEL_PIN_RST,       // 面板硬复位引脚
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,   // 像素通道顺序
        .bits_per_pixel = CO5300_PANEL_BIT_PER_PIXEL, // 像素位宽（RGB565）
        .vendor_config = (void *)&vendor_config,      // CO5300 私有配置
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_co5300(s_io_handle, &panel_config, &s_panel_handle),
                        TAG, "New panel failed");

    ESP_LOGI(TAG, "Reset and start panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel_handle), TAG, "Panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel_handle), TAG, "Panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel_handle, true), TAG, "Display on failed");

    s_initialized = true;
    s_brightness = 0xFF;

#if CO5300_PANEL_USE_TE_SIGNAL
    ESP_LOGI(TAG, "CO5300 init OK (TE enabled, mode: 0x%02X)", CO5300_PANEL_TE_MODE);
#else
    ESP_LOGI(TAG, "CO5300 init OK (TE disabled)");
#endif
    return ESP_OK;
}

#if CO5300_PANEL_USE_TE_SIGNAL
esp_err_t co5300_panel_wait_te_signal(uint32_t timeout_ms)
{
    if (!s_initialized || s_te_semaphore == NULL)
    {
        ESP_LOGE(TAG, "TE not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (gpio_get_level(CO5300_PANEL_PIN_TE) == 1)
    {
        return ESP_OK;
    }

    while (xSemaphoreTake(s_te_semaphore, 0) == pdTRUE)
    {
    }

    if (gpio_get_level(CO5300_PANEL_PIN_TE) == 1)
    {
        return ESP_OK;
    }

    TickType_t timeout_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(s_te_semaphore, timeout_ticks) == pdTRUE)
    {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "TE timeout (%lu ms)", timeout_ms);
    return ESP_ERR_TIMEOUT;
}
#endif

esp_err_t co5300_panel_get_raw(struct esp_lcd_panel_io_t **io, struct esp_lcd_panel_t **panel)
{
    if (!s_initialized || s_io_handle == NULL || s_panel_handle == NULL)
    {
        ESP_LOGE(TAG, "Panel not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (io != NULL)
    {
        *io = s_io_handle; // 返回共享 IO 句柄，不转移所有权
    }
    if (panel != NULL)
    {
        *panel = s_panel_handle; // 返回共享 panel 句柄，不转移所有权
    }

    return ESP_OK;
}

esp_err_t co5300_panel_register_color_done_callback(const esp_lcd_panel_io_callbacks_t *cbs,
                                                    void *user_ctx)
{
    if (!s_initialized || s_io_handle == NULL)
    {
        ESP_LOGE(TAG, "Panel not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (cbs == NULL)
    {
        ESP_LOGE(TAG, "Callback is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // user_ctx 会在回调执行时原样传回，调用方需保证其生命周期有效。
    return esp_lcd_panel_io_register_event_callbacks(s_io_handle, cbs, user_ctx);
}
