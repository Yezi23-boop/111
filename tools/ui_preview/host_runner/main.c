#define SDL_MAIN_HANDLED

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gui_guider.h"
#include "events_init.h"
#include "widgets_init.h"
#include "ai_ui_controller.h"
#include "danger_detection_controller.h"
#include "memory_watch_controller.h"
#include "mini_games_controller.h"
#include "ota_maintenance_view.h"
#include "drivers/sdl/lv_sdl_keyboard.h"
#include "drivers/sdl/lv_sdl_mouse.h"
#include "drivers/sdl/lv_sdl_mousewheel.h"
#include "drivers/sdl/lv_sdl_window.h"
#include "watch_notification_center.h"
#include "lvgl.h"
#include "libs/lodepng/lodepng.h"

#define PREVIEW_W 410
#define PREVIEW_H 502
#define PREVIEW_CORNER_RADIUS 120

static uint8_t s_screen_mask_data[PREVIEW_W * PREVIEW_H * 4];
static lv_image_dsc_t s_screen_mask_dsc;
static lv_obj_t *s_screen_mask = NULL;
static SDL_atomic_t s_preview_quit_requested;

typedef struct {
    bool open_hermes;
    bool open_hermes_inbox;
    bool open_hermes_detail;
    bool open_ai;
    bool open_calendar;
    bool open_ota;
    bool open_function;
    bool open_danger;
    bool open_games;
    uint8_t open_game_index;
    const char *capture_path;
} preview_args_t;

static int preview_sdl_event_watch(void *userdata, SDL_Event *event)
{
    (void)userdata;

    if (event != NULL && event->type == SDL_QUIT)
    {
        SDL_AtomicSet(&s_preview_quit_requested, 1);
    }

    return 1;
}

static bool preview_point_inside_rounded_screen(int32_t x, int32_t y)
{
    const int32_t r = PREVIEW_CORNER_RADIUS;
    int32_t cx = x;
    int32_t cy = y;

    if (x < r)
    {
        cx = r;
    }
    else if (x >= PREVIEW_W - r)
    {
        cx = PREVIEW_W - r - 1;
    }

    if (y < r)
    {
        cy = r;
    }
    else if (y >= PREVIEW_H - r)
    {
        cy = PREVIEW_H - r - 1;
    }

    const int32_t dx = x - cx;
    const int32_t dy = y - cy;
    return (dx * dx + dy * dy) <= (r * r);
}

static void preview_create_screen_mask(void)
{
    for (int32_t y = 0; y < PREVIEW_H; ++y)
    {
        for (int32_t x = 0; x < PREVIEW_W; ++x)
        {
            uint8_t *px = &s_screen_mask_data[((y * PREVIEW_W) + x) * 4];
            px[0] = 0;
            px[1] = 0;
            px[2] = 0;
            px[3] = preview_point_inside_rounded_screen(x, y) ? 0 : 255;
        }
    }

    s_screen_mask_dsc = (lv_image_dsc_t){
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

static preview_args_t preview_parse_args(int argc, char **argv)
{
    preview_args_t args = {0};

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--open-hermes") == 0)
        {
            args.open_hermes = true;
        }
        else if (strcmp(argv[i], "--open-hermes-inbox") == 0)
        {
            args.open_hermes_inbox = true;
        }
        else if (strcmp(argv[i], "--open-hermes-detail") == 0)
        {
            args.open_hermes_detail = true;
        }
        else if (strcmp(argv[i], "--open-ai") == 0)
        {
            args.open_ai = true;
        }
        else if (strcmp(argv[i], "--open-calendar") == 0)
        {
            args.open_calendar = true;
        }
        else if (strcmp(argv[i], "--open-ota") == 0)
        {
            args.open_ota = true;
        }
        else if (strcmp(argv[i], "--open-function") == 0)
        {
            args.open_function = true;
        }
        else if (strcmp(argv[i], "--open-danger") == 0)
        {
            args.open_danger = true;
        }
        else if (strcmp(argv[i], "--open-games") == 0)
        {
            args.open_games = true;
        }
        else if (strcmp(argv[i], "--open-game-2048") == 0)
        {
            args.open_game_index = 1;
        }
        else if (strcmp(argv[i], "--open-game-flappy") == 0)
        {
            args.open_game_index = 2;
        }
        else if (strcmp(argv[i], "--open-game-dino") == 0)
        {
            args.open_game_index = 3;
        }
        else if (strcmp(argv[i], "--capture") == 0 && i + 1 < argc)
        {
            args.capture_path = argv[++i];
        }
    }

    return args;
}

static void preview_tick_once(uint32_t *last_tick)
{
    SDL_Delay(5);
    uint32_t now = SDL_GetTicks();
    lv_tick_inc(now - *last_tick);
    *last_tick = now;
    lv_timer_handler();
    memory_watch_controller_poll_ui();
    mini_games_controller_poll_ui();
    danger_detection_controller_poll_ui();
    watch_nc_poll(false, false);
    if (s_screen_mask != NULL && lv_obj_is_valid(s_screen_mask))
    {
        lv_obj_move_foreground(s_screen_mask);
    }
}

static bool preview_capture_png(lv_display_t *display, const char *path)
{
    if (display == NULL || path == NULL)
    {
        return false;
    }

    SDL_Renderer *renderer = (SDL_Renderer *)lv_sdl_window_get_renderer(display);
    if (renderer == NULL)
    {
        return false;
    }

    uint8_t *pixels = malloc(PREVIEW_W * PREVIEW_H * 4);
    if (pixels == NULL)
    {
        return false;
    }

    /*
     * SDL_PIXELFORMAT_ABGR8888 stores bytes as R,G,B,A on little-endian Windows,
     * which is the exact raw order expected by lodepng_encode32_file().
     */
    bool ok = SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ABGR8888,
                                   pixels, PREVIEW_W * 4) == 0;
    if (!ok)
    {
        fprintf(stderr, "SDL_RenderReadPixels failed: %s\n", SDL_GetError());
    }
    if (ok)
    {
        unsigned char *png = NULL;
        size_t png_size = 0;
        unsigned png_error = lodepng_encode32(&png, &png_size, pixels, PREVIEW_W, PREVIEW_H);
        if (png_error != 0)
        {
            fprintf(stderr, "lodepng_encode32 failed: %u\n", png_error);
            ok = false;
        }
        else
        {
            FILE *fp = fopen(path, "wb");
            if (fp == NULL)
            {
                fprintf(stderr, "fopen failed for capture path: %s\n", path);
                ok = false;
            }
            else
            {
                ok = fwrite(png, 1, png_size, fp) == png_size;
                if (!ok)
                {
                    fprintf(stderr, "fwrite failed for capture path: %s\n", path);
                }
                fclose(fp);
            }
        }
        lv_free(png);
    }

    free(pixels);
    return ok;
}

