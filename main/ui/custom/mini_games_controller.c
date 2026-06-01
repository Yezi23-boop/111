#include "mini_games_controller.h"

#include <stdio.h>

#include "app/board_button.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "features/mini_games/mini_game_2048.h"
#include "lvgl.h"

static const char *TAG = "mini_games";

static const lv_coord_t kScreenWidth = 410;
static const lv_coord_t kScreenHeight = 502;
static const lv_coord_t kBoardX = 25;
static const lv_coord_t kBoardY = 126;
static const lv_coord_t kBoardSize = 360;
static const lv_coord_t kTileSize = 82;
static const lv_coord_t kTileGap = 8;
static const lv_coord_t kGestureThresholdPx = 28;

static lv_ui *s_ui;
static lv_obj_t *s_screen;
static lv_obj_t *s_score_label;
static lv_obj_t *s_state_label;
static lv_obj_t *s_board;
static lv_obj_t *s_tile_labels[MINI_GAME_2048_SIZE][MINI_GAME_2048_SIZE];
static mini_game_2048_t s_game;
static bool s_game_initialized;
static bool s_paused;
static bool s_pressing;
static lv_point_t s_press_start;

static void mini_games_controller_refresh_board(void);
static void mini_games_controller_leave(void);

static uint32_t mini_games_controller_seed(void)
{
    const int64_t now_us = esp_timer_get_time();
    return (uint32_t)now_us ^ (uint32_t)(now_us >> 32);
}

static bool mini_games_controller_is_screen_alive(void)
{
    return s_screen != NULL && lv_obj_is_valid(s_screen);
}

static bool mini_games_controller_is_foreground(void)
{
    return mini_games_controller_is_screen_alive() &&
           lv_screen_active() == s_screen;
}

static void mini_games_controller_reset_refs(void)
{
    s_screen = NULL;
    s_score_label = NULL;
    s_state_label = NULL;
    s_board = NULL;
    for (uint8_t row = 0; row < MINI_GAME_2048_SIZE; ++row) {
        for (uint8_t col = 0; col < MINI_GAME_2048_SIZE; ++col) {
            s_tile_labels[row][col] = NULL;
        }
    }
    s_pressing = false;
}

static lv_color_t mini_games_controller_tile_color(uint16_t value)
{
    switch (value) {
        case 0:
            return lv_color_hex(0x223047);
        case 2:
            return lv_color_hex(0xeee4da);
        case 4:
            return lv_color_hex(0xede0c8);
        case 8:
            return lv_color_hex(0xf2b179);
        case 16:
            return lv_color_hex(0xf59563);
        case 32:
            return lv_color_hex(0xf67c5f);
        case 64:
            return lv_color_hex(0xf65e3b);
        case 128:
            return lv_color_hex(0xedcf72);
        case 256:
            return lv_color_hex(0xedcc61);
        case 512:
            return lv_color_hex(0xedc850);
        case 1024:
            return lv_color_hex(0xedc53f);
        default:
            return lv_color_hex(0x3c8d7d);
    }
}

static lv_color_t mini_games_controller_tile_text_color(uint16_t value)
{
    return value <= 4U ? lv_color_hex(0x293241) : lv_color_hex(0xffffff);
}

static void mini_games_controller_set_paused(bool paused)
{
    s_paused = paused;
    mini_games_controller_refresh_board();
}

static void mini_games_controller_new_game(void)
{
    mini_game_2048_reset(&s_game, mini_games_controller_seed());
    s_game_initialized = true;
    s_paused = false;
    mini_games_controller_refresh_board();
}

static void mini_games_controller_screen_delete_event_cb(lv_event_t *e)
{
    (void)e;
    mini_games_controller_reset_refs();
}

static void mini_games_controller_back_event_cb(lv_event_t *e)
{
    (void)e;
    mini_games_controller_leave();
}

static void mini_games_controller_new_event_cb(lv_event_t *e)
{
    (void)e;
    mini_games_controller_new_game();
}

static void mini_games_controller_pause_event_cb(lv_event_t *e)
{
    (void)e;
    mini_games_controller_set_paused(!s_paused);
}

