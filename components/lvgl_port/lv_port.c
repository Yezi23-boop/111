/**
 * @file lv_port.c
 * @brief LVGL移植层实现
 * @details 在 ESP32S3 上集成 CO5300 显示面板、FT5x06 触摸与 LVGL 定时器。
 *
 * 主要功能：
 * 1. 初始化并注册 LVGL 显示驱动（基于 esp_lcd 面板）
 * 2. 初始化并注册 LVGL 触摸输入（基于 esp_lcd_touch / FT5x06）
 * 3. 提供多种显示缓冲策略（优化 PSRAM、小缓冲）
 * 4. 提供 5ms LVGL tick 定时器
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lv_port.h"
#include "esp_log.h"
#include "lvgl.h" // LVGL 9.3 主头文件
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "lv_port.h"
#include "lv_port_config.h"
#include "co5300_panel.h"
#include "co5300_panel_defaults.h" // 增量改动：包含TE信号配置宏定义
#include "touch_ft5x06.h"
#include "esp_lcd_panel_ops.h"
#include "esp_check.h"
#include <inttypes.h>
#include <string.h>
#define TAG "lv_port"
// LCD显示参数改为 CO5300 面板分辨率

// LVGL 9.x API：移除了 lv_disp_drv_t，改为使用 lv_display_t 对象
static lv_display_t *s_display = NULL; // LVGL显示对象

// 新增：底层原生句柄（从 co5300_panel / ft5x06 获取）
static esp_lcd_panel_handle_t s_panel = NULL;
static void *s_touch = NULL;
// 新增：保存上一次有效触摸坐标，避免“松开时坐标归零”导致UI跳变
static int16_t s_last_x = 0;
static int16_t s_last_y = 0;

// 字节交换控制变量（运行时可调整）
static bool s_byte_swap_enabled = LV_PORT_BYTE_SWAP_ENABLE;

#if CO5300_PANEL_USE_TE_SIGNAL
/* ========== 帧同步状态管理 ========== */
typedef struct
{
    bool frame_start;          // 是否为帧起始（首个area）
    uint32_t flush_count;      // 本帧已刷新的area计数
    uint32_t te_sync_count;    // TE同步成功计数
    uint32_t te_timeout_count; // TE超时计数
} frame_sync_ctx_t;

static frame_sync_ctx_t s_frame_ctx = {
    .frame_start = true,
    .flush_count = 0,
    .te_sync_count = 0,
    .te_timeout_count = 0,
};
#endif

/* ========== 简化传输函数声明 ========== */

