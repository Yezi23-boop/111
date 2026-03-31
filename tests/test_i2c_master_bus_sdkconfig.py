import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
SDKCONFIG = REPO_ROOT / "sdkconfig"


class I2CMasterBusSdkconfigTests(unittest.TestCase):
    def test_codec_i2c_backward_compatibility_is_disabled(self) -> None:
        sdkconfig = SDKCONFIG.read_text(encoding="utf-8")
        self.assertNotIn("CONFIG_CODEC_I2C_BACKWARD_COMPATIBLE=y", sdkconfig)
        self.assertIn("# CONFIG_CODEC_I2C_BACKWARD_COMPATIBLE is not set", sdkconfig)


if __name__ == "__main__":
    unittest.main()
