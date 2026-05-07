#!/usr/bin/env python3
"""确保仓库预置的 LVGL 中文 binfont 已生成并处于分区预算内。"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT = Path(__file__).with_name("build_lvgl_binfont.py")
RESOURCES_FONTS_DIR = REPO_ROOT / "resources" / "fonts"
PRESET_SIZES = (16, 22, 24, 27)
PRESET_NAME = "tghz_level1_3500"
PRESET_BPP = 4
RESOURCES_PARTITION_BUDGET_BYTES = 4 * 1024 * 1024


def preset_path(size: int) -> Path:
    return RESOURCES_FONTS_DIR / f"lvgl_montserrat_lxgw_{PRESET_NAME}_{size}_{PRESET_BPP}.bin"


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
    parser = argparse.ArgumentParser(description="生成并检查预置 LVGL 中文字体")
    parser.add_argument("--force", action="store_true", help="重新生成已有字体")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    outputs = [ensure_font(size, args.force) for size in PRESET_SIZES]
    total_size = sum(path.stat().st_size for path in outputs)

    for path in outputs:
        print(f"{path.name}: {path.stat().st_size} bytes")
    print(f"total: {total_size} bytes")

    if total_size > RESOURCES_PARTITION_BUDGET_BYTES:
        raise ValueError(
            f"preset fonts exceed resources budget: {total_size} > {RESOURCES_PARTITION_BUDGET_BYTES}"
        )


if __name__ == "__main__":
    main()
