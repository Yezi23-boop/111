from __future__ import annotations

import argparse
import array
import csv
import math
import pathlib
import subprocess
import sys
import wave
from dataclasses import dataclass


DEFAULT_SAMPLE_RATE = 16000
DEFAULT_FRAME_MS = 20
DEFAULT_HOP_MS = 10
DEFAULT_CLIP_MS = 1000


@dataclass
class FrameFeature:
    start_sample: int
    rms: float
    zcr: float


@dataclass
class HornEvent:
    peak_sample: int
    peak_rms: float
    peak_zcr: float
    start_sample: int
    end_sample: int


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Extract horn-like 1 second segments from wav files."
    )
    parser.add_argument("--source-dir", required=True, help="Directory containing wav files.")
    parser.add_argument(
        "--output-dir",
        help="Directory for extracted segments. Defaults to <source-dir>/_horn_like_segments",
    )
    parser.add_argument(
        "--report-path",
        help="CSV report path. Defaults to <source-dir>/horn_like_segments_report.csv",
    )
    parser.add_argument(
        "--recursive",
        action="store_true",
        help="Recursively scan wav files under the source directory.",
    )
    parser.add_argument("--sample-rate", type=int, default=DEFAULT_SAMPLE_RATE)
    parser.add_argument("--frame-ms", type=int, default=DEFAULT_FRAME_MS)
    parser.add_argument("--hop-ms", type=int, default=DEFAULT_HOP_MS)
    parser.add_argument("--clip-ms", type=int, default=DEFAULT_CLIP_MS)
    parser.add_argument(
        "--prominence-factor",
        type=float,
        default=3.0,
        help="Frame RMS must exceed noise floor * this factor.",
    )
    parser.add_argument(
        "--min-peak-rms-db",
        type=float,
        default=-28.0,
        help="Minimum RMS threshold for horn-like peaks.",
    )
    parser.add_argument(
        "--min-zcr",
        type=float,
        default=0.015,
        help="Minimum zero-crossing rate for candidate frames.",
    )
    parser.add_argument(
        "--max-zcr",
        type=float,
        default=0.18,
        help="Maximum zero-crossing rate for candidate frames.",
    )
    parser.add_argument(
        "--min-event-ms",
        type=int,
        default=80,
        help="Minimum event duration in milliseconds.",
    )
    parser.add_argument(
        "--max-event-ms",
        type=int,
        default=700,
        help="Maximum event duration in milliseconds.",
    )
    parser.add_argument(
        "--merge-gap-ms",
        type=int,
        default=40,
        help="Allowed inactive gap when merging neighboring active frames.",
    )
    parser.add_argument(
        "--min-peak-gap-ms",
        type=int,
        default=500,
        help="Minimum spacing between exported peaks.",
    )
    return parser.parse_args()


def iter_audio_files(source_dir: pathlib.Path, recursive: bool) -> list[pathlib.Path]:
    pattern = "**/*.wav" if recursive else "*.wav"
    return sorted(path for path in source_dir.glob(pattern) if path.is_file())


def decode_audio_samples(file_path: pathlib.Path, sample_rate: int) -> list[int]:
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
    return list(pcm)


def amplitude_to_db(amplitude: float) -> float:
    if amplitude <= 0.0:
        return -120.0
    return 20.0 * math.log10(amplitude)


def compute_frame_features(samples: list[int], frame_size: int, hop_size: int) -> list[FrameFeature]:
    if len(samples) < frame_size:
        samples = samples + [0] * (frame_size - len(samples))

    features: list[FrameFeature] = []
    max_start = max(0, len(samples) - frame_size)
    for start in range(0, max_start + 1, hop_size):
        frame = samples[start : start + frame_size]
        rms = math.sqrt(sum((sample / 32768.0) ** 2 for sample in frame) / len(frame))
        zero_crossings = 0
        for previous, current in zip(frame, frame[1:]):
            if (previous < 0 <= current) or (previous > 0 >= current):
                zero_crossings += 1
        zcr = zero_crossings / len(frame)
        features.append(FrameFeature(start_sample=start, rms=rms, zcr=zcr))
    return features


