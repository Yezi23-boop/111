import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
LV_PORT_SOURCE = REPO_ROOT / "components" / "lvgl_port" / "lv_port.c"


class LvPortDmaBounceSourceTests(unittest.TestCase):
    def test_lv_port_uses_internal_dma_bounce_buffers_before_flush(self) -> None:
        source = LV_PORT_SOURCE.read_text(encoding="utf-8")

        self.assertIn("static uint8_t *s_tx_chunk_bufs[LV_PORT_MAX_INFLIGHT_CHUNKS]", source)
        self.assertIn("heap_caps_malloc(tx_buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)", source)
        self.assertIn("memcpy(tx_px_map, px_map, color_bytes);", source)
        self.assertIn("return esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, tx_px_map);", source)

    def test_lv_port_uses_explicit_partial_render_mode(self) -> None:
        source = LV_PORT_SOURCE.read_text(encoding="utf-8")

        self.assertGreaterEqual(source.count("LV_DISPLAY_RENDER_MODE_PARTIAL"), 2)

    def test_lv_port_registers_rounder_callback_for_invalidate_area(self) -> None:
        source = LV_PORT_SOURCE.read_text(encoding="utf-8")

        self.assertIn("static void lv_port_rounder_event_cb(lv_event_t *e);", source)
        self.assertGreaterEqual(source.count("lv_display_add_event_cb(s_display, lv_port_rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);"), 2)
        self.assertIn("area->x1 = (x1 >> 1) << 1;", source)
        self.assertIn("area->x2 = ((x2 >> 1) << 1) + 1;", source)


if __name__ == "__main__":
    unittest.main()