int main(int argc, char **argv)
{
    preview_args_t args = preview_parse_args(argc, argv);

    lv_init();

    lv_display_t *display = lv_sdl_window_create(PREVIEW_W, PREVIEW_H);
    lv_sdl_window_set_title(display, "Agent Preview - GUI Guider Generated");
    lv_sdl_window_set_resizeable(display, false);

    lv_sdl_mouse_create();
    lv_sdl_mousewheel_create();
    lv_sdl_keyboard_create();
    SDL_AtomicSet(&s_preview_quit_requested, 0);
    SDL_AddEventWatch(preview_sdl_event_watch, NULL);

    setup_ui(&guider_ui);
    memory_watch_controller_init(&guider_ui);
    events_init(&guider_ui);
    ai_ui_controller_init(&guider_ui);
    danger_detection_controller_init(&guider_ui);
    mini_games_controller_init(&guider_ui);
    preview_create_screen_mask();
    static const watch_nc_config_t kMockNcCfg = {0};
    watch_nc_init(&kMockNcCfg);
    if (args.open_hermes)
    {
        memory_watch_controller_open();
    }
    else if (args.open_hermes_inbox)
    {
        memory_watch_controller_open_via_notification(WATCH_NC_NAV_INBOX_LIST, NULL);
    }
    else if (args.open_hermes_detail)
    {
        memory_watch_controller_open_via_notification(WATCH_NC_NAV_INBOX_DETAIL,
                                                      "preview-006");
    }
    else if (args.open_ai)
    {
        ai_ui_open();
    }
    else if (args.open_calendar)
    {
        setup_scr_screen_time(&guider_ui);
        lv_label_set_text(guider_ui.screen_time_datetext_1, "2026/09/09");
        lv_screen_load(guider_ui.screen_time);
    }
    else if (args.open_ota)
    {
        (void)ota_maintenance_view_init();
        (void)ota_maintenance_view_open();
    }
    else if (args.open_function)
    {
        setup_scr_screen_main_function_page(&guider_ui);
        ota_maintenance_view_bind_entry();
        lv_obj_set_x(guider_ui.screen_main_tileview_1_main, -PREVIEW_W);
        lv_obj_set_x(guider_ui.screen_main_tileview_1_Function, 0);
        lv_obj_t *entry = lv_obj_get_child(
            guider_ui.screen_main_Function_main,
            lv_obj_get_child_cnt(guider_ui.screen_main_Function_main) - 1);
        lv_obj_scroll_to_view(entry, LV_ANIM_OFF);
    }
    else if (args.open_danger)
    {
        danger_detection_ui_open();
    }
    else if (args.open_games)
    {
        mini_games_controller_open();
    }
    else if (args.open_game_index != 0)
    {
        mini_games_controller_open_preview_game(args.open_game_index);
    }

    uint32_t last_tick = SDL_GetTicks();
    if (args.capture_path != NULL)
    {
        for (int i = 0; i < 240; ++i)
        {
            preview_tick_once(&last_tick);
        }

        bool captured = preview_capture_png(display, args.capture_path);
        SDL_DelEventWatch(preview_sdl_event_watch, NULL);
        lv_sdl_quit();
        if (!captured)
        {
            fprintf(stderr, "Failed to capture preview PNG: %s\n", args.capture_path);
            return 1;
        }
        return 0;
    }

    bool running = true;
    while (running)
    {
        preview_tick_once(&last_tick);

        /*
         * LVGL SDL 驱动内部已经用 SDL_PollEvent 分发鼠标、滚轮和键盘事件。
         * host 侧只旁路观察退出事件，避免提前消费点击/滑动导致页面交互丢失。
         */
        if (SDL_AtomicGet(&s_preview_quit_requested) != 0)
        {
            running = false;
        }
    }

    SDL_DelEventWatch(preview_sdl_event_watch, NULL);
    lv_sdl_quit();
    return 0;
}
