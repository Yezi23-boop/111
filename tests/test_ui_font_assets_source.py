import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
AI_UI_SOURCE = REPO_ROOT / "main" / "ui" / "custom" / "ai_ui_controller.c"
AI_CHAT_VIEW_SOURCE = REPO_ROOT / "main" / "ui" / "custom" / "ai_chat_view.c"
FONT_ASSETS_HEADER = REPO_ROOT / "main" / "ui" / "custom" / "ui_font_assets.h"
FONT_ASSETS_SOURCE = REPO_ROOT / "main" / "ui" / "custom" / "ui_font_assets.c"
CBIN_FONT_BRIDGE_SOURCE = REPO_ROOT / "main" / "ui" / "custom" / "cbin_font_bridge.c"
CUSTOM_HEADER = REPO_ROOT / "main" / "ui" / "custom" / "custom.h"
MAIN_CMAKELISTS = REPO_ROOT / "main" / "CMakeLists.txt"
MAIN_MANIFEST = REPO_ROOT / "main" / "idf_component.yml"
ROOT_CMAKELISTS = REPO_ROOT / "CMakeLists.txt"
ASSET_INDEX = REPO_ROOT / "assets" / "ai-fonts" / "index.json"
ASSET_TEXT_FONT = REPO_ROOT / "assets" / "ai-fonts" / "font_puhui_common_20_4.bin"
ASSET_PACKER = REPO_ROOT / "scripts" / "build_ai_font_assets.py"


