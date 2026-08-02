from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_dropdown_volume_slider_routes_through_custom_controller():
    generated = (ROOT / "main/ui/generated/setup_scr_screen_main.c").read_text(
        encoding="utf-8"
    )
    controller = (ROOT / "main/ui/custom/main_dropdown_controller.c").read_text(
        encoding="utf-8"
    )

    assert "screen_main_loudness = lv_slider_create" in generated
    assert "lv_slider_set_range(ui->screen_main_loudness, 0, 100);" in generated
    assert "kVolumeApplyIntervalMs = 50U" in controller
    assert "main_dropdown_controller_volume_event" in controller
    assert "LV_EVENT_VALUE_CHANGED" in controller
    assert "LV_EVENT_RELEASED" in controller
    assert "LV_EVENT_PRESS_LOST" in controller
    assert "audio_codec_set_volume(volume)" in controller
    assert "audio_codec_set_volume_preference(volume)" in controller
    assert "main_dropdown_controller_sync_volume();" in controller
    assert "LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CHECKABLE" in controller
    assert "volume == 0" in controller


def test_audio_codec_owns_persistent_volume_preference():
    header = (ROOT / "components/audio_codec/include/audio_codec.h").read_text(
        encoding="utf-8"
    )
    source = (ROOT / "components/audio_codec/audio_codec.c").read_text(
        encoding="utf-8"
    )
    cmake = (ROOT / "components/audio_codec/CMakeLists.txt").read_text(
        encoding="utf-8"
    )

    assert "audio_codec_set_volume_preference(int volume)" in header
    assert 'kVolumeNvsNamespace = "audio_codec"' in source
    assert 'kVolumeNvsKey = "volume"' in source
    assert "kDefaultVolumePercent = 60" in source
    assert "audio_codec_load_volume_preference_locked();" in source
    assert "nvs_get_u8(handle, kVolumeNvsKey" in source
    assert "nvs_set_u8(handle, kVolumeNvsKey" in source
    assert "s_persisted_volume == volume" in source
    assert "audio_codec_lock_resources(UINT32_MAX)" in source
    assert "nvs_flash" in cmake


def test_all_speaker_clients_respect_persistent_system_volume():
    hardware = (ROOT / "main/app/hardware_init.c").read_text(encoding="utf-8")
    chat = (ROOT / "main/services/official_chat_service.c").read_text(
        encoding="utf-8"
    )
    alert = (ROOT / "main/features/alerts/audio_alert_player.c").read_text(
        encoding="utf-8"
    )
    mcp = (ROOT / "components/official_chat/mcp_server.cc").read_text(
        encoding="utf-8"
    )

    assert "audio_codec_set_volume(60)" not in hardware
    assert "audio_codec_get_volume(&speaker_volume)" in chat
    assert ".speak_volume = speaker_volume" in chat
    assert "ALERT_PLAYER_VOLUME_PERCENT" not in alert
    assert "audio_codec_set_volume(" not in alert
    assert "audio_codec_set_volume_preference(volume_value)" in mcp
