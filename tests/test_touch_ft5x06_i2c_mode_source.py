import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
TOUCH_SOURCE = REPO_ROOT / "components" / "touch_ft5x06" / "touch_ft5x06.c"


class TouchFt5x06I2CModeSourceTests(unittest.TestCase):
    def test_touch_driver_uses_master_bus_path_on_new_idf(self) -> None:
        source = TOUCH_SOURCE.read_text(encoding="utf-8")
        self.assertIn("i2c_manager_get_bus_handle()", source)
        self.assertIn("i2c_master_transmit_receive(", source)


if __name__ == "__main__":
    unittest.main()
