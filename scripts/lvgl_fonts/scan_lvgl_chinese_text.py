#!/usr/bin/env python3
"""扫描手写 LVGL UI 源码中的中文字符串和字体绑定风险。"""

from __future__ import annotations

import argparse
import ast
import re
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SCAN_DIRS = (
    REPO_ROOT / "main" / "ui" / "custom",
    REPO_ROOT / "main" / "ui" / "agent",
    REPO_ROOT / "main" / "ui" / "agent_ui",
)
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp"}
CHINESE_RE = re.compile(r"[\u4e00-\u9fff]")
STRING_RE = re.compile(r'L?u?8?"(?:\\.|[^"\\])*"')
CHINESE_FONT_MARKERS = (
    "lv_binfont_create",
    'A:/fonts/',
    "ui_font_assets_",
    "ui_runtime_fonts_",
    "lvgl_montserrat_lxgw",
)
LABEL_API_MARKERS = (
    "lv_label_create",
    "lv_label_set_text",
    "lv_obj_set_style_text_font",
)
GUI_GUIDER_GLUE_MARKERS = (
    '#include "gui_guider.h"',
    "guider_ui.",
)


@dataclass(frozen=True)
class Finding:
    path: Path
    line: int
    text: str
    reason: str


def strip_comments(source: str) -> str:
    result: list[str] = []
    i = 0
    in_block = False
    in_line = False
    in_string = False
    escape = False

    while i < len(source):
        char = source[i]
        next_char = source[i + 1] if i + 1 < len(source) else ""

        if in_line:
            if char == "\n":
                in_line = False
                result.append(char)
            else:
                result.append(" ")
            i += 1
            continue

        if in_block:
            if char == "*" and next_char == "/":
                result.extend("  ")
                in_block = False
                i += 2
            else:
                result.append("\n" if char == "\n" else " ")
                i += 1
            continue

        if in_string:
            result.append(char)
            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == '"':
                in_string = False
            i += 1
            continue

        if char == "/" and next_char == "/":
            result.extend("  ")
            in_line = True
            i += 2
            continue
        if char == "/" and next_char == "*":
            result.extend("  ")
            in_block = True
            i += 2
            continue
        if char == '"':
            in_string = True

        result.append(char)
        i += 1

    return "".join(result)


def decode_c_string(token: str) -> str:
    literal = token
    if literal.startswith("u8"):
        literal = literal[1:]
    if literal.startswith("L"):
        literal = literal[1:]
    try:
        value = ast.literal_eval(literal)
    except (SyntaxError, ValueError):
        return token
    return value if isinstance(value, str) else token


def iter_source_files(scan_dirs: tuple[Path, ...]) -> list[Path]:
    files: list[Path] = []
    for scan_dir in scan_dirs:
        if not scan_dir.exists():
            continue
        for path in scan_dir.rglob("*"):
            if path.is_file() and path.suffix in SOURCE_SUFFIXES:
                files.append(path)
    return sorted(files)


def scan_file(path: Path) -> list[Finding]:
    raw = path.read_text(encoding="utf-8", errors="ignore")
    source = strip_comments(raw)
    findings: list[Finding] = []
    uses_chinese_font = any(marker in source for marker in CHINESE_FONT_MARKERS)
    touches_label_api = any(marker in source for marker in LABEL_API_MARKERS)

    if not touches_label_api or any(marker in source for marker in GUI_GUIDER_GLUE_MARKERS):
        return findings

    for match in STRING_RE.finditer(source):
        decoded = decode_c_string(match.group(0))
        if not CHINESE_RE.search(decoded):
            continue

        line = source.count("\n", 0, match.start()) + 1
        if "&lv_font_montserrat" in source[max(0, match.start() - 240) : match.end() + 240]:
            findings.append(
                Finding(path, line, decoded, "中文字符串附近直接绑定 lv_font_montserrat")
            )
        elif not uses_chinese_font:
            findings.append(
                Finding(path, line, decoded, "文件含中文字符串但未发现中文字体入口")
            )

    return findings


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="扫描 LVGL 中文 UI 字体风险")
    parser.add_argument(
        "--scan-dir",
        action="append",
        default=[],
        help="追加扫描目录；默认扫描 main/ui/custom 与 agent UI 目录",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    scan_dirs = tuple(Path(item) for item in args.scan_dir) or DEFAULT_SCAN_DIRS
    findings: list[Finding] = []
    for path in iter_source_files(scan_dirs):
        findings.extend(scan_file(path))

    for finding in findings:
        rel_path = finding.path.relative_to(REPO_ROOT)
        print(f"{rel_path}:{finding.line}: {finding.reason}: {finding.text}")

    if findings:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
