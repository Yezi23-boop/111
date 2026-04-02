import csv
import math
import pathlib
import subprocess
import sys
import tempfile
import unittest
import wave


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT_PATH = REPO_ROOT / "scripts" / "analyze_low_activity_audio.py"


def _write_wave_file(path: pathlib.Path, samples: list[int], sample_rate: int = 16000) -> None:
    with wave.open(str(path), "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        wav_file.writeframes(b"".join(sample.to_bytes(2, byteorder="little", signed=True) for sample in samples))


def _generate_sine_samples(
    duration_s: float,
    amplitude: float,
    frequency_hz: float = 440.0,
    sample_rate: int = 16000,
) -> list[int]:
    total_samples = int(duration_s * sample_rate)
    return [
        int(32767 * amplitude * math.sin(2.0 * math.pi * frequency_hz * index / sample_rate))
        for index in range(total_samples)
    ]


def _generate_dynamic_samples(sample_rate: int = 16000) -> list[int]:
    samples: list[int] = []
    segment_length = int(0.3 * sample_rate)
    for segment_index in range(10):
        if segment_index % 2 == 0:
            samples.extend([0] * segment_length)
        else:
            samples.extend(_generate_sine_samples(0.3, 0.45, 880.0, sample_rate))
    return samples


class LowActivityAudioScriptTests(unittest.TestCase):
    def test_script_selects_quiet_and_low_variation_audio(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_str:
            temp_dir = pathlib.Path(temp_dir_str)
            source_dir = temp_dir / "audio"
            source_dir.mkdir()

            _write_wave_file(source_dir / "mostly_silent.wav", [0] * (16000 * 3))
            _write_wave_file(source_dir / "steady_low.wav", _generate_sine_samples(3.0, 0.01))
            _write_wave_file(source_dir / "dynamic.wav", _generate_dynamic_samples())
            _write_wave_file(source_dir / "loud_flat.wav", _generate_sine_samples(3.0, 0.35))

            report_path = temp_dir / "report.csv"
            selected_path = temp_dir / "selected.txt"

            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT_PATH),
                    "--source-dir",
                    str(source_dir),
                    "--report-path",
                    str(report_path),
                    "--selected-path",
                    str(selected_path),
                ],
                capture_output=True,
                text=True,
                check=False,
            )

            if result.returncode != 0:
                self.fail(
                    f"script exited with {result.returncode}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
                )

            selected_files = {
                pathlib.Path(line.strip()).name
                for line in selected_path.read_text(encoding="utf-8").splitlines()
                if line.strip()
            }
            self.assertIn("mostly_silent.wav", selected_files)
            self.assertIn("steady_low.wav", selected_files)
            self.assertNotIn("dynamic.wav", selected_files)
            self.assertNotIn("loud_flat.wav", selected_files)

            with report_path.open("r", encoding="utf-8", newline="") as csv_file:
                rows = list(csv.DictReader(csv_file))

            self.assertEqual(4, len(rows))
            self.assertTrue(any(row["file_name"] == "mostly_silent.wav" and row["selected"] == "true" for row in rows))
            self.assertTrue(any(row["file_name"] == "dynamic.wav" and row["selected"] == "false" for row in rows))


if __name__ == "__main__":
    unittest.main()