class UiFontAssetsSourceTests(unittest.TestCase):
    def test_shared_ai_chat_view_initializes_font_assets_early(self) -> None:
        source = AI_CHAT_VIEW_SOURCE.read_text(encoding="utf-8")

        self.assertIn("ui_font_assets_init()", source)
        self.assertIn('ESP_LOGW(TAG, "ui_font_assets_init failed', source)
        self.assertLess(
            source.index("ui_font_assets_init()"),
            source.index("view->screen = lv_obj_create(NULL);"),
        )

    def test_shared_ai_chat_view_uses_font_assets_seam(self) -> None:
        source = AI_CHAT_VIEW_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "ui_font_assets.h"', source)
        self.assertIn("ui_font_assets_body()", source)
        self.assertIn(
            "lv_obj_set_style_text_font(text_label, ui_font_assets_body()",
            source,
        )
        self.assertIn(
            "lv_obj_set_style_text_font(view->voice_label, ui_font_assets_body()",
            source,
        )
        self.assertIn(
            "lv_obj_set_style_text_font(view->secondary_label, ui_font_assets_body()",
            source,
        )
        self.assertNotIn("&lv_font_SourceHanSerifSC_Regular_22", source)
        self.assertNotIn("&lv_font_montserratMedium_16", source)
        self.assertNotIn("&lv_font_montserratMedium_27", source)

    def test_font_assets_source_maps_title_body_meta_fonts(self) -> None:
        source = FONT_ASSETS_SOURCE.read_text(encoding="utf-8")

        self.assertIn("const lv_font_t *ui_font_assets_title(void)", source)
        self.assertIn("const lv_font_t *ui_font_assets_body(void)", source)
        self.assertIn("const lv_font_t *ui_font_assets_meta(void)", source)
        self.assertIn("const lv_font_t *ui_font_assets_icon(void)", source)
        self.assertIn("s_runtime.title_font = cbin_font_bridge_create", source)
        self.assertIn("s_runtime.body_font = s_runtime.title_font;", source)
        self.assertIn("s_runtime.meta_font = s_runtime.title_font;", source)
        self.assertIn("UI_FONT_ASSETS_BUILTIN_TEXT_NAME", source)
        self.assertIn("s_builtin_text_font_start", source)
        self.assertIn("ui_font_assets_create_builtin_text", source)
        self.assertIn("ui_font_assets_builtin_text", source)
        self.assertIn("s_runtime.builtin_text_font", source)
        self.assertIn("return &lv_font_montserratMedium_16;", source)
        self.assertIn("return s_runtime.ready && s_runtime.meta_font != NULL", source)
        self.assertIn("return ESP_OK;", source)

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
        self.assertIn("const lv_font_t *ui_font_assets_icon(void);", source)

    def test_font_assets_source_uses_partition_mmap_and_index_json(self) -> None:
        source = FONT_ASSETS_SOURCE.read_text(encoding="utf-8")

        self.assertIn("esp_partition_find_first", source)
        self.assertIn("esp_partition_mmap", source)
        self.assertIn("ESP_PARTITION_TYPE_DATA", source)
        self.assertIn("ESP_PARTITION_SUBTYPE_ANY", source)
        self.assertIn('"assets"', source)
        self.assertIn("index.json", source)
        self.assertIn("text_font", source)
        self.assertIn("icon_font", source)
        self.assertIn("ui_font_assets_compute_checksum", source)
        self.assertIn("ESP_ERR_INVALID_CRC", source)
        self.assertIn("index.json has no icon_font, use compiled icon fallback", source)
        self.assertNotIn("index.json missing icon_font, keep compiled fallback", source)

    def test_font_assets_source_no_longer_short_circuits_lvgl_93(self) -> None:
        source = FONT_ASSETS_SOURCE.read_text(encoding="utf-8")

        self.assertNotIn("runtime cbin fonts require LVGL >= 9.3.0", source)
        self.assertNotIn("ESP_ERR_NOT_SUPPORTED", source)
        self.assertIn("cbin_font_create", source)
        self.assertIn("ESP_ERR_INVALID_CRC", source)
        self.assertIn("ui_font_assets_icon(void)", source)

    def test_main_cmakelists_mentions_font_assets_source(self) -> None:
        source = MAIN_CMAKELISTS.read_text(encoding="utf-8")

        self.assertIn("ui_font_assets.c", source)
        self.assertIn("cbin_font_bridge.c", source)
        self.assertIn("78__xiaozhi-fonts", source)
        self.assertIn("BUILTIN_TEXT_FONT font_puhui_common_20_4", source)
        self.assertIn("EMBED_FILES ${BUILTIN_TEXT_FONT_FILE}", source)
        self.assertIn("UI_FONT_ASSETS_BUILTIN_TEXT_START_SYMBOL", source)
        self.assertIn("font_noto_qwen_20_4", source)

    def test_manifest_uses_managed_xiaozhi_fonts_dependency(self) -> None:
        source = MAIN_MANIFEST.read_text(encoding="utf-8")

        self.assertIn("78/xiaozhi-fonts: ^1.6.0", source)

    def test_cbin_font_bridge_source_uses_cbin_font_dependency(self) -> None:
        source = CBIN_FONT_BRIDGE_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "cbin_font.h"', source)
        self.assertIn("cbin_font_create", source)
        self.assertIn("cbin_font_delete", source)

    def test_ai_font_asset_index_exists_and_declares_text_font(self) -> None:
        text = ASSET_INDEX.read_text(encoding="utf-8")

        self.assertIn('"version"', text)
        self.assertIn('"text_font"', text)
        self.assertIn("font_puhui_common_20_4.bin", text)
        self.assertNotEqual("", text.strip())

    def test_ai_font_asset_text_font_file_exists(self) -> None:
        self.assertTrue(ASSET_TEXT_FONT.exists())
        self.assertGreater(ASSET_TEXT_FONT.stat().st_size, 0)

    def test_asset_packer_script_uses_xiaozhi_mmap_header_format(self) -> None:
        source = ASSET_PACKER.read_text(encoding="utf-8")

        self.assertIn('MAGIC_PREFIX = b"\\x5A\\x5A"', source)
        self.assertIn("compute_checksum", source)
        self.assertIn('--max-size', source)
        self.assertIn('struct.pack("<I", len(file_info_list))', source)
        self.assertIn('struct.pack("<I", len(combined_data))', source)
        self.assertIn('struct.pack("<H", width)', source)
        self.assertIn('struct.pack("<H", height)', source)

    def test_root_cmakelists_builds_and_flashes_assets_partition(self) -> None:
        source = ROOT_CMAKELISTS.read_text(encoding="utf-8")

        self.assertIn("build_ai_font_assets.py", source)
        self.assertIn('esptool_py_flash_to_partition(flash "assets"', source)
        self.assertIn("add_custom_target(ai_font_assets_bin ALL", source)


if __name__ == "__main__":
    unittest.main()