static bool mini_games_controller_get_swipe_direction(
    const lv_point_t *start, const lv_point_t *end,
    mini_game_2048_direction_t *direction)
{
    if (start == NULL || end == NULL || direction == NULL) {
        return false;
    }

    const lv_coord_t dx = (lv_coord_t)(end->x - start->x);
    const lv_coord_t dy = (lv_coord_t)(end->y - start->y);
    const lv_coord_t abs_dx = dx >= 0 ? dx : (lv_coord_t)-dx;
    const lv_coord_t abs_dy = dy >= 0 ? dy : (lv_coord_t)-dy;

    if (abs_dx < kGestureThresholdPx && abs_dy < kGestureThresholdPx) {
        return false;
    }

    if (abs_dx >= abs_dy) {
        *direction = dx > 0 ? MINI_GAME_2048_DIRECTION_RIGHT
                            : MINI_GAME_2048_DIRECTION_LEFT;
    } else {
        *direction = dy > 0 ? MINI_GAME_2048_DIRECTION_DOWN
                            : MINI_GAME_2048_DIRECTION_UP;
    }
    return true;
}

static void mini_games_controller_board_event_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);

    if (!mini_games_controller_is_foreground()) {
        return;
    }

    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(lv_indev_get_act(), &s_press_start);
        s_pressing = true;
        return;
    }

    if (code != LV_EVENT_RELEASED || !s_pressing) {
        return;
    }

    s_pressing = false;
    if (s_paused || mini_game_2048_is_game_over(&s_game)) {
        return;
    }

    lv_point_t end = {0};
    mini_game_2048_direction_t direction = MINI_GAME_2048_DIRECTION_LEFT;
    lv_indev_get_point(lv_indev_get_act(), &end);
    if (!mini_games_controller_get_swipe_direction(&s_press_start, &end,
                                                   &direction)) {
        return;
    }

    const mini_game_2048_move_result_t result =
        mini_game_2048_move(&s_game, direction);
    if (result.moved || result.game_over) {
        mini_games_controller_refresh_board();
    }
}

static lv_obj_t *mini_games_controller_create_button(lv_obj_t *parent,
                                                     const char *text,
                                                     lv_coord_t x)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, 78, 42);
    lv_obj_set_pos(button, x, 24);
    lv_obj_set_style_radius(button, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x2563eb),
                              LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserratMedium_12,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(label);
    return button;
}

