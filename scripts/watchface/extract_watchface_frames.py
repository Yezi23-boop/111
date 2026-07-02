"""Extract watchface GIFs into screen-sized preview frames.

The generated PNG frames are an intermediate artifact for visual review. They
are not part of the firmware LittleFS resources partition; a later packer turns
the selected frames into SD-card raw animation packages.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Iterable

from PIL import Image


CANVAS_W = 410
CANVAS_H = 502

DEFAULT_SOURCE_DIR = Path("D:/esp32S3") / "\u8868\u60c5\u5305"
DEFAULT_OUTPUT_DIR = Path("sdcard/watchface/frames")
DEFAULT_BACKGROUND = "#ffffff"

STATE_SOURCES = {
    "idle": "\u7a7a\u95f2.gif",
    "thinking": "\u601d\u8003.gif",
    "working": "\u5de5\u4f5c\u4e2d.gif",
    "message": "\u6765\u4fe1\u606f.gif",
    "error": "\u5931\u8d25.gif",
    "random": "\u968f\u673a.gif",
    "angry": "\u6124\u6012.gif",
}

V1_STATES = ("idle", "thinking", "working", "message", "error")

DEFAULT_FRAME_COUNTS = {
    "idle": 12,
    "thinking": 8,
    "working": 12,
    "message": 12,
    "error": 10,
    "random": 8,
    "angry": 4,
}


def parse_canvas(value: str) -> tuple[int, int]:
    try:
        width_text, height_text = value.lower().split("x", 1)
        width = int(width_text)
        height = int(height_text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("canvas must look like 410x502") from exc
    if width <= 0 or height <= 0:
        raise argparse.ArgumentTypeError("canvas dimensions must be positive")
    return width, height


def parse_background(value: str) -> tuple[int, int, int]:
    named_colors = {
        "black": (0, 0, 0),
        "white": (255, 255, 255),
    }
    normalized = value.strip().lower()
    if normalized in named_colors:
        return named_colors[normalized]
    if normalized.startswith("#"):
        normalized = normalized[1:]
    if len(normalized) != 6:
        raise argparse.ArgumentTypeError("background must be white, black, or #RRGGBB")
    try:
        return (int(normalized[0:2], 16), int(normalized[2:4], 16), int(normalized[4:6], 16))
    except ValueError as exc:
        raise argparse.ArgumentTypeError("background must be white, black, or #RRGGBB") from exc


def choose_indices(frame_count: int, target_count: int) -> list[int]:
    if frame_count <= 0:
        return []
    if target_count <= 0 or target_count >= frame_count:
        return list(range(frame_count))
    return [min(frame_count - 1, int(i * frame_count / target_count)) for i in range(target_count)]


def load_gif_frames(gif_path: Path) -> tuple[list[Image.Image], list[int]]:
    gif = Image.open(gif_path)
    frame_count = getattr(gif, "n_frames", 1)
    frames: list[Image.Image] = []
    durations: list[int] = []
    for index in range(frame_count):
        gif.seek(index)
        frames.append(gif.convert("RGBA"))
        durations.append(int(gif.info.get("duration", 100) or 100))
    return frames, durations


def fit_on_black_canvas(
    frame: Image.Image,
    canvas_size: tuple[int, int],
    *,
    resample: Image.Resampling,
    background: tuple[int, int, int],
) -> Image.Image:
    canvas_w, canvas_h = canvas_size
    scale = min(canvas_w / frame.width, canvas_h / frame.height)
    fitted_size = (max(1, round(frame.width * scale)), max(1, round(frame.height * scale)))
    fitted = frame.resize(fitted_size, resample)
    x = (canvas_w - fitted.width) // 2
    y = (canvas_h - fitted.height) // 2
    canvas = Image.new("RGBA", (canvas_w, canvas_h), (*background, 255))
    canvas.alpha_composite(fitted, (x, y))
    return canvas.convert("RGB")


def clear_previous_frames(state_dir: Path) -> None:
    state_dir.mkdir(parents=True, exist_ok=True)
    for old_frame in state_dir.glob("frame_*.png"):
        old_frame.unlink()


def iter_states(mode: str) -> Iterable[str]:
    if mode == "v1":
        return V1_STATES
    return STATE_SOURCES.keys()


def build_contact_sheet(output_dir: Path, states: list[str], frames_per_state: int = 6) -> None:
    thumbs: list[tuple[str, list[Image.Image]]] = []
    thumb_w = 82
    thumb_h = 100
    label_h = 18

    for state in states:
        state_dir = output_dir / state
        frame_paths = sorted(state_dir.glob("frame_*.png"))[:frames_per_state]
        row_frames: list[Image.Image] = []
        for frame_path in frame_paths:
            image = Image.open(frame_path).convert("RGB")
            image.thumbnail((thumb_w, thumb_h), Image.Resampling.LANCZOS)
            tile = Image.new("RGB", (thumb_w, thumb_h), (12, 12, 12))
            tile.paste(image, ((thumb_w - image.width) // 2, (thumb_h - image.height) // 2))
            row_frames.append(tile)
        if row_frames:
            thumbs.append((state, row_frames))

    if not thumbs:
        return

    sheet_w = frames_per_state * thumb_w
    sheet_h = len(thumbs) * (thumb_h + label_h)
    sheet = Image.new("RGB", (sheet_w, sheet_h), (0, 0, 0))
    for row, (state, row_frames) in enumerate(thumbs):
        y = row * (thumb_h + label_h)
        for col, image in enumerate(row_frames):
            sheet.paste(image, (col * thumb_w, y + label_h))
        # Keep labels ASCII so this script does not depend on local fonts.
        label_block = Image.new("RGB", (sheet_w, label_h), (24, 24, 24))
        sheet.paste(label_block, (0, y))
        # PIL default bitmap font is tiny; the manifest is the source of truth.
        try:
            from PIL import ImageDraw

            draw = ImageDraw.Draw(sheet)
            draw.text((4, y + 3), state, fill=(230, 230, 230))
        except Exception:
            pass

    sheet.save(output_dir / "contact_sheet.png", "PNG")


def extract_frames(args: argparse.Namespace) -> dict:
    source_dir = args.source.resolve()
    output_dir = args.output.resolve()
    canvas_size = args.canvas
    resample = Image.Resampling.NEAREST if args.resample == "nearest" else Image.Resampling.LANCZOS
    selected_states = list(iter_states(args.states))
    manifest = {
        "canvas": [canvas_size[0], canvas_size[1]],
        "fit": "contain",
        "background": f"#{args.background[0]:02x}{args.background[1]:02x}{args.background[2]:02x}",
        "fps": args.fps,
        "delay_ms": round(1000 / args.fps),
        "source_dir": str(source_dir),
        "output_dir": str(output_dir),
        "states": {},
    }

    output_dir.mkdir(parents=True, exist_ok=True)

    for state in selected_states:
        source_name = STATE_SOURCES[state]
        gif_path = source_dir / source_name
        if not gif_path.exists():
            raise FileNotFoundError(f"missing source GIF for {state}: {gif_path}")

        frames, durations = load_gif_frames(gif_path)
        target_count = args.max_frames or DEFAULT_FRAME_COUNTS[state]
        indices = choose_indices(len(frames), target_count)
        state_dir = output_dir / state
        clear_previous_frames(state_dir)

        frame_entries = []
        for out_index, source_index in enumerate(indices):
            image = fit_on_black_canvas(
                frames[source_index],
                canvas_size,
                resample=resample,
                background=args.background,
            )
            frame_name = f"frame_{out_index:03d}.png"
            frame_path = state_dir / frame_name
            image.save(frame_path, "PNG", optimize=True)
            frame_entries.append(
                {
                    "file": f"{state}/{frame_name}",
                    "source_index": source_index,
                    "source_delay_ms": durations[source_index],
                }
            )

        manifest["states"][state] = {
            "source": source_name,
            "source_size": [frames[0].width, frames[0].height],
            "source_frame_count": len(frames),
            "source_duration_ms": sum(durations),
            "frame_count": len(frame_entries),
            "frames": frame_entries,
        }
        print(
            f"{state}: {source_name} -> {len(frame_entries)} frames "
            f"({frames[0].width}x{frames[0].height} to {canvas_size[0]}x{canvas_size[1]})"
        )

    manifest_path = output_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    build_contact_sheet(output_dir, selected_states)
    print(f"manifest: {manifest_path}")
    print(f"contact sheet: {output_dir / 'contact_sheet.png'}")
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description="Extract watchface GIFs into preview PNG frames.")
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE_DIR, help="Directory containing source GIFs.")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT_DIR, help="Output frame directory.")
    parser.add_argument("--states", choices=("v1", "all"), default="v1", help="Extract V1 states or every known GIF.")
    parser.add_argument("--canvas", type=parse_canvas, default=(CANVAS_W, CANVAS_H), help="Canvas size, e.g. 410x502.")
    parser.add_argument(
        "--background",
        type=parse_background,
        default=parse_background(DEFAULT_BACKGROUND),
        help="Canvas background: white, black, or #RRGGBB.",
    )
    parser.add_argument("--fps", type=float, default=6.0, help="Playback fps recorded in the manifest.")
    parser.add_argument("--max-frames", type=int, default=0, help="Override frame count for every state.")
    parser.add_argument("--resample", choices=("smooth", "nearest"), default="smooth", help="Scaling filter.")
    args = parser.parse_args()
    if args.fps <= 0:
        raise SystemExit("--fps must be positive")
    if args.max_frames < 0:
        raise SystemExit("--max-frames must be zero or positive")
    extract_frames(args)


if __name__ == "__main__":
    main()
