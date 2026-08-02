from pathlib import Path


SOURCE = Path("main/services/music/music_stream_decoder.c")
HEADER = Path("main/services/music/music_stream_decoder.h")


def test_music_decoder_preserves_partial_input_contract() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    assert "raw.consumed" in source
    assert "esp_audio_simple_dec_process" in source
    assert "ESP_ERR_INVALID_SIZE" in source


def test_music_decoder_has_no_network_or_ui_dependency() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    assert "esp_http_client" not in source
    assert "lvgl" not in source.lower()
    assert "music_stream_decoder_process" in header