#if LV_PORT_CHUNKED_TRANSFER_ENABLE
static esp_err_t lv_port_flush_area_chunked_simple(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
#endif

static esp_err_t lv_port_flush_area_with_sync(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static esp_err_t lv_port_wait_one_chunk_done(uint32_t *pending_chunks, TickType_t timeout_ticks);
static void lv_port_rounder_event_cb(lv_event_t *e);

// LVGL 9.x API：显示刷新回调函数签名变更（第三个参数为 uint8_t*）
void lv_port_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
// 输入设备
void lv_port_indev_init(void);
// 面板/触摸硬件初始化
void lv_port_panel_init(void);
void lv_port_touch_init(void);
// Tick定时器
void lv_port_tick_init(void);

// 声明回调函数
static bool lvgl_port_flush_ready_callback(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx);

// 记录颜色传输完成事件，用于限制同时在飞的 chunk 数量
static SemaphoreHandle_t s_flush_done_sem = NULL;
// LCD 发送 bounce buffer：避免直接修改 LVGL 渲染缓冲，并绕开 PSRAM -> 私有 DMA 缓冲复制
static uint8_t *s_tx_chunk_bufs[LV_PORT_MAX_INFLIGHT_CHUNKS] = {0};
static size_t s_tx_chunk_buf_size = 0;
static uint32_t s_tx_chunk_buf_next = 0;

static void lv_port_init_tx_chunk_buffers(void);
static uint8_t *lv_port_prepare_tx_buffer(const uint8_t *px_map, uint32_t pixel_count, size_t color_bytes);

void lv_port_disp_init_small(void) // ram
{
    const size_t disp_buf_size = LCD_WIDTH * LV_PORT_FIXED_CHUNK_LINES1;

    ESP_LOGI(TAG,
             "Small buffer size: %zu pixels (%.1f KB each)",
             disp_buf_size,
             (disp_buf_size * sizeof(lv_color_t)) / 1024.0f);

    lv_color_t *disp1 = heap_caps_malloc(disp_buf_size * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    lv_color_t *disp2 = heap_caps_malloc(disp_buf_size * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);

    if (!disp1)
        disp1 = heap_caps_malloc(disp_buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
    if (!disp2)
        disp2 = heap_caps_malloc(disp_buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);

    ESP_LOGI(TAG,
             "Small Buffer1: %s, Buffer2: %s",
             esp_ptr_external_ram(disp1) ? "PSRAM" : "Internal",
             esp_ptr_external_ram(disp2) ? "PSRAM" : "Internal");

    // LVGL 9.x API：创建显示对象并设置参数
    s_display = lv_display_create(LCD_WIDTH, LCD_HEIGHT);

    if (s_flush_done_sem == NULL)
    {
        s_flush_done_sem = xSemaphoreCreateCounting(CO5300_PANEL_OPTIMIZED_TRANS_QUEUE_DEPTH, 0);
        if (s_flush_done_sem == NULL)
        {
            ESP_LOGE(TAG, "创建刷新同步信号量失败");
            return;
        }
    }

    lv_port_init_tx_chunk_buffers();

    // 设置颜色格式为RGB565（16位色深）
    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);

    // 设置刷新回调函数
    lv_display_set_flush_cb(s_display, lv_port_disp_flush);
    lv_display_add_event_cb(s_display, lv_port_rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    // 设置显示缓冲区：使用lv_display_set_buffers API，缓冲区大小以字节为单位
    lv_display_set_buffers(s_display, disp1, disp2,
                           disp_buf_size * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    // 注册传输完成回调，启用异步刷新
    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = lvgl_port_flush_ready_callback,
    };
    co5300_panel_register_color_done_callback(&cbs, s_display);
}

void lv_port_disp_init_single(void) // 片外ram
{
    const size_t disp_buf_size = LCD_WIDTH * LV_PORT_FIXED_CHUNK_LINES2;

    ESP_LOGI(TAG,
             "Single buffer size: %zu pixels (%.1f KB)",
             disp_buf_size,
             (disp_buf_size * sizeof(lv_color_t)) / 1024.0f);
    lv_color_t *disp_buf1 = heap_caps_malloc(disp_buf_size * sizeof(lv_color_t), MALLOC_CAP_32BIT | MALLOC_CAP_SPIRAM);
    lv_color_t *disp_buf2 = heap_caps_malloc(disp_buf_size * sizeof(lv_color_t), MALLOC_CAP_32BIT | MALLOC_CAP_SPIRAM);

    if (!disp_buf1)
    {
        ESP_LOGE(TAG, "单缓存1分配失败");
        return;
    }
    if (!disp_buf2)
    {
        ESP_LOGE(TAG, "单缓存2分配失败");
        return;
    }

    ESP_LOGI(TAG,
             "Single Buffer1: %s, Buffer2: %s",
             esp_ptr_external_ram(disp_buf1) ? "PSRAM" : "Internal",
             esp_ptr_external_ram(disp_buf2) ? "PSRAM" : "Internal");

    // LVGL 9.x API：创建显示对象并设置参数
    s_display = lv_display_create(LCD_WIDTH, LCD_HEIGHT);

    if (s_flush_done_sem == NULL)
    {
        s_flush_done_sem = xSemaphoreCreateCounting(CO5300_PANEL_OPTIMIZED_TRANS_QUEUE_DEPTH, 0);
        if (s_flush_done_sem == NULL)
        {
            ESP_LOGE(TAG, "创建刷新同步信号量失败");
            return;
        }
    }

    lv_port_init_tx_chunk_buffers();

    // 设置颜色格式为RGB565（16位色深）
    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);

    // 设置刷新回调函数
    lv_display_set_flush_cb(s_display, lv_port_disp_flush);
    lv_display_add_event_cb(s_display, lv_port_rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    lv_display_set_buffers(s_display, disp_buf1, disp_buf2,
                           disp_buf_size * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    // 注册传输完成回调，启用异步刷新
    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = lvgl_port_flush_ready_callback,
    };
    co5300_panel_register_color_done_callback(&cbs, s_display);

    ESP_LOGI(TAG, "LVGL 9.3 单缓存显示驱动初始化完成 (PARTIAL/%d行, RGB565格式%s字节交换)",
             LV_PORT_FIXED_CHUNK_LINES2,
             LV_PORT_BYTE_SWAP_ENABLE ? "启用" : "禁用");
}

static void lv_port_init_tx_chunk_buffers(void)
{
    if (s_tx_chunk_buf_size != 0)
    {
        return;
    }

    const size_t tx_buf_size = LCD_WIDTH * LV_PORT_FIXED_CHUNK_LINES * sizeof(lv_color_t);

    for (uint32_t i = 0; i < LV_PORT_MAX_INFLIGHT_CHUNKS; ++i)
    {
        s_tx_chunk_bufs[i] = heap_caps_malloc(tx_buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (s_tx_chunk_bufs[i] == NULL)
        {
            ESP_LOGW(TAG, "LCD bounce buffer[%lu] 分配失败，回退到直接发送渲染缓冲", (unsigned long)i);
            for (uint32_t j = 0; j < i; ++j)
            {
                free(s_tx_chunk_bufs[j]);
                s_tx_chunk_bufs[j] = NULL;
            }
            s_tx_chunk_buf_size = 0;
            s_tx_chunk_buf_next = 0;
            return;
        }
    }

    s_tx_chunk_buf_size = tx_buf_size;
    s_tx_chunk_buf_next = 0;
    ESP_LOGI(TAG, "LCD bounce buffer: %lu x %zu bytes (Internal DMA)", (unsigned long)LV_PORT_MAX_INFLIGHT_CHUNKS, tx_buf_size);
}

static uint8_t *lv_port_prepare_tx_buffer(const uint8_t *px_map, uint32_t pixel_count, size_t color_bytes)
{
    if (px_map == NULL)
    {
        return NULL;
    }

    if (s_tx_chunk_buf_size >= color_bytes && s_tx_chunk_bufs[0] != NULL)
    {
        uint8_t *tx_px_map = s_tx_chunk_bufs[s_tx_chunk_buf_next];
        s_tx_chunk_buf_next = (s_tx_chunk_buf_next + 1U) % LV_PORT_MAX_INFLIGHT_CHUNKS;
        memcpy(tx_px_map, px_map, color_bytes);

        if (s_byte_swap_enabled)
        {
            lv_draw_sw_rgb565_swap(tx_px_map, pixel_count);
        }
        return tx_px_map;
    }

    return (uint8_t *)px_map;
}

/**
 * @brief 传输完成回调函数
 * @note 在ISR上下文中调用
 */
static bool IRAM_ATTR lvgl_port_flush_ready_callback(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    (void)panel_io;
    (void)edata;
    (void)user_ctx;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (s_flush_done_sem != NULL)
    {
        xSemaphoreGiveFromISR(s_flush_done_sem, &xHigherPriorityTaskWoken);
    }
    return xHigherPriorityTaskWoken == pdTRUE;
}

static void lv_port_rounder_event_cb(lv_event_t *e)
{
    lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
    if (area == NULL)
    {
        return;
    }

    uint16_t x1 = area->x1;
    uint16_t x2 = area->x2;
    uint16_t y1 = area->y1;
    uint16_t y2 = area->y2;

    // 参考 Waveshare BSP：将失效区域对齐到偶数边界，减少圆角/弧线边缘在局部刷新时的碎片化。
    area->x1 = (x1 >> 1) << 1;
    area->y1 = (y1 >> 1) << 1;
    area->x2 = ((x2 >> 1) << 1) + 1;
    area->y2 = ((y2 >> 1) << 1) + 1;
}

/**
 * @brief LVGL显示刷新回调函数 (LVGL 9.x API)
 * @param disp 显示对象指针
 * @param area 需要刷新的区域
 * @param px_map 像素数据指针 (uint8_t* 格式)
 */
void lv_port_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_err_t ret = ESP_OK;
    uint32_t pending_chunks = 0;

    if (s_flush_done_sem != NULL)
    {
        while (xSemaphoreTake(s_flush_done_sem, 0) == pdTRUE)
        {
        }
    }

#if LV_PORT_CHUNKED_TRANSFER_ENABLE
    uint32_t area_height_val = area->y2 - area->y1 + 1;

    // 简单判断：如果区域高度大于固定块大小，就进行分块传输
    if (area_height_val > LV_PORT_FIXED_CHUNK_LINES)
    {
        // 分块传输大区域
        uint32_t area_width = area->x2 - area->x1 + 1;
        uint32_t area_height = area->y2 - area->y1 + 1;
        uint32_t bytes_per_line = area_width * sizeof(uint16_t);
        uint32_t chunk_lines = LV_PORT_FIXED_CHUNK_LINES;

        ESP_LOGD(TAG, "Chunked transfer: %lux%lu area, %lu lines per chunk, inflight=%d",
                 area_width, area_height, chunk_lines, LV_PORT_MAX_INFLIGHT_CHUNKS);

        for (uint32_t y_offset = 0; y_offset < area_height; y_offset += chunk_lines)
        {
            uint32_t current_chunk_lines =
                (y_offset + chunk_lines > area_height) ? (area_height - y_offset) : chunk_lines;

            while (pending_chunks >= LV_PORT_MAX_INFLIGHT_CHUNKS)
            {
                ret = lv_port_wait_one_chunk_done(&pending_chunks, pdMS_TO_TICKS(200));
                if (ret != ESP_OK)
                {
                    break;
                }
            }
            if (ret != ESP_OK)
            {
                break;
            }

            lv_area_t chunk_area = {
                .x1 = area->x1,
                .y1 = area->y1 + y_offset,
                .x2 = area->x2,
                .y2 = area->y1 + y_offset + current_chunk_lines - 1};

            uint8_t *chunk_px_map = px_map + (y_offset * bytes_per_line);
            ret = lv_port_flush_area_with_sync(disp, &chunk_area, chunk_px_map);
            if (ret != ESP_OK)
            {
                break;
            }

            pending_chunks++;
        }
    }
    else
    {
        // 直接传输小区域
        ret = lv_port_flush_area_with_sync(disp, area, px_map);
        if (ret == ESP_OK)
        {
            pending_chunks = 1;
        }
    }
#else
    // 标准传输模式
    ret = lv_port_flush_area_with_sync(disp, area, px_map);
    if (ret == ESP_OK)
    {
        pending_chunks = 1;
    }
#endif

    while (ret == ESP_OK && pending_chunks > 0)
    {
        ret = lv_port_wait_one_chunk_done(&pending_chunks, pdMS_TO_TICKS(200));
    }

    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Display flush failed: %s", esp_err_to_name(ret));
        lv_display_flush_ready(disp);
        return;
    }

#if CO5300_PANEL_USE_TE_SIGNAL
    // 整帧刷新完成，下一帧重新等待TE
    s_frame_ctx.frame_start = true;
#endif
    lv_display_flush_ready(disp);
}

/**
 * @brief 带同步的区域刷新
 * @param disp 显示对象
 * @param area 刷新区域
 * @param px_map 像素数据
 * @return ESP_OK: 成功, 其他: 失败
 */
static esp_err_t lv_port_flush_area_with_sync(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)disp;
#if CO5300_PANEL_USE_TE_SIGNAL
    // 帧首等TE优化：只在帧的第一个area时等待TE信号
    if (s_frame_ctx.frame_start)
    {
        ESP_LOGV(TAG, "Frame start, waiting for TE signal...");

        esp_err_t te_ret = co5300_panel_wait_te_signal(100);
        if (te_ret == ESP_OK)
        {
            s_frame_ctx.te_sync_count++;
            ESP_LOGV(TAG, "TE sync OK");
        }
        else
        {
            s_frame_ctx.te_timeout_count++;
            ESP_LOGD(TAG, "TE timeout (frame start)");
        }

        // 标记帧已开始，后续area不再等待TE
        s_frame_ctx.frame_start = false;
    }

    s_frame_ctx.flush_count++;
#endif

    // 计算像素数量用于字节交换和DMA同步
    uint32_t pixel_count = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);
    size_t color_bytes = pixel_count * sizeof(uint16_t);
    uint8_t *tx_px_map = lv_port_prepare_tx_buffer(px_map, pixel_count, color_bytes);

    // 若没有 bounce buffer，只能回退到原地字节交换
    if (tx_px_map == px_map && s_byte_swap_enabled)
    {
        lv_draw_sw_rgb565_swap(px_map, pixel_count);
    }

    // 调用底层面板驱动进行像素数据传输
    return esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, tx_px_map);
}

