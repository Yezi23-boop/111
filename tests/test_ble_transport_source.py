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
    def test_ble_transport_uses_little_endian_uuid_bytes(self) -> None:
        source = BLE_TRANSPORT_SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "0x02, 0x1a, 0x90, 0x04, 0x03, 0x82, 0x4a, 0xea,\n"
            "    0xf4, 0xbf, 0x3f, 0x6b, 0xb4, 0xdf, 0x5a, 0x1c",
            source,
        )
        self.assertIn(
            "0x02, 0x1a, 0x90, 0x04, 0x03, 0x82, 0x4a, 0xea,\n"
            "    0xf4, 0xbf, 0x3f, 0x6b, 0xb5, 0xdf, 0x5a, 0x1c",
            source,
        )
        self.assertIn(
            "0x02, 0x1a, 0x90, 0x04, 0x03, 0x82, 0x4a, 0xea,\n"
            "    0xf4, 0xbf, 0x3f, 0x6b, 0xb6, 0xdf, 0x5a, 0x1c",
            source,
        )

    def test_ble_transport_rolls_back_on_init_failure(self) -> None:
        source = BLE_TRANSPORT_SOURCE.read_text(encoding="utf-8")

        self.assertIn("ble_provision_transport_reset_runtime_state", source)
        self.assertIn("nimble_port_deinit();", source)
        self.assertIn('xTaskGetHandle("nimble_host")', source)
        self.assertIn("nimble host task missing after init", source)

    def test_ble_transport_splits_adv_and_scan_response_fields(self) -> None:
        source = BLE_TRANSPORT_SOURCE.read_text(encoding="utf-8")

        self.assertIn("ble_gap_adv_rsp_set_fields(&scan_rsp_fields);", source)
        self.assertIn("scan_rsp_fields.name = (uint8_t *)s_device_name;", source)
        self.assertNotIn("fields.tx_pwr_lvl_is_present = 1;", source)

    def test_ble_transport_supports_fragmented_rx_frames(self) -> None:
        source = BLE_TRANSPORT_SOURCE.read_text(encoding="utf-8")

        self.assertIn("BLE_PROVISION_RX_FRAME_LEN", source)
        self.assertIn("ble_provision_transport_consume_rx_chunk", source)
        self.assertIn("memchr(chunk + start, '\\n', chunk_len - start)", source)
        self.assertIn(
            "if (s_rx_frame_len == 0 && chunk[0] == '{' && chunk[chunk_len - 1] == '}')",
            source,
        )

    def test_ble_transport_notify_json_uses_newline_delimited_payload(self) -> None:
        source = BLE_TRANSPORT_SOURCE.read_text(encoding="utf-8")

        self.assertIn("char framed_payload[BLE_PROVISION_MAX_JSON_LEN + 2] = {0};", source)
        self.assertIn("framed_payload[payload_len++] = '\\n';", source)
        self.assertIn("ble_hs_mbuf_from_flat(framed_payload + offset, chunk_len);", source)
        self.assertIn("ble_gatts_notify_custom(s_conn_handle, s_tx_val_handle, txom);", source)

    def test_ble_transport_notify_json_sends_safe_sized_chunks(self) -> None:
        source = BLE_TRANSPORT_SOURCE.read_text(encoding="utf-8")

        self.assertIn("#define BLE_PROVISION_NOTIFY_CHUNK_LEN 20", source)
        self.assertIn("for (size_t offset = 0; offset < payload_len;", source)
        self.assertIn("size_t chunk_len = payload_len - offset;", source)
        self.assertIn("if (chunk_len > BLE_PROVISION_NOTIFY_CHUNK_LEN)", source)
        self.assertIn("ble_hs_mbuf_from_flat(framed_payload + offset, chunk_len);", source)


if __name__ == "__main__":
    unittest.main()
