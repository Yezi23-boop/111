import unittest

from tests.main_paths import HARDWARE_INIT_SOURCE
from tests.main_paths import QMI8658C_CMAKE
from tests.main_paths import QMI8658C_HEADER
from tests.main_paths import QMI8658C_REGS_HEADER
from tests.main_paths import QMI8658C_SOURCE


class Qmi8658cSourceTests(unittest.TestCase):
    def test_component_exists_and_depends_on_shared_i2c_manager(self) -> None:
        self.assertTrue(QMI8658C_CMAKE.exists(), "components/qmi8658c/CMakeLists.txt should exist")
        cmake = QMI8658C_CMAKE.read_text(encoding="utf-8")

        self.assertIn('SRCS "qmi8658c.c"', cmake)
        self.assertIn('INCLUDE_DIRS "include"', cmake)
        self.assertIn("REQUIRES i2c_manager", cmake)

    def test_header_exposes_minimal_probe_config_and_raw_read_api(self) -> None:
        self.assertTrue(QMI8658C_HEADER.exists(), "components/qmi8658c/include/qmi8658c.h should exist")
        header = QMI8658C_HEADER.read_text(encoding="utf-8")

        self.assertIn("qmi8658c_identity_t", header)
        self.assertIn("qmi8658c_bus_config_t", header)
        self.assertIn("qmi8658c_raw_sample_t", header)
        self.assertIn("qmi8658c_config_t", header)
        self.assertIn("qmi8658c_status1_t", header)
        self.assertIn("qmi8658c_statusint_t", header)
        self.assertIn("qmi8658c_wom_config_t", header)
        self.assertIn("esp_err_t qmi8658c_init(void);", header)
        self.assertIn("qmi8658c_init_with_bus_config", header)
        self.assertIn("esp_err_t qmi8658c_probe(qmi8658c_identity_t *identity);", header)
        self.assertIn("esp_err_t qmi8658c_configure(const qmi8658c_config_t *config);", header)
        self.assertIn("esp_err_t qmi8658c_read_raw(qmi8658c_raw_sample_t *sample);", header)
        self.assertIn("qmi8658c_configure_wake_on_motion", header)
        self.assertIn("qmi8658c_read_status1", header)
        self.assertIn("qmi8658c_read_statusint", header)
        self.assertNotIn("qmi8658c_configure_attitude_engine", header)
        self.assertNotIn("qmi8658c_request_motion_on_demand", header)
        self.assertNotIn("qmi8658c_read_dq", header)

    def test_register_constants_match_board_and_datasheet_evidence(self) -> None:
        self.assertTrue(QMI8658C_REGS_HEADER.exists(), "components/qmi8658c/qmi8658c_regs.h should exist")
        regs = QMI8658C_REGS_HEADER.read_text(encoding="utf-8")

        self.assertIn("#define QMI8658C_I2C_ADDR_7BIT 0x6B", regs)
        self.assertIn("#define QMI8658C_REG_WHO_AM_I 0x00", regs)
        self.assertIn("#define QMI8658C_WHO_AM_I_EXPECTED 0x05", regs)
        self.assertIn("#define QMI8658C_REG_REVISION_ID 0x01", regs)
        self.assertIn("#define QMI8658C_REG_CTRL1 0x02", regs)
        self.assertIn("#define QMI8658C_REG_CTRL2 0x03", regs)
        self.assertIn("#define QMI8658C_REG_CTRL3 0x04", regs)
        self.assertIn("#define QMI8658C_REG_CTRL7 0x08", regs)
        self.assertIn("#define QMI8658C_REG_CTRL8 0x09", regs)
        self.assertIn("#define QMI8658C_REG_CTRL9 0x0A", regs)
        self.assertIn("#define QMI8658C_REG_CAL1_L 0x0B", regs)
        self.assertIn("#define QMI8658C_REG_STATUSINT 0x2D", regs)
        self.assertIn("#define QMI8658C_REG_STATUS0 0x2E", regs)
        self.assertIn("#define QMI8658C_REG_STATUS1 0x2F", regs)
        self.assertIn("#define QMI8658C_REG_TEMP_L 0x33", regs)
        self.assertIn("#define QMI8658C_REG_AX_L 0x35", regs)
        self.assertIn("#define QMI8658C_REG_GX_L 0x3B", regs)
        self.assertIn("#define QMI8658C_STATUS1_WOM (1u << 2)", regs)
        self.assertIn("#define QMI8658C_STATUS1_CMD_DONE (1u << 0)", regs)
        self.assertIn("#define QMI8658C_STATUSINT_CTRL9_DONE (1u << 7)", regs)
        self.assertIn("#define QMI8658C_STATUSINT_INT1_LEVEL (1u << 1)", regs)
        self.assertIn("#define QMI8658C_CTRL8_CTRL9_HANDSHAKE_STATUSINT (1u << 7)", regs)
        self.assertIn("#define QMI8658C_CTRL9_CMD_ACK 0x00", regs)
        self.assertIn("#define QMI8658C_CTRL9_CMD_WRITE_WOM_SETTING 0x08", regs)
        self.assertIn("#define QMI8658C_CTRL9_CMD_CONFIGURE_TAP 0x0C", regs)

    def test_wake_on_motion_follows_ctrl9_sequence(self) -> None:
        self.assertTrue(QMI8658C_SOURCE.exists(), "components/qmi8658c/qmi8658c.c should exist")
        source = QMI8658C_SOURCE.read_text(encoding="utf-8")

        self.assertIn("qmi8658c_configure_wake_on_motion", source)
        self.assertIn("qmi8658c_write_byte(QMI8658C_REG_CTRL7, 0)", source)
        self.assertIn("qmi8658c_write_byte(QMI8658C_REG_CTRL2, ctrl2)", source)
        self.assertIn("qmi8658c_write_byte(QMI8658C_REG_CAL1_L", source)
        self.assertIn("qmi8658c_write_byte(QMI8658C_REG_CAL1_H", source)
        self.assertIn("QMI8658C_CTRL9_CMD_WRITE_WOM_SETTING", source)
        self.assertIn("qmi8658c_execute_ctrl9", source)
        self.assertIn("qmi8658c_read_statusint_raw(&statusint)", source)
        self.assertIn("QMI8658C_STATUSINT_CTRL9_DONE", source)
        self.assertIn("QMI8658C_CTRL9_CMD_ACK", source)
        self.assertIn("qmi8658c_enable_statusint_ctrl9_handshake", source)
        self.assertIn("ctrl9 command 0x%02x ack clear timeout", source)
        self.assertIn("QMI8658C_CTRL7_ACCEL_ENABLE", source)

    def test_rev_a_driver_does_not_expose_old_mod_or_dq_path(self) -> None:
        self.assertTrue(QMI8658C_SOURCE.exists(), "components/qmi8658c/qmi8658c.c should exist")
        source = QMI8658C_SOURCE.read_text(encoding="utf-8")
        header = QMI8658C_HEADER.read_text(encoding="utf-8")
        regs = QMI8658C_REGS_HEADER.read_text(encoding="utf-8")

        self.assertNotIn("qmi8658c_configure_attitude_engine", source + header)
        self.assertNotIn("qmi8658c_request_motion_on_demand", source + header)
        self.assertNotIn("qmi8658c_read_dq", source + header)
        self.assertNotIn("QMI8658C_CTRL9_CMD_REQ_MOTION_ON_DEMAND", source + regs)
        self.assertNotIn("QMI8658C_STATUS0_AE_DATA_AVAILABLE", source + regs)
        self.assertIn("Rev A 中 0x0C 是 Tap 配置命令", regs)

    def test_source_uses_i2c_manager_bus_handle_without_new_gpio_owner(self) -> None:
        self.assertTrue(QMI8658C_SOURCE.exists(), "components/qmi8658c/qmi8658c.c should exist")
        source = QMI8658C_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "i2c_manager.h"', source)
        self.assertIn("i2c_manager_init()", source)
        self.assertIn("i2c_manager_get_bus_handle()", source)
        self.assertIn("i2c_master_bus_add_device", source)
        self.assertIn("s_i2c_addr_7bit", source)
        self.assertIn("i2c_master_probe(bus_handle, s_i2c_addr_7bit, 50)", source)
        self.assertIn("qmi8658c_init_with_bus_config", source)
        self.assertNotIn("#define QMI8658C_SCL_GPIO", source)
        self.assertNotIn("#define QMI8658C_SDA_GPIO", source)
        self.assertNotIn("i2c_new_master_bus", source)

    def test_source_reads_identity_and_raw_sensor_window(self) -> None:
        self.assertTrue(QMI8658C_SOURCE.exists(), "components/qmi8658c/qmi8658c.c should exist")
        source = QMI8658C_SOURCE.read_text(encoding="utf-8")

        self.assertIn("QMI8658C_REG_WHO_AM_I", source)
        self.assertIn("QMI8658C_WHO_AM_I_EXPECTED", source)
        self.assertIn("QMI8658C_REG_REVISION_ID", source)
        self.assertRegex(source, r"uint8_t\s+raw\[14\]")
        self.assertIn("qmi8658c_read_bytes(QMI8658C_REG_TEMP_L, raw, sizeof(raw))", source)
        self.assertIn("qmi8658c_decode_i16(&raw[2])", source)
        self.assertIn("qmi8658c_decode_i16(&raw[8])", source)

    def test_config_rejects_datasheet_na_odr_codes(self) -> None:
        self.assertTrue(QMI8658C_SOURCE.exists(), "components/qmi8658c/qmi8658c.c should exist")
        source = QMI8658C_SOURCE.read_text(encoding="utf-8")

        self.assertIn("qmi8658c_accel_odr_code_valid", source)
        self.assertIn("odr <= 0x08 || (odr >= 0x0C && odr <= 0x0F)", source)
        self.assertIn("qmi8658c_gyro_odr_code_valid", source)
        self.assertIn("return odr <= 0x08;", source)
        self.assertIn("accel odr code is not supported by datasheet", source)
        self.assertIn("gyro odr code is not supported by datasheet", source)

    def test_component_is_not_wired_into_default_hardware_init(self) -> None:
        self.assertTrue(HARDWARE_INIT_SOURCE.exists(), "main/app/hardware_init.c should exist")
        hardware_init = HARDWARE_INIT_SOURCE.read_text(encoding="utf-8")

        self.assertNotIn("qmi8658c_init", hardware_init)
        self.assertNotIn("qmi8658c_probe", hardware_init)
        self.assertNotIn("qmi8658c_read_raw", hardware_init)


if __name__ == "__main__":
    unittest.main()