#if LV_PORT_CHUNKED_TRANSFER_ENABLE
/**
 * @brief 简化的分块传输
 * @param disp 显示对象
 * @param area 刷新区域
 * @param px_map 像素数据
 * @return ESP_OK: 成功, 其他: 失败
 */
static esp_err_t lv_port_flush_area_chunked_simple(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)disp;
    (void)area;
    (void)px_map;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif // LV_PORT_CHUNKED_TRANSFER_ENABLE

static esp_err_t lv_port_wait_one_chunk_done(uint32_t *pending_chunks, TickType_t timeout_ticks)
{
    if (pending_chunks == NULL || *pending_chunks == 0)
    {
        return ESP_OK;
    }
    if (s_flush_done_sem == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_flush_done_sem, timeout_ticks) != pdTRUE)
    {
        ESP_LOGW(TAG, "等待LCD颜色传输完成超时");
        return ESP_ERR_TIMEOUT;
    }
    (*pending_chunks)--;
    return ESP_OK;
}

/* ========== LVGL输入设备相关函数 ========== */

/**
 * @brief LVGL输入设备读取回调函数 (LVGL 9.2 API)
 * @param indev 输入设备对象指针
 * @param data 输入数据结构指针
 */
static void lv_port_indev_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev; // 当前实现中未使用indev参数

    uint16_t x[1] = {0}, y[1] = {0};
    uint8_t point_num = 0;
    esp_err_t ret = touch_ft5x06_read_points(x, y, &point_num, 1);

    if (ret == ESP_OK && point_num > 0)
    {
        // 记录并上报本次有效坐标
        s_last_x = x[0];
        s_last_y = y[0];
        data->point.x = s_last_x;
        data->point.y = s_last_y;
        data->state = LV_INDEV_STATE_PRESSED;
    }
    else
    {
        // 无触摸/读取失败：保持最近一次有效坐标，状态为“未按下”
        data->point.x = s_last_x;
        data->point.y = s_last_y;
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/**
 * @brief 初始化LVGL输入设备 (LVGL 9.x API)
 * @details 创建触摸输入设备对象并设置回调函数
 */
void lv_port_indev_init(void)
{
    // LVGL 9.x API：创建输入设备对象
    lv_indev_t *indev = lv_indev_create();

    // 设置输入设备类型为指针(触摸)
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);

    // 设置读取回调函数
    lv_indev_set_read_cb(indev, lv_port_indev_read);

    ESP_LOGI(TAG, "LVGL 9.3 输入设备初始化完成");
}

