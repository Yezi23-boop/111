import unittest

from tests.main_paths import PRINTF_ESP32_SOURCE


class PrintfEsp32SourceTests(unittest.TestCase):
    def test_esp32s3_static_iram_total_has_target_specific_fallback(self) -> None:
        source = PRINTF_ESP32_SOURCE.read_text(encoding="utf-8")

        self.assertIn("printf_esp32_get_static_iram_total", source)
        self.assertIn("CONFIG_IDF_TARGET_ESP32S3", source)
        self.assertIn("CONFIG_ESP32S3_INSTRUCTION_CACHE_SIZE", source)
        self.assertIn("SOC_DIRAM_IRAM_LOW - SOC_DIRAM_DRAM_LOW", source)
        self.assertIn("0x403CB700U", source)
        self.assertIn("size_t iram_text_total = printf_esp32_get_static_iram_total();", source)

        fallback_section = source.split('if (iram_text_total > 0)')[1]
        self.assertIn('total: unknown', fallback_section)


if __name__ == "__main__":
    unittest.main()
