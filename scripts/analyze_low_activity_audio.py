from __future__ import annotations

import argparse
import array
import csv
import math
import pathlib
import shutil
import statistics
import subprocess
import sys
from dataclasses import dataclass


DEFAULT_SAMPLE_RATE = 16000
DEFAULT_FRAME_MS = 100


@dataclass
class AudioMetrics:
    file_path: pathlib.Path
    duration_s: float
    overall_rms_db: float
    silence_ratio: float
    envelope_cv: float
    selected: bool


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Use ffmpeg to decode wav files and select low-activity audio."
    )
    parser.add_argument("--source-dir", required=True, help="Directory containing wav files.")
    parser.add_argument(
        "--report-path",
        help="CSV report path. Defaults to <source-dir>/low_activity_report.csv",
    )
    parser.add_argument(
        "--selected-path",
        help="Text file path for selected audio list. Defaults to <source-dir>/low_activity_selected.txt",
    )
    parser.add_argument(
        "--copy-selected-to",
        help="Optional directory to copy selected audio files into.",
    )
    parser.add_argument(
        "--recursive",
        action="store_true",
        help="Recursively scan wav files under the source directory.",
    )
    parser.add_argument(
        "--sample-rate",
        type=int,
        default=DEFAULT_SAMPLE_RATE,
        help="Decode sample rate for ffmpeg analysis.",
    )
    parser.add_argument(
        "--frame-ms",
        type=int,
        default=DEFAULT_FRAME_MS,
        help="Frame size in milliseconds for envelope analysis.",
    )
    parser.add_argument(
        "--silence-threshold-db",
        type=float,
        default=-35.0,
        help="Frames below this RMS threshold are treated as silent.",
    )
    parser.add_argument(
        "--max-rms-db",
        type=float,
        default=-22.0,
        help="Overall RMS above this threshold is not considered low-activity.",
    )
    parser.add_argument(
        "--min-silence-ratio",
        type=float,
        default=0.35,
        help="Minimum silent-frame ratio to be selected.",
    )
    parser.add_argument(
        "--max-envelope-cv",
        type=float,
        default=0.60,
        help="Maximum frame-RMS coefficient of variation to be selected.",
    )
    return parser.parse_args()


def iter_audio_files(source_dir: pathlib.Path, recursive: bool) -> list[pathlib.Path]:
    pattern = "**/*.wav" if recursive else "*.wav"
    return sorted(path for path in source_dir.glob(pattern) if path.is_file())


def decode_audio_samples(file_path: pathlib.Path, sample_rate: int) -> list[float]:
    command = [
        "ffmpeg",
        "-v",
        "error",
        "-i",
        str(file_path),
        "-ac",
        "1",
        "-ar",
        str(sample_rate),
        "-f",
        "s16le",
        "-",
    ]
    result = subprocess.run(command, capture_output=True, check=False)
    if result.returncode != 0:
        stderr = result.stderr.decode("utf-8", errors="replace")
        raise RuntimeError(f"ffmpeg decode failed for {file_path}: {stderr}")

    pcm = array.array("h")
    pcm.frombytes(result.stdout)
    if sys.byteorder != "little":
        pcm.byteswap()
    return [sample / 32768.0 for sample in pcm]


def compute_frame_rms(samples: list[float], frame_size: int) -> list[float]:
    if not samples:
        return [0.0]

    frame_rms_values: list[float] = []
    for offset in range(0, len(samples), frame_size):
        frame = samples[offset : offset + frame_size]
        if not frame:
            continue
        mean_square = sum(sample * sample for sample in frame) / len(frame)
        frame_rms_values.append(math.sqrt(mean_square))
    return frame_rms_values or [0.0]


def amplitude_to_db(amplitude: float) -> float:
    if amplitude <= 0.0:
        return -120.0
    return 20.0 * math.log10(amplitude)