/* ========== 硬件初始化相关函数 ========== */

/**
 * @brief 初始化 CO5300 显示面板（QSPI）
 * @details 通过 esp_lcd_co5300 驱动初始化面板与 QSPI 总线
 */
void lv_port_panel_init(void)
{
    // 保留函数名与结构，但内部改为初始化 CO5300 面板
    // 初始化底层面板，并获取原生句柄
    if (co5300_panel_init() == ESP_OK)
    {
        struct esp_lcd_panel_io_t *io = NULL;
        struct esp_lcd_panel_t *panel = NULL;
        if (co5300_panel_get_raw(&io, &panel) == ESP_OK)
        {
            s_panel = (esp_lcd_panel_handle_t)panel;
            // 设置显示偏移 - 向右偏移20像素
            ESP_LOGI(TAG, "设置显示向右偏移20像素");
            esp_err_t ret = esp_lcd_panel_set_gap(s_panel, 23, 0);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "设置显示偏移失败: %s", esp_err_to_name(ret));
            }
            else
            {
                ESP_LOGI(TAG, "CO5300 面板初始化完成");
            }
        }
        else
        {
            ESP_LOGE(TAG, "获取面板句柄失败");
        }
    }
    else
    {
        ESP_LOGE(TAG, "CO5300 面板初始化失败");
    }
}

