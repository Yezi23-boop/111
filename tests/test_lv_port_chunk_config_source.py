import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
LV_PORT_CONFIG = REPO_ROOT / "components" / "lvgl_port" / "lv_port_config.h"


class LvPortChunkConfigSourceTests(unittest.TestCase):
    def test_lv_port_fixed_chunk_lines_reduced_for_ai_experiment_memory_pressure(self) -> None:
        source = LV_PORT_CONFIG.read_text(encoding="utf-8")

        self.assertIn("#define LV_PORT_FIXED_CHUNK_LINES 10", source)
        self.assertNotIn("#define LV_PORT_FIXED_CHUNK_LINES 30", source)


if __name__ == "__main__":
    unittest.main()
