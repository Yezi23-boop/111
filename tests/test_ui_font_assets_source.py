import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
AI_UI_SOURCE = REPO_ROOT / "main" / "ui" / "custom" / "ai_ui_controller.c"
FONT_ASSETS_HEADER = REPO_ROOT / "main" / "ui" / "custom" / "ui_font_assets.h"
FONT_ASSETS_SOURCE = REPO_ROOT / "main" / "ui" / "custom" / "ui_font_assets.c"
CUSTOM_HEADER = REPO_ROOT / "main" / "ui" / "custom" / "custom.h"
MAIN_CMAKELISTS = REPO_ROOT / "main" / "CMakeLists.txt"


class UiFontAssetsSourceTests(unittest.TestCase):
    def test_ai_ui_controller_uses_font_assets_seam(self) -> None:
        source = AI_UI_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "ui_font_assets.h"', source)
        self.assertIn("ui_font_assets_title()", source)
        self.assertIn("ui_font_assets_body()", source)
        self.assertIn("ui_font_assets_meta()", source)
        self.assertIn(
            "lv_obj_set_style_text_font(s_title_label, ui_font_assets_title()",
            source,
        )
        self.assertIn(
            "lv_obj_set_style_text_font(s_state_label, ui_font_assets_title()",
            source,
        )
        self.assertIn(
            "lv_obj_set_style_text_font(s_hint_label, ui_font_assets_body()",
            source,
        )
        self.assertIn(
            "lv_obj_set_style_text_font(s_ip_label, ui_font_assets_meta()",
            source,
        )
        self.assertNotIn("&lv_font_SourceHanSerifSC_Regular_22", source)
        self.assertNotIn("&lv_font_montserratMedium_16", source)
        self.assertNotIn("&lv_font_montserratMedium_27", source)

    def test_custom_header_exposes_font_assets_layer(self) -> None:
        source = CUSTOM_HEADER.read_text(encoding="utf-8")

        self.assertIn('#include "ui_font_assets.h"', source)

    def test_font_assets_header_declares_minimal_api(self) -> None:
        source = FONT_ASSETS_HEADER.read_text(encoding="utf-8")

        self.assertIn("esp_err_t ui_font_assets_init(void);", source)
        self.assertIn("bool ui_font_assets_ready(void);", source)
        self.assertIn("const lv_font_t *ui_font_assets_title(void);", source)
        self.assertIn("const lv_font_t *ui_font_assets_body(void);", source)
        self.assertIn("const lv_font_t *ui_font_assets_meta(void);", source)

    def test_font_assets_source_probes_assets_partition_and_falls_back(self) -> None:
        source = FONT_ASSETS_SOURCE.read_text(encoding="utf-8")

        self.assertIn("esp_partition_find_first", source)
        self.assertIn("ESP_PARTITION_TYPE_DATA", source)
        self.assertIn("ESP_PARTITION_SUBTYPE_ANY", source)
        self.assertIn('"assets"', source)
        self.assertIn("lv_font_SourceHanSerifSC_Regular_22", source)
        self.assertIn("lv_font_montserratMedium_16", source)
        self.assertIn("return ESP_ERR_NOT_SUPPORTED;", source)
        self.assertIn("return false;", source)

    def test_main_cmakelists_mentions_font_assets_source(self) -> None:
        source = MAIN_CMAKELISTS.read_text(encoding="utf-8")

        self.assertIn("ui_font_assets.c", source)


if __name__ == "__main__":
    unittest.main()