/**
 * @brief 初始化 FT5x06 触摸硬件（I2C）
 * @details 通过 esp_lcd_touch_ft5x06 初始化并设置坐标范围
 */
void lv_port_touch_init(void)
{
    // 保留函数名与结构，但内部改为初始化 FT5x06 触摸
    if (touch_ft5x06_init() == ESP_OK)
    {
        if (touch_ft5x06_get_handle(&s_touch) == ESP_OK)
        {
            ESP_LOGI(TAG, "FT5x06 触摸初始化完成");
        }
        else
        {
            ESP_LOGE(TAG, "获取触摸句柄失败");
            s_touch = NULL;
        }
    }
    else
    {
        ESP_LOGE(TAG, "FT5x06 触摸初始化失败");
        s_touch = NULL;
    }
}

/* ========== LVGL定时器相关函数 ========== */

/**
 * @brief LVGL定时器回调函数
 * @param arg 定时器参数(时间间隔指针)
 * @details 定期调用lv_tick_inc()为LVGL提供时间基准
 */
void lv_port_tick_cb(void *arg)
{
    uint32_t tick_interval = *((uint32_t *)arg);
    lv_tick_inc(tick_interval); // 增加LVGL时钟计数
}

/**
 * @brief 初始化LVGL定时器
 * @details 创建ESP32定时器为LVGL提供5ms间隔的时钟基准
 */
