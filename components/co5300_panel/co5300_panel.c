/**
 * @file co5300_panel.c
 * @brief CO5300 LCD面板驱动实现 (410x502, QSPI接口)
 *
 * 主要功能：
 * - 面板初始化和配置
 * - TE信号同步（Mode1: V-Blanking + H-Blanking）
 * - 颜色传输完成回调管理
 */

#include "co5300_panel.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_co5300.h"
#include "esp_log.h"
#include "esp_check.h"
#include "co5300_panel_defaults.h"

#if CO5300_PANEL_USE_TE_SIGNAL
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#endif

static const char *TAG = "co5300_panel";

/* ========== TE信号初始化命令 ========== */

#if CO5300_PANEL_USE_TE_SIGNAL
// 自定义初始化序列：启用TE信号（替换默认的0x34命令为0x35）
static const co5300_lcd_init_cmd_t te_enable_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},                 // Sleep out + 120ms
    {0x35, (uint8_t[]){CO5300_PANEL_TE_MODE}, 1, 0},   // TE ON (Mode 1: V-Porch Only)
    {0x44, (uint8_t[]){0x00, 0x00}, 2, 0},             // TE Scan Line (0x0000 = Line 0, equivalent to TEON Mode 1)
    {0xFE, (uint8_t[]){0x00}, 1, 0},                   // Page switch
    {0xC4, (uint8_t[]){0x80}, 1, 0},                   // SPI Mode (0x80 = Enable SPI Write SRAM)
    {0x3A, (uint8_t[]){0x55}, 1, 0},                   // Pixel format (0x55 = 16-bit/pixel RGB565 for both SPI and RGB interface)
    {0x53, (uint8_t[]){0x20}, 1, 0},                   // Control display
    {0x63, (uint8_t[]){0xFF}, 1, 0},                   // HBM brightness
    {0x2A, (uint8_t[]){0x00, 0x16, 0x01, 0xAF}, 4, 0}, // 设置列地址 (Column Address: Start 0x0016=22, End 0x01AF=431)
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xF5}, 4, 0}, // 设置行地址 (Page Address: Start 0x0000=0, End 0x01F5=501)
    {0x29, (uint8_t[]){0x00}, 0, 0},                   // Display on
    {0x51, (uint8_t[]){0xFF}, 1, 0},                   // Normal brightness
    {0x58, (uint8_t[]){0x00}, 1, 0},                   // High contrast OFF
    {0x00, (uint8_t[]){0x00}, 0, 10},                  // End
};
#endif

/* ========== 全局变量 ========== */

static esp_lcd_panel_io_handle_t s_io_handle = NULL; // SPI通信句柄
static esp_lcd_panel_handle_t s_panel_handle = NULL; // LCD面板句柄

#if CO5300_PANEL_USE_TE_SIGNAL
static SemaphoreHandle_t s_te_semaphore = NULL;      // TE同步信号量
static volatile uint32_t s_te_interrupt_counter = 0; // TE中断计数器（用于过滤H-Blanking）
#define TE_FILTER_THRESHOLD 500                      // 过滤阈值：502行/帧，使用500容错
#endif

static bool s_initialized = false; // 初始化标志

/* ========== 中断处理 ========== */

#if CO5300_PANEL_USE_TE_SIGNAL
/**
 * @brief TE信号中断处理（优化版）
 * @details TE Mode 1 (V-Porch Only) 模式下：
 *          - 仅在V-Porch期间产生高电平（约60Hz）
 *          - 对应 Datasheet Reg 35h M=0, Reg 44h N=0
 *          - 无需计数器过滤，直接释放信号量
 *          - 响应速度更快，CPU占用更低
 */