def analyze_file(
    file_path: pathlib.Path,
    sample_rate: int,
    frame_ms: int,
    silence_threshold_db: float,
    max_rms_db: float,
    min_silence_ratio: float,
    max_envelope_cv: float,
) -> AudioMetrics:
    samples = decode_audio_samples(file_path, sample_rate)
    frame_size = max(1, sample_rate * frame_ms // 1000)
    frame_rms_values = compute_frame_rms(samples, frame_size)

    overall_rms = math.sqrt(sum(sample * sample for sample in samples) / len(samples)) if samples else 0.0
    overall_rms_db = amplitude_to_db(overall_rms)
    silence_threshold_amp = 10.0 ** (silence_threshold_db / 20.0)
    silent_frames = sum(1 for value in frame_rms_values if value <= silence_threshold_amp)
    silence_ratio = silent_frames / len(frame_rms_values)

    mean_frame_rms = statistics.fmean(frame_rms_values)
    if mean_frame_rms <= 1e-9:
        envelope_cv = 0.0
    else:
        envelope_cv = statistics.pstdev(frame_rms_values) / mean_frame_rms

    selected = (
        overall_rms_db <= max_rms_db
        and silence_ratio >= min_silence_ratio
        and envelope_cv <= max_envelope_cv
    )

    return AudioMetrics(
        file_path=file_path,
        duration_s=(len(samples) / sample_rate) if sample_rate > 0 else 0.0,
        overall_rms_db=overall_rms_db,
        silence_ratio=silence_ratio,
        envelope_cv=envelope_cv,
        selected=selected,
    )


def write_report(report_path: pathlib.Path, metrics_list: list[AudioMetrics]) -> None:
    report_path.parent.mkdir(parents=True, exist_ok=True)
    with report_path.open("w", encoding="utf-8", newline="") as csv_file:
        writer = csv.writer(csv_file)
        writer.writerow(
            [
                "file_name",
                "file_path",
                "duration_s",
                "overall_rms_db",
                "silence_ratio",
                "envelope_cv",
                "selected",
            ]
        )
        for metrics in metrics_list:
            writer.writerow(
                [
                    metrics.file_path.name,
                    str(metrics.file_path),
                    f"{metrics.duration_s:.3f}",
                    f"{metrics.overall_rms_db:.3f}",
                    f"{metrics.silence_ratio:.3f}",
                    f"{metrics.envelope_cv:.3f}",
                    "true" if metrics.selected else "false",
                ]
            )


def write_selected_list(selected_path: pathlib.Path, metrics_list: list[AudioMetrics]) -> list[pathlib.Path]:
    selected_files = [metrics.file_path for metrics in metrics_list if metrics.selected]
    selected_path.parent.mkdir(parents=True, exist_ok=True)
    selected_path.write_text(
        "\n".join(str(path) for path in selected_files) + ("\n" if selected_files else ""),
        encoding="utf-8",
    )
    return selected_files


def copy_selected_files(selected_files: list[pathlib.Path], copy_selected_to: pathlib.Path) -> None:
    copy_selected_to.mkdir(parents=True, exist_ok=True)
    for file_path in selected_files:
        shutil.copy2(file_path, copy_selected_to / file_path.name)


def main() -> int:
    args = parse_args()
    source_dir = pathlib.Path(args.source_dir).resolve()
    if not source_dir.is_dir():
        raise SystemExit(f"source directory does not exist: {source_dir}")

    report_path = pathlib.Path(args.report_path).resolve() if args.report_path else source_dir / "low_activity_report.csv"
    selected_path = pathlib.Path(args.selected_path).resolve() if args.selected_path else source_dir / "low_activity_selected.txt"
    copy_selected_to = pathlib.Path(args.copy_selected_to).resolve() if args.copy_selected_to else None

    audio_files = iter_audio_files(source_dir, args.recursive)
    if not audio_files:
        raise SystemExit(f"no wav files found under: {source_dir}")

    metrics_list = [
        analyze_file(
            file_path=file_path,
            sample_rate=args.sample_rate,
            frame_ms=args.frame_ms,
            silence_threshold_db=args.silence_threshold_db,
            max_rms_db=args.max_rms_db,
            min_silence_ratio=args.min_silence_ratio,
            max_envelope_cv=args.max_envelope_cv,
        )
        for file_path in audio_files
    ]

    write_report(report_path, metrics_list)
    selected_files = write_selected_list(selected_path, metrics_list)
    if copy_selected_to is not None:
        copy_selected_files(selected_files, copy_selected_to)

    print(f"analyzed={len(metrics_list)} selected={len(selected_files)}")
    print(f"report={report_path}")
    print(f"selected={selected_path}")
    if copy_selected_to is not None:
        print(f"copied_to={copy_selected_to}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
