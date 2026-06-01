import unittest

from tests.main_paths import MINI_GAME_2048_HEADER
from tests.main_paths import MINI_GAME_2048_SOURCE


class MiniGame2048SourceTests(unittest.TestCase):
    def test_rule_module_exists_and_stays_ui_independent(self) -> None:
        self.assertTrue(MINI_GAME_2048_HEADER.exists())
        self.assertTrue(MINI_GAME_2048_SOURCE.exists())

        combined = (
            MINI_GAME_2048_HEADER.read_text(encoding="utf-8")
            + "\n"
            + MINI_GAME_2048_SOURCE.read_text(encoding="utf-8")
        )

        self.assertIn("mini_game_2048_move(", combined)
        self.assertIn("mini_game_2048_load_board(", combined)
        self.assertNotIn("lvgl.h", combined)
        self.assertNotIn("lv_obj_t", combined)
        self.assertNotIn("esp_wifi", combined)
        self.assertNotIn("iot_button", combined)
        self.assertNotIn("nvs_flash", combined)

    def test_move_contract_keeps_2048_core_rules_visible(self) -> None:
        source = MINI_GAME_2048_SOURCE.read_text(encoding="utf-8")

        self.assertIn("mini_game_2048_merge_line", source)
        self.assertIn("++i;", source, "merged tile must skip its pair once")
        self.assertIn("result.moved", source)
        self.assertIn("mini_game_2048_add_random_tile(game)", source)
        self.assertIn("if (result.moved)", source)
        self.assertIn("mini_game_2048_calculate_game_over(game)", source)


if __name__ == "__main__":
    unittest.main()