static void IRAM_ATTR te_gpio_isr_handler(void *arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

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

// 默认空回调（用户可通过API动态注册自定义回调）
static bool default_color_trans_done_cb(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    return false;
}

/* ========== 公共API ========== */

/**
 * @brief 初始化CO5300面板
 *
 * 流程：TE配置 -> QSPI总线 -> 面板IO -> 面板驱动 -> 复位启动
 *
 * @return ESP_OK=成功, ESP_ERR_NO_MEM=内存不足, 其他=失败原因
 * @note 只能调用一次，重复调用直接返回ESP_OK
 */
esp_err_t co5300_panel_init(void)
{
    if (s_initialized)
    {
        ESP_LOGW(TAG, "Panel already initialized");
        return ESP_OK;
    }

#if CO5300_PANEL_USE_TE_SIGNAL
    /* 步骤1: 创建TE信号量 */
    ESP_LOGI(TAG, "Creating TE semaphore");
    s_te_semaphore = xSemaphoreCreateBinary();
    if (s_te_semaphore == NULL)
    {
        ESP_LOGE(TAG, "Failed to create TE semaphore");
        return ESP_ERR_NO_MEM;
    }

    /* 步骤2: 配置TE引脚和中断 */
    ESP_LOGI(TAG, "Configure TE pin %d", CO5300_PANEL_PIN_TE);
    const gpio_config_t te_gpio_config = {
        .pin_bit_mask = (1ULL << CO5300_PANEL_PIN_TE),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE, // Mode 1: V-Porch Start (上升沿触发)
    };
    ESP_RETURN_ON_ERROR(gpio_config(&te_gpio_config), TAG, "TE GPIO config failed");
    ESP_RETURN_ON_ERROR(gpio_install_isr_service(0), TAG, "GPIO ISR service install failed");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(CO5300_PANEL_PIN_TE, te_gpio_isr_handler, NULL), TAG, "TE ISR add failed");
    ESP_LOGI(TAG, "TE configured (Mode: 0x%02X)", CO5300_PANEL_TE_MODE);
#endif

    /* 步骤3: 初始化QSPI总线 */
    ESP_LOGI(TAG, "Initialize QSPI bus on host %d", CO5300_PANEL_HOST);
    const spi_bus_config_t buscfg = CO5300_PANEL_BUS_QSPI_CONFIG(CO5300_PANEL_PIN_PCLK,
                                                                 CO5300_PANEL_PIN_D0,
                                                                 CO5300_PANEL_PIN_D1,
                                                                 CO5300_PANEL_PIN_D2,
                                                                 CO5300_PANEL_PIN_D3,
                                                                 CO5300_PANEL_H_RES * CO5300_PANEL_MAX_TRANSFER_LINES * sizeof(uint16_t));
    ESP_RETURN_ON_ERROR(spi_bus_initialize(CO5300_PANEL_HOST, &buscfg, SPI_DMA_CH_AUTO), TAG, "SPI init failed");

    /* 步骤4: 安装面板IO（回调由LVGL层动态注册，支持同步/异步切换） */
    ESP_LOGI(TAG, "Install panel IO (CS: %d)", CO5300_PANEL_PIN_CS);
    const esp_lcd_panel_io_spi_config_t io_config = CO5300_PANEL_IO_QSPI_CONFIG_OPTIMIZED(CO5300_PANEL_PIN_CS, NULL, NULL);
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)CO5300_PANEL_HOST, &io_config, &s_io_handle), TAG, "Panel IO failed");

    /* 步骤5: 安装CO5300面板驱动 */
    co5300_vendor_config_t vendor_config = {
#if CO5300_PANEL_USE_TE_SIGNAL
        .init_cmds = te_enable_init_cmds,
        .init_cmds_size = sizeof(te_enable_init_cmds) / sizeof(co5300_lcd_init_cmd_t),
#else
        .init_cmds = NULL,
        .init_cmds_size = 0,
#endif
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = CO5300_PANEL_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = CO5300_PANEL_BIT_PER_PIXEL,
        .vendor_config = (void *)&vendor_config,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_co5300(s_io_handle, &panel_config, &s_panel_handle), TAG, "New panel failed");

    /* 步骤6: 复位并启动面板 */
    ESP_LOGI(TAG, "Reset and start panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel_handle), TAG, "Panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel_handle), TAG, "Panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel_handle, true), TAG, "Display on failed");

    s_initialized = true;

#if CO5300_PANEL_USE_TE_SIGNAL
    ESP_LOGI(TAG, "CO5300 init OK (TE enabled, mode: 0x%02X)", CO5300_PANEL_TE_MODE);
#else
    ESP_LOGI(TAG, "CO5300 init OK (TE disabled)");
#endif
    return ESP_OK;
}

#if CO5300_PANEL_USE_TE_SIGNAL
/**
 * @brief 等待TE信号
 * @param timeout_ms 超时时间（0=永久等待）
 * @return ESP_OK=成功, ESP_ERR_TIMEOUT=超时, ESP_ERR_INVALID_STATE=未初始化
 */
esp_err_t co5300_panel_wait_te_signal(uint32_t timeout_ms)
{
    if (!s_initialized || s_te_semaphore == NULL)
    {
        ESP_LOGE(TAG, "TE not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    TickType_t timeout_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);

    if (xSemaphoreTake(s_te_semaphore, timeout_ticks) == pdTRUE)
    {
        return ESP_OK;
    }
    else
    {
        ESP_LOGW(TAG, "TE timeout (%lu ms)", timeout_ms);
        return ESP_ERR_TIMEOUT;
    }
}
#endif

/**
 * @brief 获取面板原始句柄（用于直接调用ESP-IDF LCD API）
 * @param io 返回IO句柄（可为NULL）
 * @param panel 返回面板句柄（可为NULL）
 * @return ESP_OK=成功, ESP_ERR_INVALID_STATE=未初始化
 */
esp_err_t co5300_panel_get_raw(struct esp_lcd_panel_io_t **io, struct esp_lcd_panel_t **panel)
{
    if (!s_initialized || s_io_handle == NULL || s_panel_handle == NULL)
    {
        ESP_LOGE(TAG, "Panel not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (io != NULL)
    {
        *io = s_io_handle;
    }

    if (panel != NULL)
    {
        *panel = s_panel_handle;
    }

    return ESP_OK;
}

/**
 * @brief 注册颜色传输完成回调（动态注册，支持同步/异步切换）
 * @param cbs 回调函数结构体
 * @param user_ctx 用户上下文
 * @return ESP_OK=成功, ESP_ERR_INVALID_STATE=未初始化, ESP_ERR_INVALID_ARG=参数错误
 * @note 回调在中断上下文执行，应保持简短
 */
esp_err_t co5300_panel_register_color_done_callback(const esp_lcd_panel_io_callbacks_t *cbs, void *user_ctx)
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

    return esp_lcd_panel_io_register_event_callbacks(s_io_handle, cbs, user_ctx);
}
