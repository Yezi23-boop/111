#define SDL_MAIN_HANDLED

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>

#include "gui_guider.h"
#include "events_init.h"
#include "ai_ui_controller.h"
#include "memory_watch_controller.h"
#include "mini_games_controller.h"
#include "drivers/sdl/lv_sdl_keyboard.h"
#include "drivers/sdl/lv_sdl_mouse.h"
#include "drivers/sdl/lv_sdl_mousewheel.h"
#include "drivers/sdl/lv_sdl_window.h"
#include "lvgl.h"

#define PREVIEW_W 410
#define PREVIEW_H 502
#define PREVIEW_CORNER_RADIUS 64

static uint8_t s_screen_mask_data[PREVIEW_W * PREVIEW_H * 4];
static lv_image_dsc_t s_screen_mask_dsc;
static lv_obj_t *s_screen_mask = NULL;

static bool preview_point_inside_rounded_screen(int32_t x, int32_t y)
{
    const int32_t r = PREVIEW_CORNER_RADIUS;
    int32_t cx = x;
    int32_t cy = y;

    if (x < r) {
        cx = r;
    } else if (x >= PREVIEW_W - r) {
        cx = PREVIEW_W - r - 1;
    }

    if (y < r) {
        cy = r;
    } else if (y >= PREVIEW_H - r) {
        cy = PREVIEW_H - r - 1;
    }

    const int32_t dx = x - cx;
    const int32_t dy = y - cy;
    return (dx * dx + dy * dy) <= (r * r);
}

static void preview_create_screen_mask(void)
{
    for (int32_t y = 0; y < PREVIEW_H; ++y) {
        for (int32_t x = 0; x < PREVIEW_W; ++x) {
            uint8_t *px = &s_screen_mask_data[((y * PREVIEW_W) + x) * 4];
            px[0] = 0;
            px[1] = 0;
            px[2] = 0;
            px[3] = preview_point_inside_rounded_screen(x, y) ? 0 : 255;
        }
    }

    s_screen_mask_dsc = (lv_image_dsc_t) {
        .header = {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_ARGB8888,
            .flags = 0,
            .w = PREVIEW_W,
            .h = PREVIEW_H,
            .stride = PREVIEW_W * 4,
        },
        .data_size = sizeof(s_screen_mask_data),
        .data = s_screen_mask_data,
    };

    s_screen_mask = lv_image_create(lv_layer_top());
    lv_image_set_src(s_screen_mask, &s_screen_mask_dsc);
    lv_obj_set_pos(s_screen_mask, 0, 0);
    lv_obj_clear_flag(s_screen_mask, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_screen_mask, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(s_screen_mask);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    lv_init();

    lv_display_t *display = lv_sdl_window_create(PREVIEW_W, PREVIEW_H);
    lv_sdl_window_set_title(display, "Agent Preview - GUI Guider Generated");
    lv_sdl_window_set_resizeable(display, false);

    lv_sdl_mouse_create();
    lv_sdl_mousewheel_create();
    lv_sdl_keyboard_create();

    setup_ui(&guider_ui);
    memory_watch_controller_init(&guider_ui);
    events_init(&guider_ui);
    ai_ui_controller_init(&guider_ui);
    mini_games_controller_init(&guider_ui);
    preview_create_screen_mask();

    uint32_t last_tick = SDL_GetTicks();
    bool running = true;
    while (running) {
        SDL_Delay(5);
        uint32_t now = SDL_GetTicks();
        lv_tick_inc(now - last_tick);
        last_tick = now;
        lv_timer_handler();
        memory_watch_controller_poll_ui();
        mini_games_controller_poll_ui();
        if (s_screen_mask != NULL && lv_obj_is_valid(s_screen_mask)) {
            lv_obj_move_foreground(s_screen_mask);
        }

        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }
    }

    lv_sdl_quit();
    return 0;
}
