import unittest

from tests.main_cmake_contract import assert_main_source_globbed
from tests.main_paths import (
    BOARD_DS2413_MOTOR_HEADER,
    BOARD_DS2413_MOTOR_SOURCE,
    DS2413_CMAKE,
    DS2413_HEADER,
    DS2413_SOURCE,
    HARDWARE_INIT_SOURCE,
    MAIN_CMAKE,
    MAIN_IDF_COMPONENT,
)


class BoardDs2413MotorSourceTests(unittest.TestCase):
    def test_main_build_wires_board_motor_and_onewire_dependency(self) -> None:
        cmake = MAIN_CMAKE.read_text(encoding="utf-8")
        manifest = MAIN_IDF_COMPONENT.read_text(encoding="utf-8")

        assert_main_source_globbed(self, "app/board_ds2413_motor.c")
        self.assertIn("ds2413", cmake)
        self.assertIn("espressif__onewire_bus", cmake)
        self.assertIn("espressif/onewire_bus: ^1.1.1", manifest)

    def test_hardware_init_turns_motor_off_early_without_blocking_boot(self) -> None:
        source = HARDWARE_INIT_SOURCE.read_text(encoding="utf-8")
        nvs_index = source.index("ret = hardware_nvs_init();")
        motor_index = source.index("ret = board_ds2413_motor_init();")
        resource_index = source.index("ret = resource_fs_init();")

        self.assertIn('#include "board_ds2413_motor.h"', source)
        self.assertLess(nvs_index, motor_index)
        self.assertLess(motor_index, resource_index)
        self.assertIn('ESP_LOGW(TAG, "DS2413 motor init failed: %s"', source)
        self.assertNotIn("return ret;\n    }\n\n    /* 通用资源分区", source[motor_index:])

    def test_board_motor_uses_gpio18_rmt_only_and_mutex(self) -> None:
        source = BOARD_DS2413_MOTOR_SOURCE.read_text(encoding="utf-8")
        header = BOARD_DS2413_MOTOR_HEADER.read_text(encoding="utf-8")

        self.assertIn("GPIO_NUM_18", source)
        self.assertIn("onewire_new_bus_rmt", source)
        self.assertNotIn("onewire_new_bus_uart", source)
        self.assertNotIn("onewire_bus_impl_uart", source)
        self.assertNotIn("UART_NUM_1", source)
        self.assertNotIn("UART1", source)
        self.assertIn("xSemaphoreCreateMutex", source)
        self.assertIn("xSemaphoreTake(s_lock, portMAX_DELAY)", source)
        self.assertIn(".pioa_release = false", source)
        self.assertIn(".piob_release = true", source)
        self.assertIn("DS2413 motor default off", source)
        self.assertIn("board_ds2413_motor_init", header)
        self.assertIn("board_ds2413_motor_set_enabled", header)
        self.assertIn("board_ds2413_motor_pulse", header)

    def test_ds2413_component_is_minimal_board_family_driver(self) -> None:
        source = DS2413_SOURCE.read_text(encoding="utf-8")
        header = DS2413_HEADER.read_text(encoding="utf-8")
        cmake = DS2413_CMAKE.read_text(encoding="utf-8")

        self.assertIn("#define DS2413_BOARD_FAMILY_CODE 0xBA", header)
        self.assertIn("return family_code == DS2413_BOARD_FAMILY_CODE", source)
        self.assertNotIn("#define DS2413_FAMILY_CODE", header + source)
        self.assertNotIn("DS2413_CLONE_FAMILY_CODE", header + source)
        self.assertIn("ds2413_find_first", header)
        self.assertIn("ds2413_read_state", header)
        self.assertIn("ds2413_write_latch", header)
        self.assertIn("REQUIRES espressif__onewire_bus log", cmake)


if __name__ == "__main__":
    unittest.main()
