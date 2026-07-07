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

    def test_header_exposes_unified_config_and_physical_read_api(self) -> None:
        self.assertTrue(QMI8658C_HEADER.exists(), "components/qmi8658c/include/qmi8658c.h should exist")
        header = QMI8658C_HEADER.read_text(encoding="utf-8")

        self.assertIn("qmi8658c_info_t", header)
        self.assertIn("qmi8658c_bus_t", header)
        self.assertIn("qmi8658c_accel_t", header)
        self.assertIn("qmi8658c_gyro_t", header)
        self.assertIn("qmi8658c_sample_t", header)
        self.assertIn("qmi8658c_int_source_t", header)
        self.assertIn("QMI8658C_INT_SOURCE_DISABLED", header)
        self.assertIn("qmi8658c_config_t", header)
        self.assertIn("qmi8658c_int_source_t int1_source", header)
        self.assertIn("qmi8658c_int_source_t int2_source", header)
        self.assertIn("esp_err_t qmi8658c_init(void);", header)
        self.assertIn("esp_err_t qmi8658c_init_bus(const qmi8658c_bus_t *bus);", header)
        self.assertIn("esp_err_t qmi8658c_probe(qmi8658c_info_t *info);", header)
        self.assertIn("esp_err_t qmi8658c_config(const qmi8658c_config_t *config);", header)
        self.assertIn("esp_err_t qmi8658c_read(qmi8658c_sample_t *sample);", header)
        self.assertNotIn("qmi8658c_wom_status_t", header)
        self.assertNotIn("qmi8658c_int_status_t", header)
        self.assertNotIn("qmi8658c_wom_t", header)
        self.assertNotIn("qmi8658c_read_wom", header)
        self.assertNotIn("qmi8658c_read_int", header)
        self.assertNotIn("qmi8658c_enable_wom", header)
        self.assertNotIn("qmi8658c_disable_wom", header)
        self.assertNotIn("qmi8658c_raw_sample_t", header)
        self.assertNotIn("qmi8658c_read_raw", header)
        self.assertNotIn("qmi8658c_accel_mps2_t", header)
        self.assertNotIn("qmi8658c_gyro_dps_t", header)
        self.assertNotIn("x_mps2", header)
        self.assertNotIn("x_dps", header)
        self.assertNotIn("qmi8658c_read_accel_mps2", header)
        self.assertNotIn("qmi8658c_read_gyro_dps", header)
        self.assertNotIn("qmi8658c_read_sample", header)
        self.assertNotIn("qmi8658c_configure", header)
        self.assertNotIn("qmi8658c_init_with_bus_config", header)
        self.assertNotIn("qmi8658c_configure_attitude_engine", header)
        self.assertNotIn("qmi8658c_request_motion_on_demand", header)
        self.assertNotIn("qmi8658c_read_dq", header)

    def test_register_constants_match_unified_sample_path(self) -> None:
        self.assertTrue(QMI8658C_REGS_HEADER.exists(), "components/qmi8658c/qmi8658c_regs.h should exist")
        regs = QMI8658C_REGS_HEADER.read_text(encoding="utf-8")

        self.assertIn("#define QMI8658C_I2C_ADDR_7BIT 0x6B", regs)
        self.assertIn("#define QMI8658C_REG_WHO_AM_I 0x00", regs)
        self.assertIn("#define QMI8658C_WHO_AM_I_EXPECTED 0x05", regs)
        self.assertIn("#define QMI8658C_REG_REVISION_ID 0x01", regs)
        self.assertIn("#define QMI8658C_REG_CTRL1 0x02", regs)
        self.assertIn("#define QMI8658C_REG_CTRL2 0x03", regs)
        self.assertIn("#define QMI8658C_REG_CTRL3 0x04", regs)
        self.assertIn("#define QMI8658C_REG_CTRL5 0x06", regs)
        self.assertIn("#define QMI8658C_REG_CTRL7 0x08", regs)
        self.assertIn("#define QMI8658C_REG_STATUS0 0x2E", regs)
        self.assertIn("#define QMI8658C_REG_TIMESTAMP_L 0x30", regs)
        self.assertIn("#define QMI8658C_REG_TEMP_L 0x33", regs)
        self.assertIn("#define QMI8658C_REG_AX_L 0x35", regs)
        self.assertIn("#define QMI8658C_REG_GX_L 0x3B", regs)
        self.assertIn("#define QMI8658C_CTRL1_WAVESHARE_DEFAULT 0x60", regs)
        self.assertIn("#define QMI8658C_CTRL5_WAVESHARE_DEFAULT 0x03", regs)
        self.assertIn("#define QMI8658C_CTRL7_ACCEL_ENABLE (1u << 0)", regs)
        self.assertIn("#define QMI8658C_CTRL7_GYRO_ENABLE (1u << 1)", regs)
        self.assertIn("#define QMI8658C_CTRL9_CMD_CONFIGURE_TAP 0x0C", regs)
        self.assertNotIn("QMI8658C_STATUS1_WOM", regs)
        self.assertNotIn("QMI8658C_CTRL9_CMD_WRITE_WOM_SETTING", regs)
        self.assertNotIn("QMI8658C_REG_CAL1_L", regs)

    def test_unified_config_rejects_enabled_int_sources_for_now(self) -> None:
        self.assertTrue(QMI8658C_SOURCE.exists(), "components/qmi8658c/qmi8658c.c should exist")
        source = QMI8658C_SOURCE.read_text(encoding="utf-8")

        self.assertIn("esp_err_t qmi8658c_config(const qmi8658c_config_t *config)", source)
        self.assertIn("config->int1_source == QMI8658C_INT_SOURCE_DISABLED", source)
        self.assertIn("config->int2_source == QMI8658C_INT_SOURCE_DISABLED", source)
        self.assertIn("int1 source is reserved and must be disabled", source)
        self.assertIn("int2 source is reserved and must be disabled", source)
        self.assertIn("qmi8658c_write_byte(QMI8658C_REG_CTRL2, ctrl2)", source)
        self.assertIn("qmi8658c_write_byte(QMI8658C_REG_CTRL3, ctrl3)", source)
        self.assertIn("qmi8658c_write_byte(QMI8658C_REG_CTRL5", source)
        self.assertIn("QMI8658C_CTRL1_WAVESHARE_DEFAULT", source)
        self.assertIn("QMI8658C_CTRL5_WAVESHARE_DEFAULT", source)
        self.assertIn("qmi8658c_write_byte(QMI8658C_REG_CTRL7, ctrl7)", source)
        self.assertIn("qmi8658c_write_byte(QMI8658C_REG_CTRL7, 0)", source)
        self.assertIn("disable sensors before configure failed", source)
        self.assertIn("configured registers: ctrl1=0x%02X ctrl2=0x%02X ctrl3=0x%02X ctrl5=0x%02X ctrl7=0x%02X", source)
        self.assertIn("s_sample_configured = true;", source)
        self.assertNotIn("qmi8658c_enable_wom", source)
        self.assertNotIn("qmi8658c_disable_wom", source)
        self.assertNotIn("qmi8658c_read_wom", source)
        self.assertNotIn("qmi8658c_read_int", source)

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
        self.assertIn("qmi8658c_init_bus", source)
        self.assertNotIn("gpio_config", source)
        self.assertNotIn("gpio_isr_handler_add", source)
        self.assertNotIn("i2c_new_master_bus", source)

    def test_source_reads_identity_and_waveshare_aligned_sensor_window(self) -> None:
        self.assertTrue(QMI8658C_SOURCE.exists(), "components/qmi8658c/qmi8658c.c should exist")
        source = QMI8658C_SOURCE.read_text(encoding="utf-8")

        self.assertIn("QMI8658C_REG_WHO_AM_I", source)
        self.assertIn("QMI8658C_WHO_AM_I_EXPECTED", source)
        self.assertIn("QMI8658C_REG_REVISION_ID", source)
        self.assertIn("qmi8658c_read_bytes(QMI8658C_REG_STATUS0, &status0, 1)", source)
        self.assertIn("qmi8658c_read_bytes(QMI8658C_REG_TIMESTAMP_L", source)
        self.assertIn("qmi8658c_decode_u24(timestamp_raw)", source)
        self.assertRegex(source, r"uint8_t\s+raw\[12\]")
        self.assertIn("qmi8658c_read_bytes(QMI8658C_REG_AX_L, raw, sizeof(raw))", source)
        self.assertIn("qmi8658c_decode_i16(&raw[0])", source)
        self.assertIn("qmi8658c_decode_i16(&raw[6])", source)
        self.assertIn("s_read_count % 100U", source)
        self.assertIn("原始表 | 次数=%-5u 状态0=0x%02X 就绪=%d 时间戳=%-8u", source)

    def test_driver_exposes_accel_mps2_conversion_owned_by_qmi(self) -> None:
        self.assertTrue(QMI8658C_SOURCE.exists(), "components/qmi8658c/qmi8658c.c should exist")
        source = QMI8658C_SOURCE.read_text(encoding="utf-8")
        header = QMI8658C_HEADER.read_text(encoding="utf-8")

        self.assertIn("qmi8658c_accel_t", header)
        self.assertIn("float x", header)
        self.assertIn("单位 `m/s^2`", header)
        self.assertIn("static const float k_standard_gravity = 9.80665f;", source)
        self.assertIn("static uint8_t s_accel_fs_code = 0", source)
        self.assertIn("static bool s_sample_configured = false", source)
        self.assertIn("static int32_t qmi8658c_accel_lsb_per_g(uint8_t accel_fs);", source)
        self.assertIn("static esp_err_t qmi8658c_convert_raw_accel(", source)
        self.assertIn("esp_err_t qmi8658c_read(qmi8658c_sample_t *sample)", source)
        self.assertIn("qmi8658c sample range is not configured", source)
        self.assertNotIn("esp_err_t qmi8658c_read_accel", source)
        self.assertIn("qmi8658c_read_raw(&raw)", source)
        self.assertIn("accel full-scale code has no mps2 scale", source)
        self.assertNotIn("wom accel full-scale code", source)
        self.assertIn("sample->x = qmi8658c_accel_raw_to_mps2(raw->accel_x, s_accel_fs_code);", source)
        self.assertIn("s_accel_fs_code = config->accel_fs;", source)
        for sensitivity in ("16384", "8192", "4096", "2048"):
            self.assertIn(f"return {sensitivity};", source)

    def test_driver_exposes_single_gyro_dps_conversion_owned_by_qmi(self) -> None:
        self.assertTrue(QMI8658C_SOURCE.exists(), "components/qmi8658c/qmi8658c.c should exist")
        source = QMI8658C_SOURCE.read_text(encoding="utf-8")
        header = QMI8658C_HEADER.read_text(encoding="utf-8")

        self.assertIn("qmi8658c_gyro_t", header)
        self.assertIn("单位 `deg/s`", header)
        self.assertIn("static uint8_t s_gyro_fs_code = 0", source)
        self.assertIn("static int32_t qmi8658c_gyro_lsb_per_dps(uint8_t gyro_fs);", source)
        self.assertIn("static esp_err_t qmi8658c_convert_raw_gyro(", source)
        self.assertNotIn("esp_err_t qmi8658c_read_gyro", source)
        self.assertIn("gyro full-scale code has no dps scale", source)
        self.assertIn("s_gyro_fs_code = config->gyro_fs;", source)
        self.assertIn("sample->x = qmi8658c_gyro_raw_to_dps(raw->gyro_x, s_gyro_fs_code);", source)
        self.assertNotIn("radps", header + source)
        for sensitivity in ("2048", "1024", "512", "256", "128", "64", "32", "16"):
            self.assertIn(f"return {sensitivity};", source)

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
