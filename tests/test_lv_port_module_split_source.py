import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
LVGL_PORT_DIR = REPO_ROOT / "components" / "lvgl_port"


class LvPortModuleSplitSourceTests(unittest.TestCase):
    def test_lvgl_port_is_split_into_display_input_and_tick_modules(self) -> None:
        cmake_source = (LVGL_PORT_DIR / "CMakeLists.txt").read_text(encoding="utf-8")
        root_source = (LVGL_PORT_DIR / "lv_port.c").read_text(encoding="utf-8")
        display_source = (LVGL_PORT_DIR / "lv_port_display.c").read_text(encoding="utf-8")
        input_source = (LVGL_PORT_DIR / "lv_port_input.c").read_text(encoding="utf-8")
        tick_source = (LVGL_PORT_DIR / "lv_port_tick.c").read_text(encoding="utf-8")

        self.assertIn('"lv_port_display.c"', cmake_source)
        self.assertIn('"lv_port_input.c"', cmake_source)
        self.assertIn('"lv_port_tick.c"', cmake_source)

        self.assertIn("void lv_port_init_small(void)", root_source)
        self.assertNotIn("void lv_port_disp_flush(", root_source)
        self.assertNotIn("void lv_port_indev_init(", root_source)
        self.assertNotIn("void lv_port_tick_init(", root_source)

        self.assertIn("void lv_port_disp_flush(", display_source)
        self.assertIn("void lv_port_panel_init(", display_source)
        self.assertNotIn("lv_port_flush_area_chunked_simple", display_source)

        self.assertIn("void lv_port_indev_init(", input_source)
        self.assertIn("void lv_port_touch_init(", input_source)

        self.assertIn("void lv_port_tick_init(", tick_source)


if __name__ == "__main__":
    unittest.main()
