import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
PANEL_DEFAULTS = REPO_ROOT / "components" / "co5300_panel" / "co5300_panel_defaults.h"


class Co5300PanelDefaultsSourceTests(unittest.TestCase):
    def test_panel_defaults_match_migrated_legacy_profile(self) -> None:
        source = PANEL_DEFAULTS.read_text(encoding="utf-8")

        self.assertIn("#define CO5300_PANEL_MAX_TRANSFER_LINES 30", source)
        self.assertIn("#define CO5300_PANEL_USE_TE_SIGNAL 0", source)
        self.assertIn("#define CO5300_PANEL_OPTIMIZED_PCLK_HZ (80 * 1000 * 1000)", source)
        self.assertIn("#define CO5300_PANEL_OPTIMIZED_TRANS_QUEUE_DEPTH 8", source)


if __name__ == "__main__":
    unittest.main()
