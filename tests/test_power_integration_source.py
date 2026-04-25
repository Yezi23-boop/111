import unittest
import re

from tests.main_paths import APP_MAIN_SOURCE
from tests.main_paths import HARDWARE_INIT_SOURCE
from tests.main_paths import REPO_ROOT


MAIN_CMAKE = REPO_ROOT / "main" / "CMakeLists.txt"


def _strip_c_comments_and_strings(source: str) -> str:
    result = []
    i = 0
    length = len(source)
    in_block_comment = False
    in_line_comment = False
    in_string = False
    in_char = False

    while i < length:
        ch = source[i]
        nxt = source[i + 1] if i + 1 < length else ""

        if in_block_comment:
            if ch == "*" and nxt == "/":
                in_block_comment = False
                i += 2
            else:
                i += 1
            continue

        if in_line_comment:
            if ch == "\n":
                in_line_comment = False
                result.append(ch)
            i += 1
            continue

        if in_string:
            if ch == "\\" and i + 1 < length:
                i += 2
                continue
            if ch == '"':
                in_string = False
            i += 1
            continue

        if in_char:
            if ch == "\\" and i + 1 < length:
                i += 2
                continue
            if ch == "'":
                in_char = False
            i += 1
            continue

        if ch == "/" and nxt == "*":
            in_block_comment = True
            i += 2
            continue

        if ch == "/" and nxt == "/":
            in_line_comment = True
            i += 2
            continue

        if ch == '"':
            in_string = True
            i += 1
            continue

        if ch == "'":
            in_char = True
            i += 1
            continue

        result.append(ch)
        i += 1

    return "".join(result)


def _extract_c_function_body(source: str, function_name: str) -> str:
    signature = re.search(
        rf"\b{re.escape(function_name)}\s*\(\s*void\s*\)\s*\{{", source
    )
    if signature is None:
        return ""

    brace_start = source.find("{", signature.end() - 1)
    if brace_start < 0:
        return ""

    depth = 0
    for index in range(brace_start, len(source)):
        ch = source[index]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return source[brace_start + 1 : index]
    return ""


def _extract_cmake_call_body(source: str, call_name: str) -> str:
    match = re.search(
        rf"\b{re.escape(call_name)}\s*\((.*?)\n\)",
        source,
        re.S,
    )
    if match is None:
        return ""
    return match.group(1)


def _extract_named_cmake_set_body(source: str, block_name: str) -> str:
    match = re.search(
        rf"set\s*\(\s*{re.escape(block_name)}\s*(.*?)\n\)",
        source,
        re.S,
    )
    if match is None:
        return ""
    return match.group(1)


class PowerIntegrationSourceTests(unittest.TestCase):
    def test_hardware_init_brings_in_board_power_after_audio_init(self) -> None:
        self.assertTrue(HARDWARE_INIT_SOURCE.exists(), "main/app/hardware_init.c should exist")
        source = _extract_c_function_body(
            _strip_c_comments_and_strings(
                HARDWARE_INIT_SOURCE.read_text(encoding="utf-8")
            ),
            "hardware_init",
        )

        audio_pos = source.find("audio_codec_init()")
        board_pos = source.find("board_power_init()")
        self.assertGreaterEqual(audio_pos, 0)
        self.assertGreaterEqual(board_pos, 0)
        self.assertLess(audio_pos, board_pos)

    def test_hardware_init_logs_boot_power_snapshot_after_board_power_init(self) -> None:
        self.assertTrue(HARDWARE_INIT_SOURCE.exists(), "main/app/hardware_init.c should exist")
        source = HARDWARE_INIT_SOURCE.read_text(encoding="utf-8")
        stripped_source = _strip_c_comments_and_strings(source)
        body = _extract_c_function_body(stripped_source, "hardware_init")

        init_pos = body.find("board_power_init()")
        refresh_pos = body.find("board_power_refresh(")
        self.assertGreaterEqual(init_pos, 0)
        self.assertGreaterEqual(refresh_pos, 0)
        self.assertLess(init_pos, refresh_pos)
        self.assertIn("board_power_log_boot_snapshot", source)
        self.assertIn("Board power boot snapshot:", source)

    def test_app_main_starts_power_service_after_lvgl_task(self) -> None:
        self.assertTrue(APP_MAIN_SOURCE.exists(), "main/app/app_main.c should exist")
        source = _extract_c_function_body(
            _strip_c_comments_and_strings(
                APP_MAIN_SOURCE.read_text(encoding="utf-8")
            ),
            "app_main",
        )

        lvgl_pos = source.find("xTaskCreatePinnedToCore(lvgl_task,")
        power_pos = source.find("power_service_start()")
        self.assertGreaterEqual(lvgl_pos, 0)
        self.assertGreaterEqual(power_pos, 0)
        self.assertLess(lvgl_pos, power_pos)

    def test_main_cmake_registers_new_sources_and_axp2101_dependency(self) -> None:
        self.assertTrue(MAIN_CMAKE.exists(), "main/CMakeLists.txt should exist")
        source = MAIN_CMAKE.read_text(encoding="utf-8")

        app_srcs = _extract_named_cmake_set_body(source, "app_srcs")
        service_srcs = _extract_named_cmake_set_body(source, "service_srcs")
        register_block = _extract_cmake_call_body(source, "idf_component_register")

        self.assertIn("${CMAKE_CURRENT_LIST_DIR}/app/board_power.c", app_srcs)
        self.assertIn("${CMAKE_CURRENT_LIST_DIR}/services/power_service.c", service_srcs)
        self.assertRegex(
            register_block,
            r"\bREQUIRES\b[\s\S]*?\baxp2101\b",
        )


if __name__ == "__main__":
    unittest.main()
