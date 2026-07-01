"""Pack extracted watchface PNG frames into simple RGB565 raw animations.

The packer is intentionally conservative:

- It first estimates the raw RGB565 payload size.
- If the total output would exceed the resources budget, it writes the files
  to a local SD-card staging directory instead of bloating resources/.
- The runtime format is simple enough for an ESP32 loader to validate and load
  into PSRAM without giving LVGL a file path as an image source.
"""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path
from typing import Any

from PIL import Image


MAGIC = b"RAWANIM1"
VERSION = 1
HEADER_STRUCT = struct.Struct("<8sHHHHHHII")
FRAME_STRUCT = struct.Struct("<IIH")
FORMAT_RGB565_LE = 1
DEFAULT_INPUT = Path("resources/watchface/frames")
DEFAULT_RESOURCES_OUTPUT = Path("resources/watchface")
DEFAULT_SD_OUTPUT = Path("sdcard/watchface")
DEFAULT_BUDGET = 8 * 1024 * 1024


def parse_size(value: str) -> int:
    text = value.strip().lower()
    multiplier = 1
    if text.endswith("k"):
        multiplier = 1024
        text = text[:-1]
    elif text.endswith("m"):
        multiplier = 1024 * 1024
        text = text[:-1]
    try:
        size = int(float(text) * multiplier)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("size must look like 8M, 512K, or bytes") from exc
    if size <= 0:
        raise argparse.ArgumentTypeError("size must be positive")
    return size


def rgb888_to_rgb565_le_bytes(image: Image.Image) -> bytes:
    rgb = image.convert("RGB")
    rgb_bytes = rgb.tobytes()
    raw = bytearray(rgb.width * rgb.height * 2)
    out = 0
    for index in range(0, len(rgb_bytes), 3):
        red = rgb_bytes[index]
        green = rgb_bytes[index + 1]
        blue = rgb_bytes[index + 2]
        value = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)
        raw[out] = value & 0xFF
        raw[out + 1] = (value >> 8) & 0xFF
        out += 2
    return bytes(raw)


def read_manifest(input_dir: Path) -> dict[str, Any]:
    manifest_path = input_dir / "manifest.json"
    if not manifest_path.exists():
        raise FileNotFoundError(f"missing frame manifest: {manifest_path}")
    return json.loads(manifest_path.read_text(encoding="utf-8"))


def estimate_state_sizes(frame_manifest: dict[str, Any]) -> dict[str, int]:
    width, height = frame_manifest["canvas"]
    frame_size = int(width) * int(height) * 2
    return {
        state: frame_size * int(state_info["frame_count"])
        for state, state_info in frame_manifest["states"].items()
    }


