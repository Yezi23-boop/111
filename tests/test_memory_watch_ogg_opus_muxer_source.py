import unittest

from tests.main_cmake_contract import assert_main_source_globbed
from tests.main_paths import MEMORY_WATCH_OGG_OPUS_MUXER_HEADER
from tests.main_paths import MEMORY_WATCH_OGG_OPUS_MUXER_SOURCE


class MemoryWatchOggOpusMuxerSourceTests(unittest.TestCase):
    def test_muxer_header_and_source_exist(self) -> None:
        self.assertTrue(MEMORY_WATCH_OGG_OPUS_MUXER_HEADER.exists())
        self.assertTrue(MEMORY_WATCH_OGG_OPUS_MUXER_SOURCE.exists())

    def test_muxer_exposes_callback_driven_api(self) -> None:
        header = MEMORY_WATCH_OGG_OPUS_MUXER_HEADER.read_text(encoding="utf-8")

        self.assertIn("memory_watch_ogg_write_cb_t", header)
        self.assertIn("memory_watch_ogg_opus_muxer_config_t", header)
        self.assertIn("serial", header)
        self.assertIn("input_sample_rate_hz", header)
        self.assertIn("pre_skip_samples", header)
        self.assertIn("frame_duration_ms", header)
        self.assertIn("channel_count", header)
        self.assertIn("memory_watch_ogg_opus_muxer_init", header)
        self.assertIn("memory_watch_ogg_opus_muxer_write_headers", header)
        self.assertIn("memory_watch_ogg_opus_muxer_write_audio_packet", header)

    def test_muxer_writes_ogg_opus_headers_crc_and_audio_pages(self) -> None:
        source = MEMORY_WATCH_OGG_OPUS_MUXER_SOURCE.read_text(encoding="utf-8")
        header = MEMORY_WATCH_OGG_OPUS_MUXER_HEADER.read_text(encoding="utf-8")

        self.assertIn('MEMORY_WATCH_OGG_CAPTURE_PATTERN "OggS"', source)
        self.assertIn('memcpy(packet, "OpusHead", 8U)', source)
        self.assertIn('memcpy(packet, "OpusTags", 8U)', source)
        self.assertIn("MEMORY_WATCH_OGG_CRC_POLY 0x04c11db7U", source)
        self.assertIn("memory_watch_ogg_crc_update", source)
        self.assertIn("MEMORY_WATCH_OGG_FLAG_BOS", source)
        self.assertIn("MEMORY_WATCH_OGG_FLAG_EOS", source)
        self.assertIn("MEMORY_WATCH_OGG_SEGMENT_BYTES 255U", source)
        self.assertIn("MEMORY_WATCH_OGG_OPUS_MUXER_MAX_PACKET_BYTES", header)
        self.assertIn("packet_size > MEMORY_WATCH_OGG_OPUS_MUXER_MAX_PACKET_BYTES", source)
        self.assertIn("segment_count > MEMORY_WATCH_OGG_MAX_SEGMENTS", source)
        self.assertIn("MEMORY_WATCH_OPUS_GRANULE_HZ 48000U", source)
        self.assertIn("muxer->config.frame_duration_ms", source)

    def test_muxer_fails_closed_after_partial_write_error(self) -> None:
        source = MEMORY_WATCH_OGG_OPUS_MUXER_SOURCE.read_text(encoding="utf-8")
        header = MEMORY_WATCH_OGG_OPUS_MUXER_HEADER.read_text(encoding="utf-8")

        self.assertIn("bool failed", header)
        self.assertIn("memory_watch_ogg_mark_failed", source)
        self.assertIn("muxer->failed = true", source)
        self.assertIn("if (muxer->failed || muxer->finished)", source)
        self.assertIn("const uint64_t next_granule_position", source)
        self.assertIn("muxer->granule_position = next_granule_position", source)

    def test_muxer_has_no_official_chat_dependency_or_dynamic_allocation(self) -> None:
        source = MEMORY_WATCH_OGG_OPUS_MUXER_SOURCE.read_text(encoding="utf-8")
        header = MEMORY_WATCH_OGG_OPUS_MUXER_HEADER.read_text(encoding="utf-8")

        combined = source + "\n" + header
        self.assertNotIn("official_chat", combined)
        self.assertNotIn("malloc", combined)
        self.assertNotIn("calloc", combined)
        self.assertNotIn("realloc", combined)
        self.assertNotIn("free(", combined)
        self.assertNotIn("esp_http_client", combined)
        self.assertNotIn("audio_codec_read", combined)

    def test_main_cmake_registers_muxer(self) -> None:
        assert_main_source_globbed(self, "services/memory_watch/memory_watch_ogg_opus_muxer.c")


if __name__ == "__main__":
    unittest.main()
