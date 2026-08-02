import unittest

from tests.main_paths import REPO_ROOT


MUSIC_DIR = REPO_ROOT / "main" / "services" / "music"


class MusicServiceSourceTests(unittest.TestCase):
    def test_music_service_is_a_queue_owned_background_service(self) -> None:
        header = (MUSIC_DIR / "music_service.h").read_text(encoding="utf-8")
        source = (MUSIC_DIR / "music_service.c").read_text(encoding="utf-8")

        self.assertIn("music_service_init", header)
        self.assertIn("xQueueCreateStatic", source)
        self.assertIn("xTaskCreateWithCaps", source)
        self.assertIn("music_service_get_snapshot", header)
        self.assertIn("music_service_start_source", header)
        self.assertIn("music_service_load_source", header)
        self.assertIn("music_service_get_catalog", header)
        self.assertIn("music_service_start_qr_login", header)
        self.assertIn("music_service_copy_qr", header)
        self.assertIn("kTaskStackBytes = 16384U", source)
        self.assertNotIn("lvgl.h", source)
        self.assertNotIn("esp_http_client", source)

    def test_qr_login_stays_in_service_owner_and_psram(self) -> None:
        http_source = (MUSIC_DIR / "music_http_client.c").read_text(encoding="utf-8")
        service_source = (MUSIC_DIR / "music_service.c").read_text(encoding="utf-8")
        view = (REPO_ROOT / "main/ui/custom/music_view.c").read_text(encoding="utf-8")
        self.assertIn("music_http_client_create_qr", http_source)
        self.assertIn("mbedtls_base64_decode", http_source)
        self.assertIn("MUSIC_SERVICE_QR_MAX_BYTES", service_source)
        self.assertIn("MALLOC_CAP_SPIRAM", service_source)
        self.assertIn("lv_canvas_set_buffer", view)
        self.assertIn("music_service_start_qr_login", REPO_ROOT.joinpath(
            "main/ui/custom/music_controller.c").read_text(encoding="utf-8"))
        self.assertNotIn("esp_http_client", view)

    def test_qr_payload_must_cover_every_module(self) -> None:
        http_source = (MUSIC_DIR / "music_http_client.c").read_text(encoding="utf-8")
        parser = http_source.split("static esp_err_t music_http_parse_account", 1)[1].split(
            "static esp_err_t music_http_request_account", 1
        )[0]

        self.assertIn("qr_required_bytes", http_source)
        self.assertIn("decoded < qr_required_bytes", http_source)
        self.assertNotIn("memset(result", parser)

    def test_catalog_fetch_stays_in_http_client_and_source_click_does_not_play(self) -> None:
        protocol = (MUSIC_DIR / "music_protocol.h").read_text(encoding="utf-8")
        http_source = (MUSIC_DIR / "music_http_client.c").read_text(encoding="utf-8")
        controller = (REPO_ROOT / "main/ui/custom/music_controller.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("#define MUSIC_SERVICE_CATALOG_PAGE_SIZE 10U", protocol)
        self.assertIn("(unsigned)MUSIC_SERVICE_CATALOG_PAGE_SIZE", http_source)
        self.assertIn("music_http_client_fetch_tracks", http_source)
        self.assertIn("/v1/music/sources/%s/tracks", http_source)
        self.assertIn("music_service_load_source(kSourceIds[source_index])", controller)
        self.assertIn("music_service_start(source_id, track_id)", controller)

    def test_stream_player_uses_micro_decoder_and_keeps_audio_owner_boundary(self) -> None:
        header = (MUSIC_DIR / "music_stream_player.h").read_text(encoding="utf-8")
        protocol = (MUSIC_DIR / "music_protocol.h").read_text(encoding="utf-8")
        source = (MUSIC_DIR / "music_stream_player.cc").read_text(encoding="utf-8")
        service = (MUSIC_DIR / "music_service.c").read_text(encoding="utf-8")
        defaults = REPO_ROOT.joinpath("sdkconfig.defaults").read_text(encoding="utf-8")
        engine = REPO_ROOT.joinpath("server/music_service/src/engine.js").read_text(
            encoding="utf-8"
        )
        app = REPO_ROOT.joinpath("server/music_service/src/app.js").read_text(
            encoding="utf-8"
        )

        self.assertIn("MUSIC_SERVICE_RING_BYTES (256U * 1024U)", protocol)
        self.assertNotIn("MUSIC_SERVICE_START_BUFFER_BYTES", protocol)
        self.assertIn("MUSIC_SERVICE_RING_BYTES", source)
        self.assertIn("micro_decoder::DecoderSource", source)
        self.assertIn("kDecoderStackBytes = 16384U", source)
        self.assertIn("decoder_stack_in_psram = true", source)
        self.assertIn("decoder_config.decoder_priority = 5", source)
        self.assertIn("decoder_config.reader_priority = 4", source)
        self.assertIn("music_http_client_build_stream_url", source)
        self.assertIn("audio_codec_write", source)
        self.assertIn("kOpusDecoderSampleRate = 48000U", source)
        self.assertIn("kAudioCodecSampleRate = 24000U", source)
        self.assertIn("heap_caps_malloc(kOpusDownsampleBufferBytes", source)
        self.assertIn("heap_caps_free(opus_downsample_buffer)", source)
        self.assertIn("discard_next_opus_sample", source)
        self.assertIn("output[output_samples++] = input[index]", source)
        self.assertIn("uxTaskGetStackHighWaterMark(nullptr)", source)
        self.assertIn("CONFIG_MICRO_DECODER_CODEC_OPUS=y", defaults)
        self.assertIn("# CONFIG_MICRO_DECODER_CODEC_MP3 is not set", defaults)
        self.assertIn('"libopus"', engine)
        self.assertNotIn('"-re"', engine)
        self.assertIn('"audio/ogg"', app)
        self.assertIn("AUDIO_CODEC_OWNER_MUSIC_PLAYER", source)
        self.assertIn("safety_monitor_policy_set_music_active", source)
        self.assertNotIn("music_stream_reader_task", source)
        self.assertNotIn("music_stream_decoder_task", source)
        self.assertNotIn("xTaskCreateWithCaps", source)
        self.assertIn("music_stream_player_stop", header)
        self.assertIn("music_stream_player_poll", service)

    def test_http_client_uses_bearer_for_control_and_capability_url_for_stream(self) -> None:
        source = (MUSIC_DIR / "music_http_client.c").read_text(encoding="utf-8")

        self.assertIn('"Authorization"', source)
        self.assertIn('"%s%s%s%sdevice_id=%s"', source)
        self.assertIn('"/v1/music/sessions"', source)
        self.assertIn('"/v1/music/streams/', source)
        self.assertIn("music_http_client_build_stream_url", source)
        self.assertNotIn("music_http_client_open_stream", source)
        self.assertIn("allow_insecure_http", source)
        self.assertNotIn("API_SERVER_KEY", source)
        self.assertNotIn("MiMo", source)

    def test_boot_and_hermes_page_use_music_service_boundary(self) -> None:
        app_main = (REPO_ROOT / "main" / "app" / "app_main.c").read_text(
            encoding="utf-8"
        )
        hermes_controller = (
            REPO_ROOT / "main" / "ui" / "custom" /
            "memory_watch_controller.c"
        ).read_text(encoding="utf-8")

        self.assertIn("music_service_init", app_main)
        self.assertIn("music_service_pause_for_hermes_page", hermes_controller)
        self.assertNotIn("esp_http_client", hermes_controller)

    def test_hermes_preemption_is_a_noop_without_active_music(self) -> None:
        source = (MUSIC_DIR / "music_service.c").read_text(encoding="utf-8")
        section = source.split(
            "esp_err_t music_service_pause_for_hermes_page", 1
        )[1]
        self.assertIn("MUSIC_SERVICE_STATE_PLAYING", section)
        self.assertIn("MUSIC_SERVICE_STATE_BUFFERING", section)
        self.assertIn("return ESP_OK", section)



if __name__ == "__main__":
    unittest.main()
