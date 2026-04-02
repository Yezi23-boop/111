#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "co5300_panel_defaults.h"
#include "lv_port_config.h"
#include "lvgl.h"

#define LV_PORT_TAG "lv_port"

extern lv_display_t *s_display;
extern esp_lcd_panel_handle_t s_panel;
extern void *s_touch;
extern int16_t s_last_x;
extern int16_t s_last_y;
extern bool s_byte_swap_enabled;
extern SemaphoreHandle_t s_flush_done_sem;
extern uint8_t *s_tx_chunk_bufs[LV_PORT_MAX_INFLIGHT_CHUNKS];
extern size_t s_tx_chunk_buf_size;
extern uint32_t s_tx_chunk_buf_next;

#if CO5300_PANEL_USE_TE_SIGNAL
typedef struct
{
    bool frame_start;
    uint32_t flush_count;
    uint32_t te_sync_count;
    uint32_t te_timeout_count;
} frame_sync_ctx_t;

extern frame_sync_ctx_t s_frame_ctx;
#endif

void lv_port_disp_init_small(void);
void lv_port_disp_init_single(void);
void lv_port_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
void lv_port_panel_init(void);
void lv_port_touch_init(void);
void lv_port_indev_init(void);
void lv_port_tick_init(void);
