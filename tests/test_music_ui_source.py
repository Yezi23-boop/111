from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_music_view_is_ui_only_and_uses_safe_zone():
    source = (ROOT / "main/ui/custom/music_view.c").read_text(encoding="utf-8")
    assert "music_service_start" not in source
    assert "music_service_toggle_playback" not in source
    assert "lv_obj_set_pos(view->track_panel, 40, 78);" in source
    assert "music_view_set_track_layout(view, true);" in source
    assert "lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER" in source
    assert "lv_obj_get_child(view->track_buttons[i], 0)," in source
    assert "lv_obj_get_child(view->track_buttons[i], 1)," in source
    assert "lv_obj_get_child(view->track_buttons[i], 2)," in source
    assert "LV_TEXT_ALIGN_LEFT" in source
    assert "name_label, &lv_font_montserrat_lxgw_common_5500_16_4" in source
    assert "view->track_label, &lv_font_montserrat_lxgw_common_5500_22_4" in source
    assert "&lv_font_montserrat_lxgw_common_5500_16_4);" in source
    assert "&lv_font_montserrat_lxgw_music_ui_13_4" not in source
    assert "lv_obj_set_pos(view->catalog_list, 40, 280);" in source
    assert "lv_obj_set_size(view->catalog_list, 330, 202);" in source
    assert 'lv_label_set_text(view->section_kicker, "COLLECTION");' in source
    assert "music_view_source_label(source_id)" in source
    assert "catalog_back_button" not in source
    assert "view->catalog_visible && view->config.catalog_back_cb != NULL" in source


def test_music_controller_routes_commands_to_service_owner():
    source = (ROOT / "main/ui/custom/music_controller.c").read_text(encoding="utf-8")
    assert "music_service_start" in source
    assert "music_service_toggle_playback" in source
    assert "music_service_set_mode" in source
    assert "esp_http_client" not in source
    assert "music_view_apply_catalog" in source
    assert "music_view_show_catalog_loading" in source
    assert "catalog_load_more_cb" in source
    assert "s_catalog_copy = heap_caps_calloc" in source
    assert "MALLOC_CAP_SPIRAM" in source
    assert "music_view_show_sources(s_view);" in source


def test_music_catalog_uses_bounded_scroll_loading():
    source = (ROOT / "main/ui/custom/music_view.c").read_text(encoding="utf-8")
    assert "MUSIC_VIEW_CATALOG_CACHE_CAPACITY (MUSIC_SERVICE_CATALOG_PAGE_SIZE * 3U)" in source
    assert "lv_obj_set_scroll_dir(view->catalog_list, LV_DIR_VER);" in source
    assert "LV_EVENT_SCROLL_END" in source
    assert "lv_obj_get_scroll_bottom" in source
    assert "catalog_load_more_cb" in source
    assert "memmove(view->catalog_tracks" in source
    assert '"上一页"' not in source
    assert '"下一页"' not in source


def test_music_controller_loads_back_screen_before_destroying_active_view():
    source = (ROOT / "main/ui/custom/music_controller.c").read_text(encoding="utf-8")
    back = source.split("static void music_controller_back", 1)[1].split(
        "static void music_controller_start_source", 1
    )[0]
    assert "lv_screen_load(screen);" in back
    assert back.index("lv_screen_load(screen);") < back.index("music_view_destroy(s_view);")
    assert "lv_screen_load_anim" not in back


def test_dropdown_music_button_replaces_slider_and_uses_requested_gestures():
    setup = (ROOT / "main/ui/generated/setup_scr_screen_main.c").read_text(
        encoding="utf-8"
    )
    controller = (ROOT / "main/ui/custom/main_dropdown_controller.c").read_text(
        encoding="utf-8"
    )
    event = controller.split("static void main_dropdown_controller_music_event", 1)[
        1
    ].split("static lv_obj_t *main_dropdown_controller_get_wifi_button", 1)[0]

    assert "screen_main_slider_1" not in setup
    assert "screen_main_music_button = lv_button_create" in setup
    assert "lv_obj_set_pos(ui->screen_main_music_button, 53, 145);" in setup
    assert "music_controller_open();" in event
    assert "music_service_destroy" not in event
    assert 'music_service_start_source("today")' in event
    assert "music_service_toggle_playback();" in event
    assert "s_ui->screen_main_music_button" in controller
    assert "s_ui->screen_main_imgbtn_1" not in event
