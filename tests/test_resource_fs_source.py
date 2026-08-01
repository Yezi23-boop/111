import pathlib
import unittest

from tests.main_cmake_contract import assert_main_source_globbed


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
PARTITIONS = REPO_ROOT / "partitions.csv"
ROOT_CMAKELISTS = REPO_ROOT / "CMakeLists.txt"
MAIN_CMAKELISTS = REPO_ROOT / "main" / "CMakeLists.txt"
MAIN_MANIFEST = REPO_ROOT / "main" / "idf_component.yml"
SDKCONFIG_DEFAULTS = REPO_ROOT / "sdkconfig.defaults"
SDKCONFIG = REPO_ROOT / "sdkconfig"
RESOURCE_FS_SOURCE = REPO_ROOT / "main" / "app" / "resource_fs.c"
RESOURCE_FS_HEADER = REPO_ROOT / "main" / "app" / "resource_fs.h"
HARDWARE_INIT_SOURCE = REPO_ROOT / "main" / "app" / "hardware_init.c"
RESOURCES_DIR = REPO_ROOT / "resources"


class ResourceFsSourceTests(unittest.TestCase):
    def test_partitions_define_resources_littlefs_for_dual_ota_layout(self) -> None:
        source = PARTITIONS.read_text(encoding="utf-8")

        self.assertIn("ota_0,    app,  ota_0,    0x20000, 12M", source)
        self.assertIn("ota_1,    app,  ota_1,    ,       12M", source)
        self.assertIn("assets,   data, spiffs,   ,       2M", source)
        self.assertIn("resources,data, littlefs, ,       4M", source)
        self.assertNotIn("factory,", source)
        self.assertNotIn("audio,", source)
        self.assertLess(source.index("assets,"), source.index("resources,"))

    def test_manifest_adds_littlefs_component(self) -> None:
        source = MAIN_MANIFEST.read_text(encoding="utf-8")

        self.assertIn("joltwallet/littlefs: ^1.21.1", source)

    def test_cmake_builds_and_flashes_resources_image(self) -> None:
        source = ROOT_CMAKELISTS.read_text(encoding="utf-8")

        self.assertIn("RESOURCE_FS_DIR", source)
        self.assertIn("littlefs_create_partition_image(resources", source)
        self.assertIn("FLASH_IN_PROJECT", source)

    def test_main_component_registers_resource_fs(self) -> None:
        source = MAIN_CMAKELISTS.read_text(encoding="utf-8")

        assert_main_source_globbed(self, "app/resource_fs.c")
        self.assertIn("joltwallet__littlefs", source)

    def test_lvgl_posix_driver_maps_a_drive_to_resources(self) -> None:
        defaults = SDKCONFIG_DEFAULTS.read_text(encoding="utf-8")
        sdkconfig = SDKCONFIG.read_text(encoding="utf-8")

        for source in (defaults, sdkconfig):
            self.assertIn("CONFIG_LV_USE_FS_POSIX=y", source)
            self.assertIn("CONFIG_LV_FS_POSIX_LETTER=65", source)
            self.assertIn('CONFIG_LV_FS_POSIX_PATH="/resources"', source)

    def test_resource_fs_mounts_partition_to_resources(self) -> None:
        source = RESOURCE_FS_SOURCE.read_text(encoding="utf-8")
        header = RESOURCE_FS_HEADER.read_text(encoding="utf-8")

        self.assertIn('#include "esp_littlefs.h"', source)
        self.assertIn('kResourceFsBasePath = "/resources"', source)
        self.assertIn('kResourceFsPartitionLabel = "resources"', source)
        self.assertIn("esp_vfs_littlefs_register", source)
        self.assertIn("esp_littlefs_info", source)
        self.assertIn("esp_err_t resource_fs_init(void);", header)

    def test_hardware_init_mounts_resource_fs_before_ui_task_start(self) -> None:
        source = HARDWARE_INIT_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "resource_fs.h"', source)
        self.assertIn("resource_fs_init()", source)
        self.assertIn("Resource LittleFS init failed", source)

    def test_resources_tree_documents_lvgl_binfont_path(self) -> None:
        self.assertTrue((RESOURCES_DIR / "README.md").exists())
        self.assertTrue((RESOURCES_DIR / "fonts" / "README.md").exists())
        text = (RESOURCES_DIR / "fonts" / "README.md").read_text(encoding="utf-8")

        self.assertIn(
            'lv_binfont_create("A:/fonts/lvgl_montserrat_lxgw_tghz_level1_3500_24_4.bin")',
            text,
        )


if __name__ == "__main__":
    unittest.main()
