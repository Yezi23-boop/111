import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
PANEL_DEFAULTS = REPO_ROOT / "components" / "co5300_panel" / "co5300_panel_defaults.h"


class Co5300PanelDefaultsSourceTests(unittest.TestCase):
    def test_panel_defaults_respect_50mhz_hardware_ceiling(self) -> None:
        source = PANEL_DEFAULTS.read_text(encoding="utf-8")

        self.assertIn("#define CO5300_PANEL_MAX_TRANSFER_LINES 128", source)
        self.assertIn("#define CO5300_PANEL_OPTIMIZED_PCLK_HZ (50 * 1000 * 1000)", source)
        self.assertIn("#define CO5300_PANEL_OPTIMIZED_TRANS_QUEUE_DEPTH 8", source)


if __name__ == "__main__":
    unittest.main()