def clean_previous_outputs(output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    for old_file in output_dir.glob("*.rawanim"):
        old_file.unlink()
    for old_manifest in (output_dir / "manifest.json", output_dir / "pack_summary.json"):
        if old_manifest.exists():
            old_manifest.unlink()


def write_rawanim(
    output_path: Path,
    *,
    input_dir: Path,
    state: str,
    state_info: dict[str, Any],
    canvas: tuple[int, int],
    fps: float,
    delay_ms: int,
) -> dict[str, Any]:
    width, height = canvas
    frame_size = width * height * 2
    frames = state_info["frames"]
    frame_count = len(frames)
    header_size = HEADER_STRUCT.size
    table_size = FRAME_STRUCT.size * frame_count
    data_offset = header_size + table_size

    table_entries: list[tuple[int, int, int]] = []
    frame_payloads: list[bytes] = []
    offset = data_offset

    for frame in frames:
        frame_path = input_dir / frame["file"]
        if not frame_path.exists():
            raise FileNotFoundError(f"missing frame image: {frame_path}")
        image = Image.open(frame_path)
        if image.size != (width, height):
            raise ValueError(f"{frame_path} has size {image.size}, expected {(width, height)}")
        payload = rgb888_to_rgb565_le_bytes(image)
        if len(payload) != frame_size:
            raise ValueError(f"{frame_path} produced {len(payload)} bytes, expected {frame_size}")
        table_entries.append((offset, frame_size, int(delay_ms)))
        frame_payloads.append(payload)
        offset += frame_size

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("wb") as out:
        out.write(
            HEADER_STRUCT.pack(
                MAGIC,
                VERSION,
                width,
                height,
                FORMAT_RGB565_LE,
                frame_count,
                int(round(fps)),
                int(round(1000 * fps)),
                data_offset,
            )
        )
        for entry in table_entries:
            out.write(FRAME_STRUCT.pack(*entry))
        for payload in frame_payloads:
            out.write(payload)

    file_size = output_path.stat().st_size
    return {
        "file": output_path.name,
        "path": str(output_path),
        "state": state,
        "source": state_info.get("source"),
        "width": width,
        "height": height,
        "format": "rgb565_le",
        "frame_count": frame_count,
        "fps": fps,
        "delay_ms": delay_ms,
        "frame_size": frame_size,
        "file_size": file_size,
        "frames": frames,
    }


def write_pack_manifest(output_dir: Path, frame_manifest: dict[str, Any], state_entries: dict[str, Any], target: str) -> None:
    manifest = {
        "format": "rawanim",
        "magic": MAGIC.decode("ascii"),
        "version": VERSION,
        "target": target,
        "canvas": frame_manifest["canvas"],
        "background": frame_manifest.get("background", "#ffffff"),
        "fit": frame_manifest.get("fit", "contain"),
        "states": state_entries,
    }
    (output_dir / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def pack(args: argparse.Namespace) -> dict[str, Any]:
    input_dir = args.input.resolve()
    resources_output = args.resources_output.resolve()
    sd_output = args.sd_output.resolve()
    frame_manifest = read_manifest(input_dir)
    state_sizes = estimate_state_sizes(frame_manifest)
    total_payload = sum(state_sizes.values())
    estimated_total = total_payload
    target = "resources" if estimated_total <= args.resources_budget else "sdcard"
    output_dir = resources_output if target == "resources" else sd_output

    clean_previous_outputs(output_dir)
    state_entries: dict[str, Any] = {}
    width, height = map(int, frame_manifest["canvas"])
    fps = float(frame_manifest.get("fps", 6.0))
    delay_ms = int(frame_manifest.get("delay_ms", round(1000 / fps)))

    for state, state_info in frame_manifest["states"].items():
        output_path = output_dir / f"{state}.rawanim"
        state_entries[state] = write_rawanim(
            output_path,
            input_dir=input_dir,
            state=state,
            state_info=state_info,
            canvas=(width, height),
            fps=fps,
            delay_ms=delay_ms,
        )

    write_pack_manifest(output_dir, frame_manifest, state_entries, target)

    actual_total = sum(entry["file_size"] for entry in state_entries.values())
    manifest_size = (output_dir / "manifest.json").stat().st_size
    summary = {
        "target": target,
        "reason": (
            "estimated raw animation output fits resources budget"
            if target == "resources"
            else "estimated raw animation output exceeds resources budget; generated SD-card staging output"
        ),
        "resources_budget": args.resources_budget,
        "input": str(input_dir),
        "output": str(output_dir),
        "raw_payload_bytes": total_payload,
        "rawanim_files_bytes": actual_total,
        "manifest_bytes": manifest_size,
        "total_bytes": actual_total + manifest_size,
        "state_payload_estimates": state_sizes,
        "states": {
            state: {
                "file": entry["file"],
                "frame_count": entry["frame_count"],
                "file_size": entry["file_size"],
            }
            for state, entry in state_entries.items()
        },
    }
    (output_dir / "pack_summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )

    print(f"target: {target}")
    print(f"output: {output_dir}")
    print(f"budget: {args.resources_budget} bytes")
    print(f"raw payload: {total_payload} bytes")
    print(f"rawanim files: {actual_total} bytes")
    print(f"total with manifest: {actual_total + manifest_size} bytes")
    for state, entry in summary["states"].items():
        print(f"{state}: {entry['frame_count']} frames, {entry['file_size']} bytes")
    if target == "sdcard":
        print("resources budget exceeded; copy this output directory to the SD card as /watchface")
    return summary


def main() -> None:
    parser = argparse.ArgumentParser(description="Pack watchface preview frames into raw animation files.")
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT, help="Frame manifest directory.")
    parser.add_argument("--resources-output", type=Path, default=DEFAULT_RESOURCES_OUTPUT, help="resources output directory.")
    parser.add_argument("--sd-output", type=Path, default=DEFAULT_SD_OUTPUT, help="SD-card staging output directory.")
    parser.add_argument("--resources-budget", type=parse_size, default=DEFAULT_BUDGET, help="resources budget, e.g. 8M.")
    args = parser.parse_args()
    pack(args)


if __name__ == "__main__":
    main()
