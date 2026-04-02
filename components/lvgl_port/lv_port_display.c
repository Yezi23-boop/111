/**
 * @file lv_port_display.c
 * @brief LVGL 显示链路实现
 */

#include <inttypes.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "co5300_panel.h"
#include "co5300_panel_defaults.h"
#include "lv_port.h"
#include "lv_port_internal.h"

static bool lvgl_port_flush_ready_callback(esp_lcd_panel_io_handle_t panel_io,
                                           esp_lcd_panel_io_event_data_t *edata,
                                           void *user_ctx);
static esp_err_t lv_port_flush_area_with_sync(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static esp_err_t lv_port_wait_one_chunk_done(uint32_t *pending_chunks, TickType_t timeout_ticks);
static void lv_port_rounder_event_cb(lv_event_t *e);
static void lv_port_init_tx_chunk_buffers(void);
static uint8_t *lv_port_prepare_tx_buffer(const uint8_t *px_map, uint32_t pixel_count, size_t color_bytes);

void lv_port_disp_init_small(void)
{
    const size_t disp_buf_size = LCD_WIDTH * LV_PORT_FIXED_CHUNK_LINES1;

    ESP_LOGI(LV_PORT_TAG,
             "Small buffer size: %zu pixels (%.1f KB each)",
             disp_buf_size,
             (disp_buf_size * sizeof(lv_color_t)) / 1024.0f);

    lv_color_t *disp1 = heap_caps_malloc(disp_buf_size * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    lv_color_t *disp2 = heap_caps_malloc(disp_buf_size * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);

    if (!disp1)
    {
        disp1 = heap_caps_malloc(disp_buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
    }
    if (!disp2)
    {
        disp2 = heap_caps_malloc(disp_buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
    }

    ESP_LOGI(LV_PORT_TAG,
             "Small Buffer1: %s, Buffer2: %s",
             esp_ptr_external_ram(disp1) ? "PSRAM" : "Internal",
             esp_ptr_external_ram(disp2) ? "PSRAM" : "Internal");

    s_display = lv_display_create(LCD_WIDTH, LCD_HEIGHT);

    if (s_flush_done_sem == NULL)
    {
        s_flush_done_sem = xSemaphoreCreateCounting(CO5300_PANEL_OPTIMIZED_TRANS_QUEUE_DEPTH, 0);
        if (s_flush_done_sem == NULL)
        {
            ESP_LOGE(LV_PORT_TAG, "创建刷新同步信号量失败");
            return;
        }
    }

    lv_port_init_tx_chunk_buffers();

    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_display, lv_port_disp_flush);
    lv_display_add_event_cb(s_display, lv_port_rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    lv_display_set_buffers(s_display,
                           disp1,
                           disp2,
                           disp_buf_size * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = lvgl_port_flush_ready_callback,
    };
    co5300_panel_register_color_done_callback(&cbs, s_display);
}

void lv_port_disp_init_single(void)
{
    const size_t disp_buf_size = LCD_WIDTH * LV_PORT_FIXED_CHUNK_LINES2;

    ESP_LOGI(LV_PORT_TAG,
             "Single buffer size: %zu pixels (%.1f KB)",
             disp_buf_size,
             (disp_buf_size * sizeof(lv_color_t)) / 1024.0f);
    lv_color_t *disp_buf1 = heap_caps_malloc(disp_buf_size * sizeof(lv_color_t), MALLOC_CAP_32BIT | MALLOC_CAP_SPIRAM);
    lv_color_t *disp_buf2 = heap_caps_malloc(disp_buf_size * sizeof(lv_color_t), MALLOC_CAP_32BIT | MALLOC_CAP_SPIRAM);

    if (!disp_buf1)
    {
        ESP_LOGE(LV_PORT_TAG, "单缓存1分配失败");
        return;
    }
    if (!disp_buf2)
    {
        ESP_LOGE(LV_PORT_TAG, "单缓存2分配失败");
        return;
    }

    ESP_LOGI(LV_PORT_TAG,
             "Single Buffer1: %s, Buffer2: %s",
             esp_ptr_external_ram(disp_buf1) ? "PSRAM" : "Internal",
             esp_ptr_external_ram(disp_buf2) ? "PSRAM" : "Internal");

    s_display = lv_display_create(LCD_WIDTH, LCD_HEIGHT);

    if (s_flush_done_sem == NULL)
    {
        s_flush_done_sem = xSemaphoreCreateCounting(CO5300_PANEL_OPTIMIZED_TRANS_QUEUE_DEPTH, 0);
        if (s_flush_done_sem == NULL)
        {
            ESP_LOGE(LV_PORT_TAG, "创建刷新同步信号量失败");
            return;
        }
    }

    lv_port_init_tx_chunk_buffers();

    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_display, lv_port_disp_flush);
    lv_display_add_event_cb(s_display, lv_port_rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    lv_display_set_buffers(s_display,
                           disp_buf1,
                           disp_buf2,
                           disp_buf_size * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = lvgl_port_flush_ready_callback,
    };
    co5300_panel_register_color_done_callback(&cbs, s_display);

    ESP_LOGI(LV_PORT_TAG,
             "LVGL 9.3 单缓存显示驱动初始化完成 (PARTIAL/%d行, RGB565格式%s字节交换)",
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
            ESP_LOGW(LV_PORT_TAG, "LCD bounce buffer[%lu] 分配失败，回退到直接发送渲染缓冲", (unsigned long)i);
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
    ESP_LOGI(LV_PORT_TAG,
             "LCD bounce buffer: %lu x %zu bytes (Internal DMA)",
             (unsigned long)LV_PORT_MAX_INFLIGHT_CHUNKS,
             tx_buf_size);
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

static bool IRAM_ATTR lvgl_port_flush_ready_callback(esp_lcd_panel_io_handle_t panel_io,
                                                     esp_lcd_panel_io_event_data_t *edata,
                                                     void *user_ctx)
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

    area->x1 = (x1 >> 1) << 1;
    area->y1 = (y1 >> 1) << 1;
    area->x2 = ((x2 >> 1) << 1) + 1;
    area->y2 = ((y2 >> 1) << 1) + 1;
}

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

    if (area_height_val > LV_PORT_FIXED_CHUNK_LINES)
    {
        uint32_t area_width = area->x2 - area->x1 + 1;
        uint32_t area_height = area->y2 - area->y1 + 1;
        uint32_t bytes_per_line = area_width * sizeof(uint16_t);
        uint32_t chunk_lines = LV_PORT_FIXED_CHUNK_LINES;

        ESP_LOGD(LV_PORT_TAG, "Chunked transfer: %lux%lu area, %lu lines per chunk, inflight=%d",
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
                .y2 = area->y1 + y_offset + current_chunk_lines - 1,
            };

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
        ret = lv_port_flush_area_with_sync(disp, area, px_map);
        if (ret == ESP_OK)
        {
            pending_chunks = 1;
        }
    }
#else
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
        ESP_LOGW(LV_PORT_TAG, "Display flush failed: %s", esp_err_to_name(ret));
        lv_display_flush_ready(disp);
        return;
    }

#if CO5300_PANEL_USE_TE_SIGNAL
    s_frame_ctx.frame_start = true;
#endif
    lv_display_flush_ready(disp);
}

static esp_err_t lv_port_flush_area_with_sync(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)disp;
#if CO5300_PANEL_USE_TE_SIGNAL
    if (s_frame_ctx.frame_start)
    {
        ESP_LOGV(LV_PORT_TAG, "Frame start, waiting for TE signal...");

        esp_err_t te_ret = co5300_panel_wait_te_signal(100);
        if (te_ret == ESP_OK)
        {
            s_frame_ctx.te_sync_count++;
            ESP_LOGV(LV_PORT_TAG, "TE sync OK");
        }
        else
        {
            s_frame_ctx.te_timeout_count++;
            ESP_LOGD(LV_PORT_TAG, "TE timeout (frame start)");
        }

        s_frame_ctx.frame_start = false;
    }

    s_frame_ctx.flush_count++;
#endif

    uint32_t pixel_count = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);
    size_t color_bytes = pixel_count * sizeof(uint16_t);
    uint8_t *tx_px_map = lv_port_prepare_tx_buffer(px_map, pixel_count, color_bytes);

    if (tx_px_map == px_map && s_byte_swap_enabled)
    {
        lv_draw_sw_rgb565_swap(px_map, pixel_count);
    }

    return esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, tx_px_map);
}

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
        ESP_LOGW(LV_PORT_TAG, "等待LCD颜色传输完成超时");
        return ESP_ERR_TIMEOUT;
    }
    (*pending_chunks)--;
    return ESP_OK;
}

void lv_port_panel_init(void)
{
    if (co5300_panel_init() == ESP_OK)
    {
        struct esp_lcd_panel_io_t *io = NULL;
        struct esp_lcd_panel_t *panel = NULL;
        if (co5300_panel_get_raw(&io, &panel) == ESP_OK)
        {
            s_panel = (esp_lcd_panel_handle_t)panel;
            ESP_LOGI(LV_PORT_TAG, "设置显示向右偏移20像素");
            esp_err_t ret = esp_lcd_panel_set_gap(s_panel, 23, 0);
            if (ret != ESP_OK)
            {
                ESP_LOGE(LV_PORT_TAG, "设置显示偏移失败: %s", esp_err_to_name(ret));
            }
            else
            {
                ESP_LOGI(LV_PORT_TAG, "CO5300 面板初始化完成");
            }
        }
        else
        {
            ESP_LOGE(LV_PORT_TAG, "获取面板句柄失败");
        }
    }
    else
    {
        ESP_LOGE(LV_PORT_TAG, "CO5300 面板初始化失败");
    }
}
