import csv
import math
import pathlib
import subprocess
import sys
import tempfile
import unittest
import wave


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT_PATH = REPO_ROOT / "scripts" / "extract_horn_like_segments.py"
SAMPLE_RATE = 16000


def _write_wave_file(path: pathlib.Path, samples: list[int], sample_rate: int = SAMPLE_RATE) -> None:
    with wave.open(str(path), "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        wav_file.writeframes(b"".join(sample.to_bytes(2, byteorder="little", signed=True) for sample in samples))


def _generate_tone(duration_s: float, amplitude: float, frequency_hz: float) -> list[float]:
    total_samples = int(duration_s * SAMPLE_RATE)
    return [
        amplitude * math.sin(2.0 * math.pi * frequency_hz * index / SAMPLE_RATE)
        for index in range(total_samples)
    ]


def _generate_horn_burst(duration_s: float = 0.25, amplitude: float = 0.55) -> list[int]:
    tone_a = _generate_tone(duration_s, amplitude, 440.0)
    tone_b = _generate_tone(duration_s, amplitude * 0.75, 660.0)
    total_samples = len(tone_a)
    samples: list[int] = []
    for index in range(total_samples):
        envelope = math.sin(math.pi * index / max(1, total_samples - 1))
        sample = (tone_a[index] + tone_b[index]) * 0.5 * envelope
        samples.append(int(max(-1.0, min(1.0, sample)) * 32767))
    return samples


def _mix_segment(samples: list[int], offset_s: float, segment: list[int]) -> None:
    offset = int(offset_s * SAMPLE_RATE)
    for index, value in enumerate(segment):
        target = offset + index
        if 0 <= target < len(samples):
            mixed = samples[target] + value
            samples[target] = max(-32768, min(32767, mixed))


class ExtractHornLikeSegmentsScriptTests(unittest.TestCase):
    def test_script_extracts_multiple_one_second_segments_from_one_source(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_str:
            temp_dir = pathlib.Path(temp_dir_str)
            source_dir = temp_dir / "audio"
            source_dir.mkdir()

            horn_source = source_dir / "horn_source.wav"
            silence_source = source_dir / "silence.wav"

            horn_samples = [0] * (SAMPLE_RATE * 4)
            _mix_segment(horn_samples, 0.8, _generate_horn_burst())
            _mix_segment(horn_samples, 2.5, _generate_horn_burst())
            _write_wave_file(horn_source, horn_samples)
            _write_wave_file(silence_source, [0] * (SAMPLE_RATE * 4))

            output_dir = temp_dir / "segments"
            report_path = temp_dir / "segments.csv"

            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT_PATH),
                    "--source-dir",
                    str(source_dir),
                    "--output-dir",
                    str(output_dir),
                    "--report-path",
                    str(report_path),
                ],
                capture_output=True,
                text=True,
                check=False,
            )

            if result.returncode != 0:
                self.fail(
                    f"script exited with {result.returncode}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
                )

            extracted_files = sorted(output_dir.glob("*.wav"))
            self.assertEqual(2, len(extracted_files))
            self.assertTrue(all(path.name.startswith("horn_source.horn.") for path in extracted_files))

            for extracted_file in extracted_files:
                with wave.open(str(extracted_file), "rb") as wav_file:
                    self.assertEqual(SAMPLE_RATE, wav_file.getframerate())
                    self.assertEqual(SAMPLE_RATE, wav_file.getnframes())

            with report_path.open("r", encoding="utf-8", newline="") as csv_file:
                rows = list(csv.DictReader(csv_file))

            self.assertEqual(2, len(rows))
            self.assertTrue(all(row["source_file"] == "horn_source.wav" for row in rows))


if __name__ == "__main__":
    unittest.main()
