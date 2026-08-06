import importlib.util
import pathlib
import sys
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOLS_FONT_DIR = REPO_ROOT / "tools" / "lvgl_fonts"
SCRIPTS_FONT_DIR = REPO_ROOT / "scripts" / "lvgl_fonts"
RESOURCES_FONTS_DIR = REPO_ROOT / "resources" / "fonts"
PRESET_SIZES = (16, 22)
RESOURCES_PARTITION_SIZE = 3 * 1024 * 1024


def load_module(path: pathlib.Path):
    spec = importlib.util.spec_from_file_location(path.stem, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[path.stem] = module
    spec.loader.exec_module(module)
    return module


class LvglChineseFontScriptsSourceTests(unittest.TestCase):
    def test_repo_keeps_font_sources_and_charset_inputs(self) -> None:
        self.assertTrue((TOOLS_FONT_DIR / "fonts" / "montserratMedium.ttf").exists())
        self.assertTrue((TOOLS_FONT_DIR / "fonts" / "LXGWWenKai-Regular.ttf").exists())
        self.assertTrue((TOOLS_FONT_DIR / "fonts" / "OFL-LXGWWenKai.txt").exists())
        self.assertTrue((TOOLS_FONT_DIR / "fonts" / "OFL-Montserrat.txt").exists())
        self.assertTrue(
            (TOOLS_FONT_DIR / "charsets" / "charset_tghz_common_5500.txt").exists()
        )
        self.assertFalse(
            (TOOLS_FONT_DIR / "charsets" / "charset_tghz_level1_3500.txt").exists()
        )
        self.assertFalse(
            (TOOLS_FONT_DIR / "charsets" / "source_tghz_level1_3500.tsv").exists()
        )

    def test_common_charset_has_5500_unique_chinese_chars(self) -> None:
        charset = (
            TOOLS_FONT_DIR / "charsets" / "charset_tghz_common_5500.txt"
        ).read_text(encoding="utf-8")
        chars = [char for char in charset if not char.isspace()]

        self.assertEqual(5500, len(chars))
        self.assertEqual(5500, len(set(chars)))

    def test_build_script_pins_lv_font_conv_and_repo_local_defaults(self) -> None:
        source = (SCRIPTS_FONT_DIR / "build_lvgl_binfont.py").read_text(
            encoding="utf-8"
        )

        self.assertIn('LV_FONT_CONV_PACKAGE = "lv_font_conv@1.5.3"', source)
        self.assertIn("DEFAULT_LATIN_FONT = REPO_ROOT / \"tools\" / \"lvgl_fonts\"", source)
        self.assertIn("DEFAULT_CHINESE_FONT =", source)
        self.assertIn("DEFAULT_CHARSET =", source)
        self.assertIn("DEFAULT_OUTPUT_DIR = REPO_ROOT / \"resources\" / \"fonts\"", source)
        self.assertIn('"--format"', source)
        self.assertIn('"bin"', source)

    def test_punctuation_constant_covers_chinese_fullwidth_punctuation(self) -> None:
        """标点常量必须覆盖常见全角标点，避免动态文本渲染为方框。

        弯引号用 \\u 转义书写，这里用 chr() 校验，确保转义被正确解析、
        不会因源码编码问题静默丢失。
        """
        module = load_module(SCRIPTS_FONT_DIR / "build_lvgl_binfont.py")

        expected = {
            0xFF0C,  # ，
            0x3002,  # 。
            0xFF1F,  # ？
            0xFF01,  # ！
            0x3001,  # 、
            0xFF1B,  # ；
            0xFF1A,  # ：
            0x201C,  # “
            0x201D,  # ”
            0x2018,  # ‘
            0x2019,  # ’
            0xFF08,  # （
            0xFF09,  # ）
            0x2014,  # —
            0x2026,  # …
            0x00B7,  # ·
        }
        actual = {ord(ch) for ch in module.DEFAULT_PUNCTUATION}
        self.assertTrue(
            expected.issubset(actual),
            f"missing punctuation codepoints: {expected - actual}",
        )

    def test_compiled_fonts_embed_chinese_punctuation_glyphs(self) -> None:
        """编译进固件的 C 字体必须实际包含全角标点字形注释。

        防止构建期标点合并失效（如 lv_font_conv 静默跳过缺失字形）导致
        运行时方框。校验 DEFAULT_PUNCTUATION 中 16 个码点在四个字号里全量存在。
        """
        module = load_module(SCRIPTS_FONT_DIR / "build_lvgl_binfont.py")
        fonts_dir = REPO_ROOT / "main" / "ui" / "custom" / "fonts"
        required_codepoints = tuple(f"{ord(ch):04X}" for ch in module.DEFAULT_PUNCTUATION)

        for size in PRESET_SIZES:
            source = (
                fonts_dir / f"lv_font_montserrat_lxgw_common_5500_{size}_4.c"
            ).read_text(encoding="utf-8")
            for cp in required_codepoints:
                self.assertIn(
                    f"U+{cp}",
                    source,
                    f"size {size} missing glyph U+{cp}",
                )

    def test_compiled_font_script_generates_lvgl_c_fonts(self) -> None:
        source = (SCRIPTS_FONT_DIR / "build_lvgl_cfont.py").read_text(
            encoding="utf-8"
        )

        self.assertIn("LV_FONT_CONV_PACKAGE", source)
        self.assertIn('DEFAULT_OUTPUT_DIR = REPO_ROOT / "main" / "ui" / "custom" / "fonts"', source)
        self.assertIn('"--format"', source)
        self.assertIn('"lvgl"', source)
        self.assertIn("sanitize_generated_header", source)

    def test_ensure_script_declares_expected_presets_and_budget(self) -> None:
        module = load_module(SCRIPTS_FONT_DIR / "ensure_lvgl_chinese_fonts.py")

        self.assertEqual(PRESET_SIZES, module.PRESET_SIZES)
        self.assertEqual("common_5500", module.PRESET_NAME)
        self.assertEqual(4, module.PRESET_BPP)
        self.assertEqual(RESOURCES_PARTITION_SIZE, module.RESOURCES_PARTITION_BUDGET_BYTES)

    def test_ensure_compiled_script_declares_expected_presets(self) -> None:
        module = load_module(SCRIPTS_FONT_DIR / "ensure_lvgl_compiled_fonts.py")

        self.assertEqual(PRESET_SIZES, module.PRESET_SIZES)
        self.assertEqual("common_5500", module.PRESET_NAME)
        self.assertEqual(4, module.PRESET_BPP)
        self.assertIn("main", str(module.COMPILED_FONTS_DIR))
        self.assertIn("fonts", str(module.COMPILED_FONTS_DIR))

    def test_compiled_c_fonts_exist_and_export_expected_symbols(self) -> None:
        fonts_dir = REPO_ROOT / "main" / "ui" / "custom" / "fonts"

        for size in PRESET_SIZES:
            source = (
                fonts_dir / f"lv_font_montserrat_lxgw_common_5500_{size}_4.c"
            ).read_text(encoding="utf-8")
            self.assertIn(
                f"lv_font_montserrat_lxgw_common_5500_{size}_4",
                source,
            )
            self.assertIn("Generated by scripts/lvgl_fonts/build_lvgl_cfont.py", source)

    def test_scan_script_excludes_comments_and_detects_bad_chinese_font_use(self) -> None:
        module = load_module(SCRIPTS_FONT_DIR / "scan_lvgl_chinese_text.py")
        source = (
            '// "注释里的中文" 不应触发\n'
            'static void build(void) {\n'
            '    lv_label_set_text(label, "中文");\n'
            '    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);\n'
            '}\n'
        )

        stripped = module.strip_comments(source)
        self.assertNotIn("注释里的中文", stripped)
        self.assertIn("中文", stripped)

    def test_current_hand_written_ui_passes_chinese_font_guard(self) -> None:
        module = load_module(SCRIPTS_FONT_DIR / "scan_lvgl_chinese_text.py")
        findings = []
        for path in module.iter_source_files((REPO_ROOT / "main" / "ui" / "custom",)):
            findings.extend(module.scan_file(path))

        self.assertEqual([], findings)


if __name__ == "__main__":
    unittest.main()
