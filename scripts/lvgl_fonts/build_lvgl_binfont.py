#!/usr/bin/env python3
"""生成可由 LVGL `lv_binfont_create()` 直接加载的中文 binfont。"""

from __future__ import annotations

import argparse
import shutil
import subprocess
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
LV_FONT_CONV_PACKAGE = "lv_font_conv@1.5.3"
DEFAULT_LATIN_FONT = REPO_ROOT / "tools" / "lvgl_fonts" / "fonts" / "montserratMedium.ttf"
DEFAULT_CHINESE_FONT = (
    REPO_ROOT / "tools" / "lvgl_fonts" / "fonts" / "LXGWWenKai-Regular.ttf"
)
DEFAULT_CHARSET = (
    REPO_ROOT
    / "tools"
    / "lvgl_fonts"
    / "charsets"
    / "charset_tghz_level1_3500.txt"
)
DEFAULT_OUTPUT_DIR = REPO_ROOT / "resources" / "fonts"
DEFAULT_BPP = 4
ASCII_RANGE = "0x20-0x7E"


def read_charset(path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    chars = [char for char in text if not char.isspace()]
    if not chars:
        raise ValueError(f"charset is empty: {path}")
    return "".join(dict.fromkeys(chars))


def default_output_name(size: int, charset_name: str, bpp: int) -> str:
    return f"lvgl_montserrat_lxgw_{charset_name}_{size}_{bpp}.bin"


def build_font(args: argparse.Namespace) -> Path:
    latin_font = Path(args.latin_font)
    chinese_font = Path(args.chinese_font)
    charset = Path(args.charset)

    for path in (latin_font, chinese_font, charset):
        if not path.exists():
            raise FileNotFoundError(path)

    charset_symbols = read_charset(charset)
    output = Path(args.output) if args.output else Path(args.output_dir) / default_output_name(
        args.size,
        args.name,
        args.bpp,
    )
    output.parent.mkdir(parents=True, exist_ok=True)

    npx = shutil.which("npx") or shutil.which("npx.cmd")
    if npx is None:
        raise FileNotFoundError("npx or npx.cmd")

    command = [
        npx,
        "--yes",
        "--package",
        LV_FONT_CONV_PACKAGE,
        "lv_font_conv",
        "--font",
        str(latin_font),
        "--range",
        ASCII_RANGE,
        "--font",
        str(chinese_font),
        "--symbols",
        charset_symbols,
        "--size",
        str(args.size),
        "--bpp",
        str(args.bpp),
        "--format",
        "bin",
        "--output",
        str(output),
    ]
    subprocess.run(command, check=True)
    return output


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="生成 LVGL 原生 .bin 字体")
    parser.add_argument("--size", type=int, required=True, help="字号，例如 24")
    parser.add_argument("--bpp", type=int, default=DEFAULT_BPP, help="位深，默认 4")
    parser.add_argument("--name", default="tghz_level1_3500", help="输出文件名中的字符集标签")
    parser.add_argument("--latin-font", default=DEFAULT_LATIN_FONT, help="拉丁字体 TTF")
    parser.add_argument("--chinese-font", default=DEFAULT_CHINESE_FONT, help="中文字体 TTF")
    parser.add_argument("--charset", default=DEFAULT_CHARSET, help="中文字符集文本")
    parser.add_argument("--output-dir", default=DEFAULT_OUTPUT_DIR, help="默认输出目录")
    parser.add_argument("--output", help="指定完整输出文件路径")
    return parser.parse_args()


def main() -> None:
    output = build_font(parse_args())
    print(f"generated: {output}")


if __name__ == "__main__":
    main()