static void mini_games_controller_create_tiles(void)
{
    for (uint8_t row = 0; row < MINI_GAME_2048_SIZE; ++row) {
        for (uint8_t col = 0; col < MINI_GAME_2048_SIZE; ++col) {
            lv_obj_t *tile = lv_obj_create(s_board);
            lv_obj_set_size(tile, kTileSize, kTileSize);
            lv_obj_set_pos(tile, (lv_coord_t)(col * (kTileSize + kTileGap)),
                           (lv_coord_t)(row * (kTileSize + kTileGap)));
            lv_obj_set_style_radius(tile, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(tile, 0,
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);

            lv_obj_t *label = lv_label_create(tile);
            lv_obj_set_style_text_font(label, &lv_font_montserratMedium_27,
                                       LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER,
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_center(label);
            s_tile_labels[row][col] = label;
        }
    }
}

static void mini_games_controller_ensure_screen_created(void)
{
    if (mini_games_controller_is_screen_alive()) {
        return;
    }

    mini_games_controller_reset_refs();

    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, kScreenWidth, kScreenHeight);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x111827),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_screen, mini_games_controller_screen_delete_event_cb,
                        LV_EVENT_DELETE, NULL);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, "2048");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title, &lv_font_montserratMedium_46,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(title, 24, 18);

    lv_obj_t *back_btn =
        mini_games_controller_create_button(s_screen, "Back", 222);
    lv_obj_add_event_cb(back_btn, mini_games_controller_back_event_cb,
                        LV_EVENT_CLICKED, NULL);

    lv_obj_t *new_btn =
        mini_games_controller_create_button(s_screen, "New", 308);
    lv_obj_add_event_cb(new_btn, mini_games_controller_new_event_cb,
                        LV_EVENT_CLICKED, NULL);

    lv_obj_t *pause_btn =
        mini_games_controller_create_button(s_screen, "Pause", 308);
    lv_obj_set_pos(pause_btn, 308, 74);
    lv_obj_add_event_cb(pause_btn, mini_games_controller_pause_event_cb,
                        LV_EVENT_CLICKED, NULL);

    s_score_label = lv_label_create(s_screen);
    lv_obj_set_pos(s_score_label, 26, 82);
    lv_obj_set_style_text_color(s_score_label, lv_color_hex(0xd1d5db),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_score_label, &lv_font_montserratMedium_16,
                               LV_PART_MAIN | LV_STATE_DEFAULT);

    s_state_label = lv_label_create(s_screen);
    lv_obj_set_size(s_state_label, 250, 24);
    lv_obj_set_pos(s_state_label, 26, 104);
    lv_label_set_long_mode(s_state_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_state_label, lv_color_hex(0xfbbf24),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(s_state_label, LV_TEXT_ALIGN_LEFT,
                                LV_PART_MAIN | LV_STATE_DEFAULT);

    s_board = lv_obj_create(s_screen);
    lv_obj_set_size(s_board, kBoardSize, kBoardSize);
    lv_obj_set_pos(s_board, kBoardX, kBoardY);
    lv_obj_set_style_bg_color(s_board, lv_color_hex(0x334155),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_board, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(s_board, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(s_board, kTileGap,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_board, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(s_board, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_board, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_board, mini_games_controller_board_event_cb,
                        LV_EVENT_ALL, NULL);

    mini_games_controller_create_tiles();
    ESP_LOGI(TAG, "2048 screen created");
}

static void mini_games_controller_refresh_board(void)
{
    if (!mini_games_controller_is_screen_alive()) {
        return;
    }

    if (s_score_label != NULL && lv_obj_is_valid(s_score_label)) {
        lv_label_set_text_fmt(s_score_label, "Score: %lu",
                              (unsigned long)mini_game_2048_get_score(&s_game));
    }

    if (s_state_label != NULL && lv_obj_is_valid(s_state_label)) {
        if (mini_game_2048_is_game_over(&s_game)) {
            lv_label_set_text(s_state_label, "Game Over");
        } else if (s_paused) {
            lv_label_set_text(s_state_label, "Paused");
        } else {
            lv_label_set_text(s_state_label, "Swipe to move");
        }
    }

    for (uint8_t row = 0; row < MINI_GAME_2048_SIZE; ++row) {
        for (uint8_t col = 0; col < MINI_GAME_2048_SIZE; ++col) {
            lv_obj_t *label = s_tile_labels[row][col];
            if (label == NULL || !lv_obj_is_valid(label)) {
                continue;
            }

            lv_obj_t *tile = lv_obj_get_parent(label);
            const uint16_t value = mini_game_2048_get_cell(&s_game, row, col);
            if (value == 0U) {
                lv_label_set_text(label, "");
            } else {
                lv_label_set_text_fmt(label, "%u", (unsigned int)value);
            }

            lv_obj_set_style_bg_color(
                tile, mini_games_controller_tile_color(value),
                LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(
                label, mini_games_controller_tile_text_color(value),
                LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_center(label);
        }
    }
}

static void mini_games_controller_leave(void)
{
    if (s_ui == NULL || s_ui->screen_main == NULL ||
        !mini_games_controller_is_screen_alive()) {
        return;
    }

    s_paused = false;
    lv_screen_load_anim(s_ui->screen_main, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0,
                        true);
}

void mini_games_controller_init(lv_ui *ui)
{
    s_ui = ui;
}

void mini_games_controller_open(void)
{
    board_button_clear_events();
    mini_games_controller_ensure_screen_created();
    if (!mini_games_controller_is_screen_alive()) {
        return;
    }

    if (!s_game_initialized) {
        mini_game_2048_init(&s_game, mini_games_controller_seed());
        s_game_initialized = true;
    }

    mini_games_controller_refresh_board();
    lv_screen_load_anim(s_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

void mini_games_controller_poll_ui(void)
{
    if (!mini_games_controller_is_foreground()) {
        board_button_clear_events();
        return;
    }

    for (;;) {
        const board_button_event_t event = board_button_consume_event();
        if (event == BOARD_BUTTON_EVENT_NONE) {
            break;
        }
        if (event == BOARD_BUTTON_EVENT_LONG_PRESS) {
            mini_games_controller_leave();
            break;
        }
        if (event == BOARD_BUTTON_EVENT_SINGLE_CLICK) {
            mini_games_controller_set_paused(!s_paused);
        }
    }
}
