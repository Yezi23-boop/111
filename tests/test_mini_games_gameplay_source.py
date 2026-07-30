import unittest

from tests.main_paths import MAIN_CMAKE
from tests.main_paths import MINI_GAMES_DIR
from tests.main_paths import UI_MINI_GAMES_CONTROLLER_SOURCE


class MiniGamesGameplaySourceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.game_2048 = (MINI_GAMES_DIR / "mini_game_2048.c").read_text(
            encoding="utf-8"
        )
        self.flappy = (MINI_GAMES_DIR / "mini_game_flappy.c").read_text(
            encoding="utf-8"
        )
        self.dino = (MINI_GAMES_DIR / "mini_game_dino.c").read_text(
            encoding="utf-8"
        )
        self.controller = UI_MINI_GAMES_CONTROLLER_SOURCE.read_text(
            encoding="utf-8"
        )

    def test_2048_has_a_single_win_pause_then_can_continue(self) -> None:
        self.assertIn("mini_game_2048_has_winning_tile", self.game_2048)
        self.assertIn("result.just_won", self.game_2048)
        self.assertIn("mini_game_2048_continue", self.game_2048)
        self.assertIn("mini_game_2048_is_waiting_for_continue", self.controller)
        self.assertIn("继续挑战", self.controller)

    def test_flappy_waits_for_first_tap(self) -> None:
        self.assertIn("game->started = false", self.flappy)
        self.assertIn("game->started = true", self.flappy)
        self.assertIn("game->game_over || !game->started", self.flappy)
        self.assertIn("mini_game_flappy_is_started", self.controller)
        self.assertIn("轻触屏幕开始飞翔", self.controller)

    def test_dino_uses_hold_to_duck_and_distance_scoring(self) -> None:
        self.assertIn("mini_game_dino_set_ducking", self.dino)
        self.assertNotIn("duck_timer", self.dino)
        self.assertIn("kScoreFramesPerPoint", self.dino)
        self.assertIn("s_game_dino.score / 250", self.controller)
        self.assertIn("LV_EVENT_PRESS_LOST", self.controller)
        self.assertIn(
            "mini_games_controller_dino_click_cb,\n                        LV_EVENT_PRESSED",
            self.controller,
        )
        self.assertNotIn(
            "mini_games_controller_dino_click_cb, LV_EVENT_ALL",
            self.controller,
        )

    def test_high_score_io_stays_out_of_the_lvgl_controller(self) -> None:
        progress_source = (MINI_GAMES_DIR / "mini_games_progress.c").read_text(
            encoding="utf-8"
        )
        cmake = MAIN_CMAKE.read_text(encoding="utf-8")

        self.assertIn("mini_games_progress_worker", progress_source)
        self.assertIn("xQueueOverwrite", progress_source)
        self.assertIn("nvs_commit", progress_source)
        self.assertIn("mini_games_progress.c", cmake)
        self.assertNotIn("nvs_open(", self.controller)

    def test_runtime_pause_reads_existing_ui_snapshots_only(self) -> None:
        self.assertIn("ui_refresh_policy_get_activity_snapshot", self.controller)
        self.assertIn("watch_nc_is_visible", self.controller)
        self.assertIn("s_auto_paused", self.controller)


if __name__ == "__main__":
    unittest.main()