void lv_port_tick_init(void)
{
    static uint32_t tick_interval = 5; // 5ms间隔

    // 定时器配置参数
    const esp_timer_create_args_t arg = {
        .arg = &tick_interval,             // 回调参数
        .callback = lv_port_tick_cb,       // 回调函数
        .name = "lvgl",                    // 定时器名称
        .dispatch_method = ESP_TIMER_TASK, // 在定时器任务中执行
        .skip_unhandled_events = true,     // 跳过未处理事件
    };

    esp_timer_handle_t timer_handle;
    // 创建定时器
    esp_timer_create(&arg, &timer_handle);
    // 启动周期定时器(5ms周期)
    esp_timer_start_periodic(timer_handle, tick_interval * 1000);
}

/* ========== LVGL移植层主初始化函数 ========== */

/* ========== LVGL移植层主初始化函数 ========== */

/**
 * @brief LVGL移植层总初始化函数
 */
void lv_port_init_small(void)
{
    lv_init();            // 初始化LVGL库
    lv_port_panel_init(); // 初始化显示硬件
    lv_port_touch_init(); // 初始化触摸硬件
    if (LV_PORT_FIXED_CHUNK_LINES23)
    {
        lv_port_disp_init_small(); // 片内ram
    }
    else
    {
        lv_port_disp_init_single(); // 片外ram
    }

    lv_port_indev_init(); // 初始化输入设备驱动
    lv_port_tick_init();  // 初始化定时器
}
