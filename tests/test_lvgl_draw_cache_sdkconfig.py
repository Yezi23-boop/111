import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
SDKCONFIG = REPO_ROOT / "sdkconfig"


class LvglDrawCacheSdkconfigTests(unittest.TestCase):
    def test_refr_period_and_draw_caches_match_60fps_profile(self) -> None:
        source = SDKCONFIG.read_text(encoding="utf-8")

        self.assertIn("CONFIG_LV_DEF_REFR_PERIOD=16", source)
        self.assertIn("CONFIG_LV_DRAW_SW_CIRCLE_CACHE_SIZE=4", source)
        self.assertIn("CONFIG_LV_DRAW_SW_SHADOW_CACHE_SIZE=4", source)


if __name__ == "__main__":
    unittest.main()