def compute_noise_floor(features: list[FrameFeature]) -> float:
    rms_values = sorted(feature.rms for feature in features)
    if not rms_values:
        return 0.0
    percentile_index = max(0, int(len(rms_values) * 0.30) - 1)
    return rms_values[percentile_index]


def frame_is_active(
    feature: FrameFeature,
    noise_floor: float,
    prominence_factor: float,
    min_peak_rms: float,
    min_zcr: float,
    max_zcr: float,
) -> bool:
    threshold = max(noise_floor * prominence_factor, min_peak_rms)
    return feature.rms >= threshold and min_zcr <= feature.zcr <= max_zcr


def detect_horn_events(
    features: list[FrameFeature],
    frame_size: int,
    hop_size: int,
    sample_rate: int,
    prominence_factor: float,
    min_peak_rms_db: float,
    min_zcr: float,
    max_zcr: float,
    min_event_ms: int,
    max_event_ms: int,
    merge_gap_ms: int,
    min_peak_gap_ms: int,
) -> list[HornEvent]:
    if not features:
        return []

    noise_floor = compute_noise_floor(features)
    min_peak_rms = 10.0 ** (min_peak_rms_db / 20.0)
    merge_gap_frames = max(0, merge_gap_ms // max(1, (hop_size * 1000 // sample_rate)))
    min_event_samples = max(1, sample_rate * min_event_ms // 1000)
    max_event_samples = max(min_event_samples, sample_rate * max_event_ms // 1000)
    min_peak_gap_samples = sample_rate * min_peak_gap_ms // 1000

    groups: list[list[FrameFeature]] = []
    current_group: list[FrameFeature] = []
    inactive_gap = 0

    for feature in features:
        active = frame_is_active(
            feature=feature,
            noise_floor=noise_floor,
            prominence_factor=prominence_factor,
            min_peak_rms=min_peak_rms,
            min_zcr=min_zcr,
            max_zcr=max_zcr,
        )
        if active:
            if not current_group:
                current_group = [feature]
                inactive_gap = 0
            else:
                current_group.append(feature)
                inactive_gap = 0
        elif current_group:
            inactive_gap += 1
            if inactive_gap <= merge_gap_frames:
                current_group.append(feature)
            else:
                groups.append(current_group[:-inactive_gap])
                current_group = []
                inactive_gap = 0

    if current_group:
        groups.append(current_group[:-inactive_gap] if inactive_gap else current_group)

    events: list[HornEvent] = []
    for group in groups:
        if not group:
            continue
        event_start = group[0].start_sample
        event_end = group[-1].start_sample + frame_size
        event_duration = event_end - event_start
        if event_duration < min_event_samples or event_duration > max_event_samples:
            continue

        peak_feature = max(group, key=lambda feature: feature.rms)
        events.append(
            HornEvent(
                peak_sample=peak_feature.start_sample + frame_size // 2,
                peak_rms=peak_feature.rms,
                peak_zcr=peak_feature.zcr,
                start_sample=event_start,
                end_sample=event_end,
            )
        )

    deduped_events: list[HornEvent] = []
    for event in sorted(events, key=lambda item: item.peak_sample):
        if not deduped_events:
            deduped_events.append(event)
            continue
        previous = deduped_events[-1]
        if event.peak_sample - previous.peak_sample < min_peak_gap_samples:
            if event.peak_rms > previous.peak_rms:
                deduped_events[-1] = event
        else:
            deduped_events.append(event)
    return deduped_events


def write_clip(output_path: pathlib.Path, clip_samples: list[int], sample_rate: int) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(output_path), "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        wav_file.writeframes(b"".join(sample.to_bytes(2, byteorder="little", signed=True) for sample in clip_samples))


def extract_clip(samples: list[int], peak_sample: int, sample_rate: int, clip_ms: int) -> tuple[list[int], int]:
    clip_samples_count = max(1, sample_rate * clip_ms // 1000)
    total_samples = len(samples)
    if total_samples >= clip_samples_count:
        start_sample = max(0, min(peak_sample - clip_samples_count // 2, total_samples - clip_samples_count))
        clip_samples = samples[start_sample : start_sample + clip_samples_count]
        return clip_samples, start_sample

    padded = list(samples) + [0] * (clip_samples_count - total_samples)
    return padded, 0


def process_file(
    file_path: pathlib.Path,
    output_dir: pathlib.Path,
    sample_rate: int,
    frame_ms: int,
    hop_ms: int,
    clip_ms: int,
    prominence_factor: float,
    min_peak_rms_db: float,
    min_zcr: float,
    max_zcr: float,
    min_event_ms: int,
    max_event_ms: int,
    merge_gap_ms: int,
    min_peak_gap_ms: int,
) -> list[dict[str, str]]:
    samples = decode_audio_samples(file_path, sample_rate)
    frame_size = max(1, sample_rate * frame_ms // 1000)
    hop_size = max(1, sample_rate * hop_ms // 1000)
    features = compute_frame_features(samples, frame_size, hop_size)
    events = detect_horn_events(
        features=features,
        frame_size=frame_size,
        hop_size=hop_size,
        sample_rate=sample_rate,
        prominence_factor=prominence_factor,
        min_peak_rms_db=min_peak_rms_db,
        min_zcr=min_zcr,
        max_zcr=max_zcr,
        min_event_ms=min_event_ms,
        max_event_ms=max_event_ms,
        merge_gap_ms=merge_gap_ms,
        min_peak_gap_ms=min_peak_gap_ms,
    )

    rows: list[dict[str, str]] = []
    for clip_index, event in enumerate(events, start=1):
        clip_samples, clip_start_sample = extract_clip(samples, event.peak_sample, sample_rate, clip_ms)
        output_name = f"{file_path.stem}.horn.{clip_index:03d}.wav"
        output_path = output_dir / output_name
        write_clip(output_path, clip_samples, sample_rate)
        rows.append(
            {
                "source_file": file_path.name,
                "source_path": str(file_path),
                "clip_file": output_name,
                "clip_path": str(output_path),
                "peak_time_s": f"{event.peak_sample / sample_rate:.3f}",
                "clip_start_s": f"{clip_start_sample / sample_rate:.3f}",
                "clip_end_s": f"{(clip_start_sample + len(clip_samples)) / sample_rate:.3f}",
                "event_start_s": f"{event.start_sample / sample_rate:.3f}",
                "event_end_s": f"{event.end_sample / sample_rate:.3f}",
                "peak_rms_db": f"{amplitude_to_db(event.peak_rms):.3f}",
                "peak_zcr": f"{event.peak_zcr:.3f}",
            }
        )
    return rows


def write_report(report_path: pathlib.Path, rows: list[dict[str, str]]) -> None:
    report_path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "source_file",
        "source_path",
        "clip_file",
        "clip_path",
        "peak_time_s",
        "clip_start_s",
        "clip_end_s",
        "event_start_s",
        "event_end_s",
        "peak_rms_db",
        "peak_zcr",
    ]
    with report_path.open("w", encoding="utf-8", newline="") as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    args = parse_args()
    source_dir = pathlib.Path(args.source_dir).resolve()
    if not source_dir.is_dir():
        raise SystemExit(f"source directory does not exist: {source_dir}")

    output_dir = pathlib.Path(args.output_dir).resolve() if args.output_dir else source_dir / "_horn_like_segments"
    report_path = pathlib.Path(args.report_path).resolve() if args.report_path else source_dir / "horn_like_segments_report.csv"
    audio_files = iter_audio_files(source_dir, args.recursive)
    if not audio_files:
        raise SystemExit(f"no wav files found under: {source_dir}")

    all_rows: list[dict[str, str]] = []
    for file_path in audio_files:
        all_rows.extend(
            process_file(
                file_path=file_path,
                output_dir=output_dir,
                sample_rate=args.sample_rate,
                frame_ms=args.frame_ms,
                hop_ms=args.hop_ms,
                clip_ms=args.clip_ms,
                prominence_factor=args.prominence_factor,
                min_peak_rms_db=args.min_peak_rms_db,
                min_zcr=args.min_zcr,
                max_zcr=args.max_zcr,
                min_event_ms=args.min_event_ms,
                max_event_ms=args.max_event_ms,
                merge_gap_ms=args.merge_gap_ms,
                min_peak_gap_ms=args.min_peak_gap_ms,
            )
        )

    write_report(report_path, all_rows)
    print(f"analyzed={len(audio_files)} clips={len(all_rows)}")
    print(f"output_dir={output_dir}")
    print(f"report={report_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
