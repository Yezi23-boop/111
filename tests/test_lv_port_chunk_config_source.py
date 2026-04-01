import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
LV_PORT_CONFIG = REPO_ROOT / "components" / "lvgl_port" / "lv_port_config.h"


class LvPortChunkConfigSourceTests(unittest.TestCase):
    def test_chunk_policy_uses_512_line_tiles_and_128_line_flush_chunks(self) -> None:
        lv_port_source = LV_PORT_CONFIG.read_text(encoding="utf-8")

        self.assertIn("#define LV_PORT_FIXED_CHUNK_LINES1 512", lv_port_source)
        self.assertIn("#define LV_PORT_FIXED_CHUNK_LINES2 512", lv_port_source)
        self.assertIn("#define LV_PORT_FIXED_CHUNK_LINES 128", lv_port_source)
        self.assertIn("#define LV_PORT_MAX_INFLIGHT_CHUNKS 2", lv_port_source)


if __name__ == "__main__":
    unittest.main()
