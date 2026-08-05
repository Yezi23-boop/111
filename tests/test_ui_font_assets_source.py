import importlib.util
import json
import pathlib
import sys
import tempfile
import unittest

from tests.main_cmake_contract import assert_main_source_globbed


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
PARTITIONS = REPO_ROOT / "partitions.csv"
ASSET_INDEX = REPO_ROOT / "assets" / "ai-fonts" / "index.json"
ASSET_PACKER = REPO_ROOT / "scripts" / "build_ai_font_assets.py"


def load_module(path: pathlib.Path):
    spec = importlib.util.spec_from_file_location(path.stem, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[path.stem] = module
    spec.loader.exec_module(module)
    return module


class UiFontAssetsSourceTests(unittest.TestCase):
    def test_shared_ai_chat_view_uses_raw_text_font_and_error_state(self) -> None:
        source = AI_CHAT_VIEW_SOURCE.read_text(encoding="utf-8")

        self.assertIn("ui_font_assets_init()", source)
        self.assertIn("ui_font_assets_text()", source)
        self.assertIn('"FONT ASSET ERROR"', source)
        self.assertIn("ai_chat_view_show_font_error", source)
        self.assertNotIn("ui_font_assets_body()", source)
        self.assertNotIn("ui_font_assets_title()", source)
        self.assertNotIn("font_puhui", source)

    def test_custom_header_exposes_font_assets_layer(self) -> None:
        source = CUSTOM_HEADER.read_text(encoding="utf-8")
        self.assertIn('#include "ui_font_assets.h"', source)

    def test_font_assets_header_declares_two_profile_api(self) -> None:
        source = FONT_ASSETS_HEADER.read_text(encoding="utf-8")

        self.assertIn("esp_err_t ui_font_assets_init(void);", source)
        self.assertIn("bool ui_font_assets_ready(void);", source)
        self.assertIn("const lv_font_t *ui_font_assets_text(void);", source)
        self.assertIn("const lv_font_t *ui_font_assets_hermes(void);", source)
        self.assertIn("const lv_font_t *ui_font_assets_icon(void);", source)
        self.assertNotIn("ui_font_assets_body", source)
        self.assertNotIn("ui_font_assets_title", source)

    def test_font_assets_source_maps_and_validates_both_cbin_profiles(self) -> None:
        source = FONT_ASSETS_SOURCE.read_text(encoding="utf-8")

        for token in (
            "esp_partition_read",
            "esp_partition_mmap",
            "esp_partition_munmap",
            "spi_flash_mmap_get_free_pages",
            "SPI_FLASH_MMU_PAGE_SIZE",
            '"assets"',
            '"index.json"',
            '"text_font_meta"',
            '"hermes_text_font_meta"',
            '"font_noto_sans_common_20_4.bin"',
            '"font_noto_sans_common_16_4.bin"',
            "kExpectedBundle",
            "entry_data[0] != 'Z'",
            "ESP_ERR_INVALID_CRC",
        ):
            self.assertIn(token, source)

        for forbidden in (
            "BUILTIN_TEXT_FONT",
            "font_puhui",
            "lv_binfont_create",
            "fallback",
            "common5500",
        ):
            self.assertNotIn(forbidden, source)

    def test_font_assets_source_does_not_create_basic_font(self) -> None:
        source = FONT_ASSETS_SOURCE.read_text(encoding="utf-8")
        self.assertNotIn("font_noto_sans_basic_20_4", source)
        self.assertNotIn("s_builtin_text_font", source)
        self.assertIn("const lv_font_t *ui_font_assets_text(void)", source)

    def test_main_cmakelists_has_no_embedded_text_font(self) -> None:
        source = MAIN_CMAKELISTS.read_text(encoding="utf-8")

        assert_main_source_globbed(self, "ui/custom/ui_font_assets.c")
        assert_main_source_globbed(self, "ui/custom/cbin_font_bridge.c")
        self.assertIn("78__xiaozhi-fonts", source)
        self.assertNotIn("BUILTIN_TEXT_FONT", source)
        self.assertNotIn("EMBED_FILES", source)

    def test_manifest_uses_official_versions(self) -> None:
        source = MAIN_MANIFEST.read_text(encoding="utf-8")
        self.assertIn("lvgl/lvgl: ~9.5.0", source)
        self.assertIn("78/xiaozhi-fonts: 2.0.0", source)

    def test_asset_index_declares_both_profiles(self) -> None:
        index = json.loads(ASSET_INDEX.read_text(encoding="utf-8"))
        self.assertEqual(2, index["version"])
        self.assertEqual("noto-v1", index["bundle"])
        self.assertEqual("font_noto_sans_common_20_4.bin", index["text_font"])
        self.assertEqual("font_noto_sans_common_16_4.bin", index["hermes_text_font"])
        self.assertEqual(index["text_font_meta"], {
            "bundle": "noto-v1", "charset": "common", "size": 20, "bpp": 4,
        })
        self.assertEqual(index["hermes_text_font_meta"], {
            "bundle": "noto-v1", "charset": "common", "size": 16, "bpp": 4,
        })

    def test_asset_packer_validates_metadata_and_is_deterministic(self) -> None:
        module = load_module(ASSET_PACKER)
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            fonts_dir = root / "xiaozhi-fonts"
            (fonts_dir / "cbin").mkdir(parents=True)
            (fonts_dir / "charsets").mkdir()
            (fonts_dir / "manifest.json").write_text(json.dumps({
                "family": "noto",
                "bundle_id": "noto-v1",
                "tokenizer": {
                    "model": "deepseek-ai/DeepSeek-V4-Flash",
                    "core_vocab_only": True,
                },
                "text_profiles": [
                    {"size": 16, "bpp": 4}, {"size": 20, "bpp": 4},
                ],
            }), encoding="utf-8")
            (fonts_dir / "charsets" / "common.json").write_text(
                json.dumps({"charset": "common", "count": 1}), encoding="utf-8"
            )
            (fonts_dir / "cbin" / "font_noto_sans_common_20_4.bin").write_bytes(b"20")
            (fonts_dir / "cbin" / "font_noto_sans_common_16_4.bin").write_bytes(b"16")
            index = root / "index.json"
            index.write_text(ASSET_INDEX.read_text(encoding="utf-8"), encoding="utf-8")

            module.validate_xiaozhi_fonts(fonts_dir)
            index_data = json.loads(index.read_text(encoding="utf-8"))
            module.validate_index(index_data)
            files = [("index.json", index)] + [
                (name, fonts_dir / "cbin" / name)
                for _, _, name, _ in module.FONT_SPECS
            ]
            self.assertEqual(module.build_assets_image(files), module.build_assets_image(files))

    def test_root_cmakelists_builds_and_flashes_assets_partition(self) -> None:
        source = ROOT_CMAKELISTS.read_text(encoding="utf-8")
        self.assertIn("build_ai_font_assets.py", source)
        self.assertIn("--xiaozhi-fonts-dir", source)
        self.assertIn('esptool_py_flash_to_partition(flash "assets"', source)
        self.assertIn("add_custom_target(ai_font_assets_bin ALL", source)

    def test_partition_layout_keeps_model_offset(self) -> None:
        source = PARTITIONS.read_text(encoding="utf-8")
        self.assertIn("assets,   data, spiffs,   ,       3M", source)
        self.assertIn("resources,data, littlefs, ,       3M", source)
        self.assertIn("model,    data, spiffs,   ,       0x1E0000", source)

    def test_cbin_font_bridge_uses_component_loader(self) -> None:
        source = CBIN_FONT_BRIDGE_SOURCE.read_text(encoding="utf-8")
        self.assertIn('#include "cbin_font.h"', source)
        self.assertIn("cbin_font_create", source)
        self.assertIn("cbin_font_delete", source)


if __name__ == "__main__":
    unittest.main()
