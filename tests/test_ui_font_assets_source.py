import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
AI_UI_SOURCE = REPO_ROOT / "main" / "ui" / "custom" / "ai_ui_controller.c"


class UiFontAssetsSourceTests(unittest.TestCase):
    def test_ai_ui_controller_uses_font_assets_seam(self) -> None:
        source = AI_UI_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "ui_font_assets.h"', source)
        self.assertIn("ui_font_assets_title()", source)
        self.assertIn("ui_font_assets_body()", source)
        self.assertIn("ui_font_assets_meta()", source)
        self.assertNotIn("&lv_font_SourceHanSerifSC_Regular_22", source)
        self.assertNotIn("&lv_font_montserratMedium_16", source)
        self.assertNotIn("&lv_font_montserratMedium_27", source)


if __name__ == "__main__":
    unittest.main()
