import unittest

from tests.main_cmake_contract import assert_main_source_globbed
from tests.main_paths import APP_MAIN_SOURCE
from tests.main_paths import GET_TIME_DIR
from tests.main_paths import MAIN_DIR
from tests.main_paths import NETWORK_SERVICE_SOURCE
from tests.main_paths import OFFICIAL_CHAT_C_API_SOURCE
from tests.main_paths import OFFICIAL_CHAT_HEADER
from tests.main_paths import OFFICIAL_CHAT_OTA_SOURCE
from tests.main_paths import OFFICIAL_CHAT_SERVICE_SOURCE
from tests.main_paths import PCF85063ATL_HEADER
from tests.main_paths import PCF85063ATL_SOURCE
from tests.main_paths import SYSTEM_TIME_CMAKE
from tests.main_paths import SYSTEM_TIME_HEADER
from tests.main_paths import SYSTEM_TIME_SERVICE_HEADER
from tests.main_paths import SYSTEM_TIME_SERVICE_SOURCE
from tests.main_paths import SYSTEM_TIME_SOURCE
from tests.main_paths import WEATHER_SERVICE_SOURCE
from tests.main_paths import UI_GENERATED_DIR


MAIN_CMAKE = MAIN_DIR / "CMakeLists.txt"
UI_WIDGETS_INIT_SOURCE = UI_GENERATED_DIR / "widgets_init.c"
UI_SCREEN_MAIN_SOURCE = UI_GENERATED_DIR / "setup_scr_screen_main.c"


