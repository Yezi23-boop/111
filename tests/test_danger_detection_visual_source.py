import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]


class DangerDetectionVisualSourceTests(unittest.TestCase):
    def test_view_and_controller_use_fullscreen_color_states(self) -> None:
        view_header = (
            REPO_ROOT / "main" / "ui" / "custom" / "danger_detection_view.h"
        ).read_text(encoding="utf-8")
        view_source = (
            REPO_ROOT / "main" / "ui" / "custom" / "danger_detection_view.c"
        ).read_text(encoding="utf-8")
        controller_source = (
            REPO_ROOT / "main" / "ui" / "custom" / "danger_detection_controller.c"
        ).read_text(encoding="utf-8")

        self.assertIn("danger_detection_view_model_t", view_header)
        self.assertIn("danger_detection_view_apply_model", view_header)
        self.assertIn("content_layer", view_source)
        self.assertIn("alert_layer", view_source)
        self.assertIn("lv_obj_set_size(view->back_btn, 116, 52)", view_source)
        self.assertIn("lv_obj_align(view->back_btn, LV_ALIGN_TOP_LEFT, 10, 4)", view_source)
        self.assertIn("if (model->alert_visible)", view_source)
        self.assertIn("lv_color_hex(0xffffff)", view_source)
        self.assertIn("lv_palette_main(LV_PALETTE_RED)", view_source)
        self.assertIn("danger_detection_view_apply_model", controller_source)
        self.assertNotIn("正在监听", controller_source)
        self.assertNotIn("检测到喇叭", controller_source)
        self.assertNotIn("检测到警笛", controller_source)


if __name__ == "__main__":
    unittest.main()
