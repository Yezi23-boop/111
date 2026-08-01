import unittest

from tests.main_cmake_contract import assert_main_source_globbed
from tests.main_paths import BOARD_BUTTON_HEADER
from tests.main_paths import BOARD_BUTTON_SOURCE
from tests.main_paths import HARDWARE_INIT_SOURCE
from tests.main_paths import LVGL_TASK_SOURCE
from tests.main_paths import MAIN_CMAKE
from tests.main_paths import MINI_GAME_2048_SOURCE
from tests.main_paths import UI_EVENTS_INIT_SOURCE
from tests.main_paths import UI_MINI_GAMES_CONTROLLER_HEADER
from tests.main_paths import UI_MINI_GAMES_CONTROLLER_SOURCE


class MiniGamesUiSourceTests(unittest.TestCase):
    def test_game_entry_is_bound_to_generated_main_screen(self) -> None:
        source = UI_EVENTS_INIT_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "mini_games_controller.h"', source)
        self.assertIn("screen_main_option_4_event_handler", source)
        self.assertIn("mini_games_controller_open();", source)
        self.assertIn("ui->screen_main_option_4", source)
        self.assertIn("ui->screen_main_Game", source)
        self.assertIn("LV_OBJ_FLAG_CLICKABLE", source)

    def test_lvgl_task_initializes_and_polls_controller(self) -> None:
        source = LVGL_TASK_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "ui/custom/mini_games_controller.h"', source)
        self.assertIn("mini_games_controller_init(&guider_ui)", source)
        self.assertIn("mini_games_controller_poll_ui();", source)

    def test_controller_uses_touch_for_moves_and_button_for_pause_exit(self) -> None:
        source = UI_MINI_GAMES_CONTROLLER_SOURCE.read_text(encoding="utf-8")
        header = UI_MINI_GAMES_CONTROLLER_HEADER.read_text(encoding="utf-8")

        self.assertIn("LV_EVENT_PRESSED", source)
        self.assertIn("LV_EVENT_RELEASED", source)
        self.assertIn("mini_game_2048_move(&s_game, direction)", source)
        self.assertIn("BOARD_BUTTON_EVENT_SINGLE_CLICK", source)
        self.assertIn("mini_games_controller_set_paused(!s_paused)", source)
        self.assertIn("BOARD_BUTTON_EVENT_LONG_PRESS", source)
        self.assertIn("mini_games_controller_leave();", source)
        self.assertNotIn("mini_games_controller_pulse(s_board)", source)
        self.assertIn("mini_games_controller_poll_ui", header)
        self.assertIn("board_button_clear_events();", source)

    def test_game_layout_uses_full_stage_and_safe_overlay_controls(self) -> None:
        source = UI_MINI_GAMES_CONTROLLER_SOURCE.read_text(encoding="utf-8")
        flappy_header = (MINI_GAME_2048_SOURCE.parent / "mini_game_flappy.h").read_text(
            encoding="utf-8"
        )
        dino_header = (MINI_GAME_2048_SOURCE.parent / "mini_game_dino.h").read_text(
            encoding="utf-8"
        )

        self.assertIn("kBoardY = 96", source)
        self.assertIn("kControlsY = 434", source)
        self.assertIn("kControlsHeight = 44", source)
        self.assertIn("mini_games_controller_set_controls_running", source)
        self.assertIn("LV_OPA_50", source)
        self.assertIn("lv_obj_set_size(stage, kScreenWidth, kScreenHeight)", source)
        self.assertNotIn('lv_label_set_text(title_lbl, "飞翔的小鸟")', source)
        self.assertNotIn('lv_label_set_text(title_lbl, "小恐龙跑酷")', source)
        self.assertIn("#define FLAPPY_PLAY_AREA_H 420", flappy_header)
        self.assertIn("#define FLAPPY_PLAY_AREA_W 410", flappy_header)
        self.assertIn("kFlappyPlayfieldY = 0", source)
        self.assertIn("s_game_score_panels", source)
        self.assertIn("#define DINO_PLAY_AREA_H 420", dino_header)
        self.assertIn("#define DINO_PLAY_AREA_W 410", dino_header)

    def test_board_button_bridge_keeps_callbacks_out_of_lvgl(self) -> None:
        source = BOARD_BUTTON_SOURCE.read_text(encoding="utf-8")
        header = BOARD_BUTTON_HEADER.read_text(encoding="utf-8")

        self.assertIn("BOARD_BUTTON_EVENT_SINGLE_CLICK", header)
        self.assertIn("BOARD_BUTTON_EVENT_LONG_PRESS", header)
        self.assertIn("board_button_consume_event", source)
        self.assertIn("board_button_clear_events", source)
        self.assertIn("xQueueCreate", source)
        self.assertIn("xQueueSendToBack", source)
        self.assertIn("xQueueReceive", source)
        self.assertIn("vQueueDelete", source)
        self.assertIn("BUTTON_SINGLE_CLICK", source)
        self.assertIn("BUTTON_LONG_PRESS_START", source)
        self.assertIn("ui_refresh_policy_notify_activity()", source)
        self.assertNotIn("volatile", source)
        self.assertNotIn("s_pending_events", source)
        self.assertNotIn("lv_obj_", source)
        self.assertNotIn("lv_screen_load", source)

    def test_hardware_init_only_calls_board_button_owner(self) -> None:
        source = HARDWARE_INIT_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "board_button.h"', source)
        self.assertIn("board_button_init()", source)
        self.assertNotIn("BUTTON_SINGLE_CLICK", source)
        self.assertNotIn("iot_button_register_cb", source)

    def test_cmake_registers_mini_game_sources(self) -> None:
        assert_main_source_globbed(self, "features/mini_games/mini_game_2048.c")
        assert_main_source_globbed(self, "app/board_button.c")
        assert_main_source_globbed(self, "ui/custom/mini_games_controller.c")


if __name__ == "__main__":
    unittest.main()
