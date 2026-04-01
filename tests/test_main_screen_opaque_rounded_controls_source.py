import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN_SCREEN = REPO_ROOT / "main" / "ui" / "generated" / "setup_scr_screen_main.c"


class MainScreenOpaqueRoundedControlsSourceTests(unittest.TestCase):
    def test_problematic_rounded_controls_restore_original_background_opacity(self) -> None:
        source = MAIN_SCREEN.read_text(encoding="utf-8")

        self.assertIn(
            "lv_obj_set_style_bg_opa(ui->screen_main_digital_clock_1, 128, LV_PART_MAIN|LV_STATE_DEFAULT);",
            source,
        )
        self.assertIn(
            "lv_obj_set_style_bg_opa(ui->screen_main_Dropdown_menu, 102, LV_PART_MAIN|LV_STATE_DEFAULT);",
            source,
        )
        self.assertIn(
            "lv_obj_set_style_bg_opa(ui->screen_main_Brightness, 60, LV_PART_MAIN|LV_STATE_DEFAULT);",
            source,
        )
        self.assertIn(
            "lv_obj_set_style_bg_opa(ui->screen_main_Brightness, 230, LV_PART_INDICATOR|LV_STATE_DEFAULT);",
            source,
        )
        self.assertIn(
            "lv_obj_set_style_bg_opa(ui->screen_main_loudness, 60, LV_PART_MAIN|LV_STATE_DEFAULT);",
            source,
        )
        self.assertIn(
            "lv_obj_set_style_bg_opa(ui->screen_main_loudness, 230, LV_PART_INDICATOR|LV_STATE_DEFAULT);",
            source,
        )
        self.assertIn(
            "lv_obj_set_style_bg_opa(ui->screen_main_slider_1, 34, LV_PART_MAIN|LV_STATE_DEFAULT);",
            source,
        )
        self.assertIn(
            "lv_obj_set_style_bg_opa(ui->screen_main_slider_1, 238, LV_PART_INDICATOR|LV_STATE_DEFAULT);",
            source,
        )


if __name__ == "__main__":
    unittest.main()
