#pragma once
#include "lvgl.h"
#include <stdint.h>
lv_font_t *cbin_font_create(uint8_t *data);
void cbin_font_delete(lv_font_t *font);
