import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
CO5300_PANEL_SOURCE = REPO_ROOT / "components" / "co5300_panel" / "co5300_panel.c"


class Co5300TeSyncSourceTests(unittest.TestCase):
    def test_te_sync_uses_mode1_rising_edge_and_real_te_window_wait(self) -> None:
        source = CO5300_PANEL_SOURCE.read_text(encoding="utf-8")

        self.assertIn("{0x35, (uint8_t[]){CO5300_PANEL_TE_MODE}, 1, 0}", source)
        self.assertIn("{0x44, (uint8_t[]){0x00, 0x00}, 2, 0}", source)
        self.assertIn(".intr_type = GPIO_INTR_POSEDGE", source)
        self.assertIn("if (gpio_get_level(CO5300_PANEL_PIN_TE) == 1)", source)
        self.assertIn("while (xSemaphoreTake(s_te_semaphore, 0) == pdTRUE)", source)
        self.assertIn("return ESP_OK;", source)


if __name__ == "__main__":
    unittest.main()
