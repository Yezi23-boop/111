/**
 * @file lv_port.c
 * @brief LVGL 移植层公共状态与总入口
 */

#include "lv_port.h"
#include "lv_port_config.h"
#include "lv_port_internal.h"

lv_display_t *s_display = NULL;
esp_lcd_panel_handle_t s_panel = NULL;
void *s_touch = NULL;
int16_t s_last_x = 0;
int16_t s_last_y = 0;
bool s_byte_swap_enabled = LV_PORT_BYTE_SWAP_ENABLE;
SemaphoreHandle_t s_flush_done_sem = NULL;
uint8_t *s_tx_chunk_bufs[LV_PORT_MAX_INFLIGHT_CHUNKS] = {0};
size_t s_tx_chunk_buf_size = 0;
uint32_t s_tx_chunk_buf_next = 0;

#if CO5300_PANEL_USE_TE_SIGNAL
frame_sync_ctx_t s_frame_ctx = {
    .frame_start = true,
    .flush_count = 0,
    .te_sync_count = 0,
    .te_timeout_count = 0,
};
#endif

void lv_port_init_small(void)
{
    lv_init();
    lv_port_panel_init();
    lv_port_touch_init();
    if (LV_PORT_FIXED_CHUNK_LINES23)
    {
        lv_port_disp_init_small();
    }
    else
    {
        lv_port_disp_init_single();
    }

    lv_port_indev_init();
    lv_port_tick_init();
}
