import unittest

from tests.main_paths import APP_MAIN_SOURCE
from tests.main_paths import MAIN_DIR
from tests.main_paths import PCF85063ATL_CMAKE
from tests.main_paths import PCF85063ATL_HEADER
from tests.main_paths import PCF85063ATL_SOURCE
from tests.main_paths import WAKEUP_EVIDENCE_SERVICE_HEADER
from tests.main_paths import WAKEUP_EVIDENCE_SERVICE_SOURCE


MAIN_CMAKE = MAIN_DIR / "CMakeLists.txt"


class WakeupEvidenceSourceTests(unittest.TestCase):
    def test_pcf85063atl_component_is_minimal_shared_i2c_driver(self) -> None:
        self.assertTrue(PCF85063ATL_CMAKE.exists())
        self.assertTrue(PCF85063ATL_HEADER.exists())
        self.assertTrue(PCF85063ATL_SOURCE.exists())

        cmake = PCF85063ATL_CMAKE.read_text(encoding="utf-8")
        header = PCF85063ATL_HEADER.read_text(encoding="utf-8")
        source = PCF85063ATL_SOURCE.read_text(encoding="utf-8")

        self.assertIn("REQUIRES i2c_manager", cmake)
        self.assertIn("PCF85063ATL", header)
        self.assertIn("pcf85063atl_probe", header)
        self.assertIn("pcf85063atl_arm_countdown_timer", header)
        self.assertIn("pcf85063atl_stop_countdown_timer", header)
        self.assertIn("PCF85063ATL_I2C_ADDR_7BIT 0x51", source)
        self.assertIn("PCF85063ATL_REG_TIMER_VALUE 0x10", source)
        self.assertIn("PCF85063ATL_REG_TIMER_MODE 0x11", source)
        self.assertIn("PCF85063ATL_TIMER_MODE_TCF_1HZ", source)
        self.assertIn("PCF85063ATL_TIMER_MODE_TIE", source)
        self.assertIn("pcf85063atl_stop_countdown_timer", source)
        self.assertNotIn("esp_light_sleep_start", source)
        self.assertNotIn("esp_deep_sleep_start", source)

    def test_wakeup_evidence_service_observes_rtc_int_and_axp_irq_only(self) -> None:
        self.assertTrue(WAKEUP_EVIDENCE_SERVICE_HEADER.exists())
        self.assertTrue(WAKEUP_EVIDENCE_SERVICE_SOURCE.exists())

        header = WAKEUP_EVIDENCE_SERVICE_HEADER.read_text(encoding="utf-8")
        source = WAKEUP_EVIDENCE_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("wakeup_evidence_service_start", header)
        self.assertNotIn("wakeup_evidence_sleep_test_result_t", header)
        self.assertNotIn("wakeup_evidence_service_get_sleep_test_result", header)
        self.assertIn("GPIO_NUM_39", source)
        self.assertIn("GPIO_PULLUP_ENABLE", source)
        self.assertIn("pcf85063atl_arm_countdown_timer", source)
        self.assertIn("rtc_int_sample", source)
        self.assertIn('ESP_LOGD(TAG, "rtc_int_sample:', source)
        self.assertIn("rtc_timer_flag_observed", source)
        self.assertIn("rtc_timer_flag_cleared", source)
        self.assertIn("rtc_timer_stopped_after_evidence", source)
        self.assertIn("pcf85063atl_stop_countdown_timer", source)
        self.assertIn("runtime_evidence_only", source)
        self.assertIn("s_rtc_runtime_evidence_ready", source)
        self.assertLess(
            source.index("s_rtc_runtime_evidence_ready = true"),
            source.index("wakeup_evidence_stop_runtime_timer_after_evidence()"),
        )
        self.assertLess(
            source.index("wakeup_evidence_stop_runtime_timer_after_evidence()"),
            source.index("runtime_evidence_only"),
        )
        self.assertNotIn("gpio_wakeup_enable", source)
        self.assertNotIn("esp_sleep_enable_gpio_wakeup", source)
        self.assertNotIn("esp_sleep_enable_timer_wakeup", source)
        self.assertNotIn("esp_light_sleep_start", source)
        self.assertNotIn("esp_sleep_get_wakeup_cause", source)
        self.assertIn("axp2101_read_irq_status", source)
        self.assertIn("axp2101_clear_irq_status", source)
        self.assertNotIn("GPIO_MODE_OUTPUT", source)
        self.assertNotIn("esp_deep_sleep_start", source)

    def test_main_build_and_startup_wire_the_evidence_service(self) -> None:
        cmake = MAIN_CMAKE.read_text(encoding="utf-8")
        app_main = APP_MAIN_SOURCE.read_text(encoding="utf-8")

        self.assertIn("${CMAKE_CURRENT_LIST_DIR}/services/power/wakeup_evidence_service.c", cmake)
        self.assertRegex(cmake, r"\bREQUIRES\b[\s\S]*?\bpcf85063atl\b")
        self.assertIn('#include "services/power/wakeup_evidence_service.h"', app_main)
        self.assertIn("wakeup_evidence_service_start()", app_main)
        self.assertLess(
            app_main.index("power_policy_start()"),
            app_main.index("wakeup_evidence_service_start()"),
        )


if __name__ == "__main__":
    unittest.main()
