import unittest

from tests.main_paths import REPO_ROOT


AXP2101_DIR = REPO_ROOT / "components" / "axp2101"
AXP2101_CMAKE = AXP2101_DIR / "CMakeLists.txt"
AXP2101_HEADER = AXP2101_DIR / "include" / "axp2101.h"
AXP2101_SOURCE = AXP2101_DIR / "axp2101.c"
AXP2101_REGS = AXP2101_DIR / "axp2101_regs.h"


class Axp2101PowerSourceTests(unittest.TestCase):
    def test_component_cmake_registers_read_only_driver_and_shared_i2c(self) -> None:
        self.assertTrue(AXP2101_CMAKE.exists(), "components/axp2101/CMakeLists.txt should exist")
        cmake = AXP2101_CMAKE.read_text(encoding="utf-8")

        self.assertIn("idf_component_register(", cmake)
        self.assertIn('"axp2101.c"', cmake)
        self.assertIn("i2c_manager", cmake)

    def test_public_header_exposes_read_only_snapshot_api(self) -> None:
        self.assertTrue(AXP2101_HEADER.exists(), "components/axp2101/include/axp2101.h should exist")
        header = AXP2101_HEADER.read_text(encoding="utf-8")

        self.assertIn("axp2101_snapshot_t", header)
        self.assertIn("axp2101_irq_status_t", header)
        self.assertIn("bool vbus_good;", header)
        self.assertIn("bool battery_present;", header)
        self.assertIn("bool battfet_on;", header)
        self.assertIn("bool charging;", header)
        self.assertIn("bool discharging;", header)
        self.assertIn("uint16_t battery_mv;", header)
        self.assertIn("uint16_t vbus_mv;", header)
        self.assertIn("uint16_t vsys_mv;", header)
        self.assertIn("int8_t battery_percent;", header)
        self.assertIn("uint8_t irq0;", header)
        self.assertIn("uint8_t irq1;", header)
        self.assertIn("uint8_t irq2;", header)
        self.assertIn("esp_err_t axp2101_init(void);", header)
        self.assertIn("esp_err_t axp2101_probe(bool *present);", header)
        self.assertIn("esp_err_t axp2101_read_snapshot(axp2101_snapshot_t *snapshot);", header)
        self.assertIn("esp_err_t axp2101_read_irq_status(axp2101_irq_status_t *status);", header)
        self.assertIn("esp_err_t axp2101_clear_irq_status(const axp2101_irq_status_t *status);", header)

    def test_source_uses_shared_i2c_manager_contract(self) -> None:
        self.assertTrue(AXP2101_SOURCE.exists(), "components/axp2101/axp2101.c should exist")
        source = AXP2101_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "i2c_manager.h"', source)
        self.assertIn("i2c_manager_init", source)
        self.assertIn("i2c_manager_get_bus_handle", source)

    def test_regs_file_keeps_required_status_adc_and_irq_constants(self) -> None:
        self.assertTrue(AXP2101_REGS.exists(), "components/axp2101/axp2101_regs.h should exist")
        regs = AXP2101_REGS.read_text(encoding="utf-8")

        for symbol in (
            "AXP2101_REG_STATUS0",
            "AXP2101_REG_STATUS1",
            "AXP2101_REG_BATTERY_H",
            "AXP2101_REG_BATTERY_L",
            "AXP2101_REG_VBUS_H",
            "AXP2101_REG_VBUS_L",
            "AXP2101_REG_VSYS_H",
            "AXP2101_REG_VSYS_L",
            "AXP2101_REG_BAT_PERCENT",
            "AXP2101_REG_IRQ0",
            "AXP2101_REG_IRQ1",
            "AXP2101_REG_IRQ2",
        ):
            self.assertIn(symbol, regs)


if __name__ == "__main__":
    unittest.main()