class SystemTimeOwnerSourceTests(unittest.TestCase):
    def test_pcf85063atl_exposes_time_write_and_status_apis(self) -> None:
        header = PCF85063ATL_HEADER.read_text(encoding="utf-8")
        source = PCF85063ATL_SOURCE.read_text(encoding="utf-8")

        self.assertIn("pcf85063atl_status_t", header)
        self.assertIn("pcf85063atl_set_time", header)
        self.assertIn("pcf85063atl_clear_oscillator_stopped", header)
        self.assertIn("pcf85063atl_read_status", header)
        self.assertIn("pcf85063atl_dec_to_bcd", source)
        self.assertIn("pcf85063atl_write_bytes(PCF85063ATL_REG_SECONDS", source)
        self.assertIn("~PCF85063ATL_SECONDS_OS", source)
        self.assertIn("status->oscillator_stopped", source)
        self.assertIn("status->alarm_flag", source)
        self.assertIn("status->timer_flag", source)

    def test_system_time_component_is_the_only_sntp_owner(self) -> None:
        self.assertTrue(SYSTEM_TIME_CMAKE.exists())
        self.assertTrue(SYSTEM_TIME_HEADER.exists())
        self.assertTrue(SYSTEM_TIME_SOURCE.exists())

        cmake = SYSTEM_TIME_CMAKE.read_text(encoding="utf-8")
        header = SYSTEM_TIME_HEADER.read_text(encoding="utf-8")
        source = SYSTEM_TIME_SOURCE.read_text(encoding="utf-8")
        official_ota = OFFICIAL_CHAT_OTA_SOURCE.read_text(encoding="utf-8")

        self.assertIn("REQUIRES pcf85063atl lwip", cmake)
        self.assertIn("system_time_sync_sntp_and_write_rtc", header)
        self.assertIn("system_time_ensure_valid_for_tls", header)
        self.assertIn("system_time_apply_unix_time", header)
        self.assertIn("system_time_get_local_time", header)
        self.assertIn("system_time_source_text", header)
        self.assertIn("esp_sntp_init", source)
        self.assertIn("esp_sntp_restart", source)
        self.assertIn("pcf85063atl_set_time", source)
        self.assertIn("settimeofday", source)
        self.assertIn("system_time_sync: source=%s rtc_writeback=%d drift_sec=%lld drift_known=%d", source)
        self.assertNotIn("esp_sntp_init", official_ota)
        self.assertNotIn("esp_sntp_restart", official_ota)
        self.assertNotIn("settimeofday", official_ota)

    def test_system_time_service_wires_startup_and_network_ready(self) -> None:
        self.assertTrue(SYSTEM_TIME_SERVICE_HEADER.exists())
        self.assertTrue(SYSTEM_TIME_SERVICE_SOURCE.exists())

        service_header = SYSTEM_TIME_SERVICE_HEADER.read_text(encoding="utf-8")
        service_source = SYSTEM_TIME_SERVICE_SOURCE.read_text(encoding="utf-8")
        app_main = APP_MAIN_SOURCE.read_text(encoding="utf-8")
        network_service = NETWORK_SERVICE_SOURCE.read_text(encoding="utf-8")
        main_cmake = MAIN_CMAKE.read_text(encoding="utf-8")

        self.assertIn("system_time_service_start", service_header)
        self.assertIn("system_time_service_note_network_ready", service_header)
        self.assertIn("system_time_service_ensure_valid_for_tls", service_header)
        self.assertIn("system_time_service_apply_server_time", service_header)
        self.assertIn("system_time_bootstrap_from_rtc", service_source)
        self.assertIn("system_time_sync_sntp_and_write_rtc", service_source)
        self.assertIn("system_time_service_log_boot_summary", service_source)
        self.assertIn("system_time_boot: rtc_present=%d os=%d source=%s sys_valid=%d rtc=%s reason=%s", service_source)
        self.assertIn('#include "services/time/system_time_service.h"', app_main)
        self.assertIn("system_time_service_start()", app_main)
        self.assertIn('#include "services/time/system_time_service.h"', network_service)
        self.assertIn("system_time_service_note_network_ready()", network_service)
        assert_main_source_globbed(self, "services/time/system_time_service.c")
        self.assertRegex(main_cmake, r"\bREQUIRES\b[\s\S]*?\bsystem_time\b")
        self.assertNotRegex(main_cmake, r"\bREQUIRES\b[\s\S]*?\bget_time\b")

    def test_network_time_sync_stack_is_internal_for_rtc_writeback(self) -> None:
        service_source = SYSTEM_TIME_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("system_time_sync_sntp_and_write_rtc", service_source)
        self.assertIn("MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT", service_source)
        self.assertNotIn("&s_sync_task_handle, MALLOC_CAP_SPIRAM", service_source)

    def test_official_chat_requests_time_via_callback(self) -> None:
        header = OFFICIAL_CHAT_HEADER.read_text(encoding="utf-8")
        ota_source = OFFICIAL_CHAT_OTA_SOURCE.read_text(encoding="utf-8")
        c_api = OFFICIAL_CHAT_C_API_SOURCE.read_text(encoding="utf-8")
        service_source = OFFICIAL_CHAT_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("official_chat_ensure_time_cb_t", header)
        self.assertIn("official_chat_apply_server_time_cb_t", header)
        self.assertIn("ensure_time_valid", header)
        self.assertIn("apply_server_time", header)
        self.assertIn("EnsureSystemTimeValidForTls(url, ensure_time_valid_", ota_source)
        self.assertIn("apply_server_time_(unix_seconds", ota_source)
        self.assertIn("server_time.timestamp 是 Unix epoch 毫秒", ota_source)
        self.assertNotIn("timezone_offset->valueint", ota_source)
        self.assertNotIn("milliseconds += timezone_offset", ota_source)
        self.assertIn(".ensure_time_valid = nullptr", c_api)
        self.assertIn(".apply_server_time = nullptr", c_api)
        self.assertIn(".ensure_time_valid = official_chat_service_ensure_time_valid", service_source)
        self.assertIn(".apply_server_time = official_chat_service_apply_server_time", service_source)
        self.assertIn("system_time_service_ensure_valid_for_tls", service_source)
        self.assertIn("system_time_service_apply_server_time", service_source)

    def test_get_time_component_is_removed_and_weather_does_not_reown_time(self) -> None:
        self.assertFalse((GET_TIME_DIR / "CMakeLists.txt").exists())
        self.assertFalse((GET_TIME_DIR / "get_time.c").exists())
        self.assertFalse((GET_TIME_DIR / "get_time.h").exists())

        weather_service = WEATHER_SERVICE_SOURCE.read_text(encoding="utf-8")
        self.assertNotIn('#include "get_time.h"', weather_service)
        self.assertNotIn("esp_wait_sntp_sync", weather_service)
        self.assertNotIn("update_now_time", weather_service)
        self.assertNotIn("now_time", weather_service)
        self.assertNotIn("system_time_service", weather_service)

    def test_main_screen_clock_reads_system_time_owner(self) -> None:
        widgets = UI_WIDGETS_INIT_SOURCE.read_text(encoding="utf-8")
        screen_main = UI_SCREEN_MAIN_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "system_time.h"', widgets)
        self.assertIn("system_time_get_local_time(&now)", widgets)
        self.assertIn('lv_label_set_text(ui->screen_main_digital_clock_1, "--:--")', screen_main)
        self.assertIn("screen_main_digital_clock_1_timer(NULL)", screen_main)

        timer_body = widgets.split("void screen_main_digital_clock_1_timer", 1)[1]
        self.assertNotIn("clock_count(&screen_main_digital_clock_1_hour_value", timer_body)
        self.assertIn('lv_label_set_text_fmt(guider_ui.screen_main_digital_clock_1, "%02d:%02d"', timer_body)


if __name__ == "__main__":
    unittest.main()
