#!/usr/bin/env python3
"""确保默认中文 UI 使用的 LVGL C 字体已生成。"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT = Path(__file__).with_name("build_lvgl_cfont.py")
COMPILED_FONTS_DIR = REPO_ROOT / "main" / "ui" / "custom" / "fonts"
# 27px 页面文案按实际使用点生成，避免默认任务重新生成完整大字库。
PRESET_SIZES = (16, 22)
PRESET_NAME = "common_5500"
PRESET_BPP = 4


def preset_path(size: int) -> Path:
    return COMPILED_FONTS_DIR / f"lv_font_montserrat_lxgw_{PRESET_NAME}_{size}_{PRESET_BPP}.c"


def ensure_font(size: int, force: bool) -> Path:
    output = preset_path(size)
    if output.exists() and output.stat().st_size > 0 and not force:
        return output

    subprocess.run(
        [
            sys.executable,
            str(SCRIPT),
            "--size",
            str(size),
            "--output",
            str(output),
        ],
        check=True,
    )
    return output


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="生成并检查预置 LVGL C 中文字体")
    parser.add_argument("--force", action="store_true", help="重新生成已有字体")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    outputs = [ensure_font(size, args.force) for size in PRESET_SIZES]

    for path in outputs:
        print(f"{path.name}: {path.stat().st_size} bytes")


if __name__ == "__main__":
    main()
