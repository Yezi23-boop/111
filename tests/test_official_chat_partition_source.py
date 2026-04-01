import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
PARTITIONS = REPO_ROOT / "partitions.csv"


def _parse_size(size_text: str) -> int:
    value = size_text.strip().upper()
    multipliers = {
        "K": 1024,
        "M": 1024 * 1024,
    }
    suffix = value[-1]
    if suffix in multipliers:
        return int(value[:-1], 0) * multipliers[suffix]
    return int(value, 0)


class OfficialChatPartitionSourceTests(unittest.TestCase):
    def test_partitions_include_model_and_assets_for_official_chat_runtime(self) -> None:
        source = PARTITIONS.read_text(encoding="utf-8")

        entries = {}
        current_offset = 0
        for raw_line in source.splitlines():
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue
            fields = [field.strip() for field in line.split(",")]
            if len(fields) < 5:
                continue
            size_value = _parse_size(fields[4])
            offset_value = int(fields[3], 0) if fields[3] else current_offset
            current_offset = offset_value + size_value
            entries[fields[0]] = {
                "fields": fields,
                "offset": offset_value,
            }

        self.assertIn("model", entries)
        self.assertIn("assets", entries)
        self.assertEqual("spiffs", entries["model"]["fields"][2])
        self.assertEqual("spiffs", entries["assets"]["fields"][2])
        self.assertGreaterEqual(_parse_size(entries["model"]["fields"][4]), 0x400000)
        self.assertGreaterEqual(_parse_size(entries["assets"]["fields"][4]), 0x200000)
        self.assertLess(entries["assets"]["offset"], 0x1000000)
        self.assertLess(entries["model"]["offset"], 0x1000000)


if __name__ == "__main__":
    unittest.main()
