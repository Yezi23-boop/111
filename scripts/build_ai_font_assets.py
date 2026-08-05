#!/usr/bin/env python3
"""Build the deterministic raw assets image for AI and Hermes Noto fonts."""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path


NAME_LENGTH = 32
MAGIC_PREFIX = b"\x5A\x5A"
EXPECTED_BUNDLE = "noto-v1"
EXPECTED_TOKENIZER = "deepseek-ai/DeepSeek-V4-Flash"
FONT_SPECS = (
    ("text_font", "text_font_meta", "font_noto_sans_common_20_4.bin", 20),
    ("hermes_text_font", "hermes_text_font_meta", "font_noto_sans_common_16_4.bin", 16),
)


def compute_checksum(data: bytes) -> int:
    return sum(data) & 0xFFFF


def load_json(path: Path) -> dict:
    if not path.is_file():
        raise FileNotFoundError(f"required file not found: {path}")
    return json.loads(path.read_text(encoding="utf-8"))


def validate_xiaozhi_fonts(fonts_dir: Path) -> None:
    manifest = load_json(fonts_dir / "manifest.json")
    if manifest.get("family") != "noto":
        raise ValueError("xiaozhi-fonts manifest family must be noto")
    if manifest.get("bundle_id") != EXPECTED_BUNDLE:
        raise ValueError(
            f"xiaozhi-fonts bundle_id must be {EXPECTED_BUNDLE}, "
            f"got {manifest.get('bundle_id')!r}"
        )

    tokenizer = manifest.get("tokenizer")
    if not isinstance(tokenizer, dict) or tokenizer.get("model") != EXPECTED_TOKENIZER:
        raise ValueError("xiaozhi-fonts manifest does not use DeepSeek-V4-Flash")
    if tokenizer.get("core_vocab_only") is not True:
        raise ValueError("xiaozhi-fonts manifest must use the tokenizer core vocabulary")

    profiles = {
        (profile.get("size"), profile.get("bpp"))
        for profile in manifest.get("text_profiles", [])
        if isinstance(profile, dict)
    }
    for _, _, _, size in FONT_SPECS:
        if (size, 4) not in profiles:
            raise ValueError(f"xiaozhi-fonts manifest is missing common {size}px/4bpp")

    charset = load_json(fonts_dir / "charsets" / "common.json")
    if charset.get("charset") != "common" or not isinstance(charset.get("count"), int):
        raise ValueError("xiaozhi-fonts common charset metadata is invalid")
    if charset["count"] <= 0:
        raise ValueError("xiaozhi-fonts common charset is empty")


def validate_index(index: dict) -> None:
    if index.get("version") != 2:
        raise ValueError("assets index version must be 2")
    if index.get("bundle") != EXPECTED_BUNDLE:
        raise ValueError(f"assets index bundle must be {EXPECTED_BUNDLE}")

    for font_key, meta_key, expected_name, expected_size in FONT_SPECS:
        if index.get(font_key) != expected_name:
            raise ValueError(f"assets index {font_key} must be {expected_name}")
        meta = index.get(meta_key)
        if not isinstance(meta, dict):
            raise ValueError(f"assets index {meta_key} must be an object")
        if meta != {
            "bundle": EXPECTED_BUNDLE,
            "charset": "common",
            "size": expected_size,
            "bpp": 4,
        }:
            raise ValueError(f"assets index {meta_key} does not match Noto common profile")


def build_assets_image(asset_files: list[tuple[str, Path]]) -> bytes:
    merged_data = bytearray()
    file_info_list: list[tuple[str, int, int, int, int]] = []

    for file_name, file_path in asset_files:
        if not file_path.is_file():
            raise FileNotFoundError(f"asset file not found: {file_path}")
        encoded_name = file_name.encode("utf-8")
        if len(encoded_name) > NAME_LENGTH:
            raise ValueError(f"asset name too long for mmap table: {file_name}")

        file_data = file_path.read_bytes()
        file_info_list.append((file_name, len(merged_data), len(file_data), 0, 0))
        merged_data.extend(MAGIC_PREFIX)
        merged_data.extend(file_data)

    mmap_table = bytearray()
    for file_name, offset, file_size, width, height in file_info_list:
        mmap_table.extend(file_name.encode("utf-8").ljust(NAME_LENGTH, b"\0"))
        mmap_table.extend(struct.pack("<I", file_size))
        mmap_table.extend(struct.pack("<I", offset))
        mmap_table.extend(struct.pack("<H", width))
        mmap_table.extend(struct.pack("<H", height))

    combined_data = mmap_table + merged_data
    return b"".join(
        (
            struct.pack("<I", len(file_info_list)),
            struct.pack("<I", compute_checksum(combined_data)),
            struct.pack("<I", len(combined_data)),
            combined_data,
        )
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="打包 Noto AI/Hermes raw assets 分区镜像")
    parser.add_argument("--xiaozhi-fonts-dir", required=True, help="xiaozhi-fonts 组件目录")
    parser.add_argument("--index", required=True, help="assets index.json 路径")
    parser.add_argument("--output", required=True, help="输出 bin 路径")
    parser.add_argument("--max-size", help="最大镜像大小，支持十进制或 0x 十六进制")
    args = parser.parse_args()

    fonts_dir = Path(args.xiaozhi_fonts_dir)
    index_path = Path(args.index)
    index = load_json(index_path)
    validate_xiaozhi_fonts(fonts_dir)
    validate_index(index)

    asset_files = [("index.json", index_path)]
    asset_files.extend(
        (font_name, fonts_dir / "cbin" / font_name)
        for _, _, font_name, _ in FONT_SPECS
    )
    image = build_assets_image(asset_files)

    if args.max_size and len(image) > int(args.max_size, 0):
        raise ValueError(
            f"assets image too large: {len(image)} bytes > partition size {args.max_size}"
        )

    output_file = Path(args.output)
    output_file.parent.mkdir(parents=True, exist_ok=True)
    output_file.write_bytes(image)


if __name__ == "__main__":
    main()
