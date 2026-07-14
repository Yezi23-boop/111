import unittest

from tests.main_paths import UI_CUSTOM_HEADER
from tests.main_paths import UI_EVENTS_INIT_SOURCE
from tests.main_paths import UI_MAIN_DROPDOWN_CONTROLLER_HEADER
from tests.main_paths import UI_MAIN_DROPDOWN_CONTROLLER_SOURCE


class MainScreenBleToggleSourceTests(unittest.TestCase):
    def test_events_init_binds_real_bluetooth_toggle_handler(self) -> None:
        source = UI_EVENTS_INIT_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "main_dropdown_controller.h"', source)
        self.assertIn("screen_main_bluetooth_event_handler", source)
        self.assertIn("main_dropdown_controller_bind(", source)
        self.assertEqual(source.count("main_dropdown_controller_bind("), 1)
        self.assertNotIn("screen_main_event_handler", source)
        self.assertIn(
            "lv_obj_add_event_cb(ui->screen_main_Bluetooth, screen_main_bluetooth_event_handler, LV_EVENT_ALL, ui);",
            source,
        )

    def test_custom_layer_exposes_main_dropdown_controller(self) -> None:
        custom_header = UI_CUSTOM_HEADER.read_text(encoding="utf-8")
        controller_header = UI_MAIN_DROPDOWN_CONTROLLER_HEADER.read_text(
            encoding="utf-8"
        )

        self.assertIn('#include "main_dropdown_controller.h"', custom_header)
        self.assertIn("void main_dropdown_controller_bind(lv_ui *ui);", controller_header)
        self.assertIn("void main_dropdown_controller_handle_bluetooth_click(void);", controller_header)

    def test_main_dropdown_controller_syncs_ble_state_and_shows_toast(self) -> None:
        source = UI_MAIN_DROPDOWN_CONTROLLER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("network_manager_set_ble_enabled(", source)
        self.assertIn("network_manager_is_ble_active()", source)
        self.assertIn("network_manager_is_ble_enabled()", source)
        self.assertIn("foreground_runtime_gate_acquire", source)
        self.assertIn("FOREGROUND_RUNTIME_OWNER_BLE_PROVISIONING", source)
        self.assertIn("foreground_runtime_gate_release", source)
        self.assertIn("background_service_manager_notify_foreground_runtime_changed", source)
        self.assertIn("kBleRetryDelayMs", source)
        self.assertIn("ret == ESP_ERR_NO_MEM", source)
        self.assertIn("lv_timer_create(", source)
        self.assertIn("BLE switch update failed", source)
        self.assertIn("LV_STATE_CHECKED", source)
        self.assertIn('ESP_LOGI(TAG, "bind BLE dropdown controller"', source)
        self.assertIn(
            'ESP_LOGI(TAG, "Bluetooth button clicked: ble_enabled=%d ble_active=%d"',
            source,
        )
        self.assertIn(
            'ESP_LOGI(TAG, "sync Bluetooth button: checked=%d ble_enabled=%d ble_active=%d"',
            source,
        )
        self.assertIn('ESP_LOGI(TAG, "show BLE toast: %s"', source)
        self.assertIn(
            "lv_obj_remove_flag(s_ui->screen_main_Bluetooth, LV_OBJ_FLAG_HIDDEN);",
            source,
        )
        self.assertIn(
            "lv_obj_remove_flag(s_ui->screen_main_Bluetooth_label, LV_OBJ_FLAG_HIDDEN);",
            source,
        )


if __name__ == "__main__":
    unittest.main()
