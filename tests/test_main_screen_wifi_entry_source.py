import unittest

from tests.main_paths import UI_EVENTS_INIT_SOURCE
from tests.main_paths import UI_MAIN_DROPDOWN_CONTROLLER_HEADER
from tests.main_paths import UI_MAIN_DROPDOWN_CONTROLLER_SOURCE


class MainScreenWifiEntrySourceTests(unittest.TestCase):
    def test_events_init_binds_wifi_entry_handler(self) -> None:
        source = UI_EVENTS_INIT_SOURCE.read_text(encoding="utf-8")

        self.assertIn("screen_main_wifi_event_handler", source)
        self.assertIn(
            "lv_obj_add_event_cb(ui->screen_main_Wifi, screen_main_wifi_event_handler, LV_EVENT_ALL, ui);",
            source,
        )

    def test_dropdown_controller_exposes_wifi_click_entry(self) -> None:
        header = UI_MAIN_DROPDOWN_CONTROLLER_HEADER.read_text(encoding="utf-8")
        source = UI_MAIN_DROPDOWN_CONTROLLER_SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "void main_dropdown_controller_handle_wifi_click(void);", header
        )
        self.assertIn("network_manager_get_status(", source)
        self.assertIn("wifi_management_controller_open()", source)
        self.assertIn('ESP_LOGI(TAG, "sync WiFi button:', source)


if __name__ == "__main__":
    unittest.main()
