import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
BLE_TRANSPORT_SOURCE = (
    REPO_ROOT
    / "components"
    / "wifi_provision"
    / "src"
    / "ble_server"
    / "ble_provision_transport.c"
)


class BleTransportSourceTests(unittest.TestCase):
    def test_ble_transport_rolls_back_on_init_failure(self) -> None:
        source = BLE_TRANSPORT_SOURCE.read_text(encoding="utf-8")

        self.assertIn("ble_provision_transport_reset_runtime_state", source)
        self.assertIn("nimble_port_deinit();", source)


if __name__ == "__main__":
    unittest.main()
