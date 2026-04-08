import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
SDKCONFIG_DEFAULTS = REPO_ROOT / "sdkconfig.defaults"


class BleSdkconfigDefaultsSourceTests(unittest.TestCase):
    def test_sdkconfig_defaults_contains_minimum_ble_flags(self) -> None:
        source = SDKCONFIG_DEFAULTS.read_text(encoding="utf-8")

        self.assertIn("CONFIG_BT_ENABLED=y", source)
        self.assertIn("CONFIG_BT_BLUEDROID_ENABLED=n", source)
        self.assertIn("CONFIG_BT_NIMBLE_ENABLED=y", source)
        self.assertNotIn("CONFIG_BT_BLUEDROID_ENABLED=y", source)

    def test_sdkconfig_defaults_keeps_ble_provisioning_in_minimal_peripheral_mode(
        self,
    ) -> None:
        source = SDKCONFIG_DEFAULTS.read_text(encoding="utf-8")

        self.assertIn("CONFIG_BT_NIMBLE_ROLE_CENTRAL=n", source)
        self.assertIn("CONFIG_BT_NIMBLE_ROLE_OBSERVER=n", source)
        self.assertIn("CONFIG_BT_NIMBLE_GATT_CLIENT=n", source)
        self.assertIn("CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT=n", source)
        self.assertIn("CONFIG_BT_NIMBLE_EXT_SCAN=n", source)
        self.assertIn("CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1", source)
        self.assertIn("CONFIG_BT_CTRL_BLE_MAX_ACT=2", source)

    def test_sdkconfig_defaults_enable_controller_support_for_connectable_adv(
        self,
    ) -> None:
        source = SDKCONFIG_DEFAULTS.read_text(encoding="utf-8")

        self.assertIn("CONFIG_BT_CTRL_BLE_SCAN=y", source)
        self.assertIn("CONFIG_BT_CTRL_BLE_MASTER=y", source)


if __name__ == "__main__":
    unittest.main()
