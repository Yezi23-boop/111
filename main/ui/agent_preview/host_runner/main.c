#define SDL_MAIN_HANDLED

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>

#include "gui_guider.h"
#include "events_init.h"
#include "drivers/sdl/lv_sdl_keyboard.h"
#include "drivers/sdl/lv_sdl_mouse.h"
#include "drivers/sdl/lv_sdl_mousewheel.h"
#include "drivers/sdl/lv_sdl_window.h"
#include "lvgl.h"

#define PREVIEW_W 410
#define PREVIEW_H 502

#include <windows.h>
#include <signal.h>

LONG WINAPI CrashHandler(EXCEPTION_POINTERS *ExceptionInfo) {
    freopen("crash_log.txt", "a", stdout);
    printf("CRASH CAUGHT! Code: 0x%08X at 0x%p\n",
        ExceptionInfo->ExceptionRecord->ExceptionCode,
        ExceptionInfo->ExceptionRecord->ExceptionAddress);
    fflush(stdout);
    ExitProcess(1);
    return EXCEPTION_EXECUTE_HANDLER;
}

void sig_handler(int signum) {
    freopen("crash_log.txt", "a", stdout);
    printf("SIGNAL CAUGHT! signum=%d\n", signum);
    fflush(stdout);
    ExitProcess(1);
}

void my_log_cb(lv_log_level_t level, const char * buf) {
    FILE *f = fopen("crash_log.txt", "a");
    if(f) {
        fprintf(f, "[LVGL] %s", buf);
        fclose(f);
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    lv_init();
    lv_log_register_print_cb(my_log_cb);
    AddVectoredExceptionHandler(1, CrashHandler);
    freopen("crash_log.txt", "w", stderr);
    freopen("crash_log.txt", "a", stdout);
    printf("Starting simulator...\n");
    fflush(stdout);

    lv_display_t *display = lv_sdl_window_create(PREVIEW_W, PREVIEW_H);
    lv_sdl_window_set_title(display, "Agent Preview - GUI Guider Generated");
    lv_sdl_window_set_resizeable(display, false);

    printf("Initializing mouse/keyboard...\n");
    fflush(stdout);
    lv_sdl_mouse_create();
    lv_sdl_mousewheel_create();
    lv_sdl_keyboard_create();

    printf("Calling setup_ui...\n");
    fflush(stdout);
    setup_ui(&guider_ui);

    printf("Calling events_init...\n");
    fflush(stdout);
    events_init(&guider_ui);

    printf("Entering main loop...\n");
    fflush(stdout);
    uint32_t last_tick = SDL_GetTicks();
    bool running = true;
    while (running) {
        SDL_Delay(5);
        uint32_t now = SDL_GetTicks();
        lv_tick_inc(now - last_tick);
        last_tick = now;
        printf("Calling lv_timer_handler\n"); fflush(stdout);
        lv_timer_handler();
        printf("Done lv_timer_handler\n"); fflush(stdout);

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
