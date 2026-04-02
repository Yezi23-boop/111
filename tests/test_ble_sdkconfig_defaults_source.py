import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
SDKCONFIG_DEFAULTS = REPO_ROOT / "sdkconfig.defaults"


class BleSdkconfigDefaultsSourceTests(unittest.TestCase):
    def test_sdkconfig_defaults_contains_minimum_ble_flags(self) -> None:
        source = SDKCONFIG_DEFAULTS.read_text(encoding="utf-8")

        self.assertIn("CONFIG_BT_ENABLED=y", source)
        self.assertIn("CONFIG_BT_NIMBLE_ENABLED=y", source)
        self.assertNotIn("CONFIG_BT_BLUEDROID_ENABLED=y", source)


if __name__ == "__main__":
    unittest.main()
