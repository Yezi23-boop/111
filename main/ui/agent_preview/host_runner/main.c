#define SDL_MAIN_HANDLED

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>

#include "apple_watch_s5_preview.h"
#include "drivers/sdl/lv_sdl_keyboard.h"
#include "drivers/sdl/lv_sdl_mouse.h"
#include "drivers/sdl/lv_sdl_mousewheel.h"
#include "drivers/sdl/lv_sdl_window.h"
#include "lvgl.h"

#define PREVIEW_W 410
#define PREVIEW_H 502

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    lv_init();

    lv_display_t *display = lv_sdl_window_create(PREVIEW_W, PREVIEW_H);
    lv_sdl_window_set_title(display, "Agent Preview - Apple Watch S5 Style");
    lv_sdl_window_set_resizeable(display, false);

    lv_sdl_mouse_create();
    lv_sdl_mousewheel_create();
    lv_sdl_keyboard_create();

    lv_obj_t *screen = NULL;
    setup_scr_agent_preview_apple_watch_s5(&screen);
    lv_screen_load(screen);

    uint32_t last_tick = SDL_GetTicks();
    bool running = true;
    while (running) {
        SDL_Delay(5);
        uint32_t now = SDL_GetTicks();
        lv_tick_inc(now - last_tick);
        last_tick = now;
        lv_timer_handler();

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
