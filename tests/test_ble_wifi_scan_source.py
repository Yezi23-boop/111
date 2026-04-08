import pathlib
import re
import json
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
BLE_PROTOCOL_HEADER = (
    REPO_ROOT
    / "components"
    / "wifi_provision"
    / "src"
    / "ble_server"
    / "ble_provision_protocol.h"
)
BLE_PROTOCOL_SOURCE = (
    REPO_ROOT
    / "components"
    / "wifi_provision"
    / "src"
    / "ble_server"
    / "ble_provision_protocol.c"
)
WIFI_PROVISION_SOURCE = (
    REPO_ROOT / "components" / "wifi_provision" / "src" / "wifi_provision.c"
)
WIFI_MANAGER_HEADER = (
    REPO_ROOT
    / "components"
    / "wifi_provision"
    / "src"
    / "wifi_driver"
    / "wifi_manager.h"
)
WIFI_MANAGER_SOURCE = (
    REPO_ROOT
    / "components"
    / "wifi_provision"
    / "src"
    / "wifi_driver"
    / "wifi_manager.c"
)
BLE_TRANSPORT_SOURCE = (
    REPO_ROOT
    / "components"
    / "wifi_provision"
    / "src"
    / "ble_server"
    / "ble_provision_transport.c"
)


class BleWifiScanSourceTests(unittest.TestCase):
    def test_ble_protocol_supports_scan_wifi_command(self) -> None:
        header = BLE_PROTOCOL_HEADER.read_text(encoding="utf-8")
        source = BLE_PROTOCOL_SOURCE.read_text(encoding="utf-8")

        self.assertIn("BLE_PROV_CMD_SCAN_WIFI", header)
        self.assertIn('strcmp(cmd->valuestring, "scan_wifi") == 0', source)

    def test_ble_protocol_exposes_wifi_scan_formatters(self) -> None:
        header = BLE_PROTOCOL_HEADER.read_text(encoding="utf-8")
        source = BLE_PROTOCOL_SOURCE.read_text(encoding="utf-8")

        self.assertIn("ble_prov_wifi_scan_item_t", header)
        self.assertIn("ble_provision_protocol_format_wifi_scan_started", header)
        self.assertIn("ble_provision_protocol_format_wifi_scan_batch", header)
        self.assertIn("ble_provision_protocol_format_wifi_scan_done", header)
        self.assertIn("ble_provision_protocol_format_wifi_scan_failed", header)
        self.assertIn('"evt", "wifi_scan"', source)
        self.assertIn('buffer, buffer_len, "batch", &root', source)
        self.assertIn('"items"', source)

    def test_wifi_provision_normalizes_scan_results_for_ble_and_web(self) -> None:
        source = WIFI_PROVISION_SOURCE.read_text(encoding="utf-8")

        self.assertIn("WIFI_SCAN_MAX_VISIBLE_ITEMS 12", source)
        self.assertIn("WIFI_SCAN_BATCH_SIZE", source)
        self.assertIn("wifi_provision_collect_scan_items", source)
        self.assertIn("wifi_provision_compare_scan_items_desc", source)
        self.assertIn("qsort(", source)
        self.assertIn("ssid[0] == '\\0'", source)

    def test_wifi_provision_handles_ble_scan_busy_and_batches(self) -> None:
        source = WIFI_PROVISION_SOURCE.read_text(encoding="utf-8")

        self.assertIn('wifi_provision_send_ble_wifi_scan_failed("scan_busy")', source)
        self.assertIn("wifi_provision_send_ble_wifi_scan_started", source)
        self.assertIn("wifi_provision_send_ble_wifi_scan_batch", source)
        self.assertIn("wifi_provision_send_ble_wifi_scan_done", source)
        self.assertIn("case BLE_PROV_CMD_SCAN_WIFI:", source)
        self.assertIn("wifi_manager_scan(", source)

    def test_wifi_scan_batch_budget_fits_transport_limit(self) -> None:
        wifi_source = WIFI_PROVISION_SOURCE.read_text(encoding="utf-8")
        transport_source = BLE_TRANSPORT_SOURCE.read_text(encoding="utf-8")

        batch_match = re.search(r"WIFI_SCAN_BATCH_SIZE\s+(\d+)", wifi_source)
        payload_match = re.search(
            r"BLE_PROVISION_MAX_JSON_LEN\s+(\d+)", transport_source
        )
        self.assertIsNotNone(batch_match)
        self.assertIsNotNone(payload_match)

        batch_size = int(batch_match.group(1))
        payload_limit = int(payload_match.group(1))
        item = {"ssid": "A" * 32, "rssi": -127, "encrypted": True}
        payload = {
            "evt": "wifi_scan",
            "state": "batch",
            "items": [item] * batch_size,
            "more": True,
        }
        payload_len = len(json.dumps(payload, separators=(",", ":")))

        self.assertLessEqual(
            payload_len,
            payload_limit,
            msg=(
                f"当前 batch_size={batch_size} 时，32 字节 SSID 的 batch JSON "
                f"长度为 {payload_len}，超过 transport 上限 {payload_limit}"
            ),
        )

    def test_wifi_scan_duplicate_network_keeps_encrypted_true(self) -> None:
        source = WIFI_PROVISION_SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "items[existing_index].encrypted || encrypted",
            source,
        )

    def test_wifi_manager_scan_reports_internal_failures(self) -> None:
        header = WIFI_MANAGER_HEADER.read_text(encoding="utf-8")
        source = WIFI_MANAGER_SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "typedef void (*p_wifi_scan_callback)(wifi_ap_record_t *ap, int ap_count,\n                                     esp_err_t scan_result);",
            header,
        )
        self.assertIn("ctx->cb(NULL, 0, scan_ret);", source)
        self.assertIn("ctx->cb(NULL, 0, ESP_ERR_NO_MEM);", source)
        self.assertIn("return ESP_FAIL;", source)


if __name__ == "__main__":
    unittest.main()
