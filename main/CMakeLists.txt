file(GLOB_RECURSE main_common_srcs
    ${CMAKE_CURRENT_LIST_DIR}/*.c
)

set(official_chat_service_src ${CMAKE_CURRENT_LIST_DIR}/official_chat_service.c)
list(FIND main_common_srcs ${official_chat_service_src} official_chat_service_index)
if(official_chat_service_index EQUAL -1)
    list(APPEND main_common_srcs ${official_chat_service_src})
endif()

set(ui_font_assets_src ${CMAKE_CURRENT_LIST_DIR}/ui/custom/ui_font_assets.c)
list(FIND main_common_srcs ${ui_font_assets_src} ui_font_assets_index)
if(ui_font_assets_index EQUAL -1)
    list(APPEND main_common_srcs ${ui_font_assets_src})
endif()

set(cbin_font_bridge_src ${CMAKE_CURRENT_LIST_DIR}/ui/custom/cbin_font_bridge.c)
list(FIND main_common_srcs ${cbin_font_bridge_src} cbin_font_bridge_index)
if(cbin_font_bridge_index EQUAL -1)
    list(APPEND main_common_srcs ${cbin_font_bridge_src})
endif()

set(ai_chat_view_src ${CMAKE_CURRENT_LIST_DIR}/ui/custom/ai_chat_view.c)
list(FIND main_common_srcs ${ai_chat_view_src} ai_chat_view_index)
if(ai_chat_view_index EQUAL -1)
    list(APPEND main_common_srcs ${ai_chat_view_src})
endif()

set(danger_detection_service_src ${CMAKE_CURRENT_LIST_DIR}/danger_detection_service.c)
list(FIND main_common_srcs ${danger_detection_service_src} danger_detection_service_index)
if(danger_detection_service_index EQUAL -1)
    list(APPEND main_common_srcs ${danger_detection_service_src})
endif()

set(app_alert_manager_src ${CMAKE_CURRENT_LIST_DIR}/app_alert_manager.c)
list(FIND main_common_srcs ${app_alert_manager_src} app_alert_manager_index)
if(app_alert_manager_index EQUAL -1)
    list(APPEND main_common_srcs ${app_alert_manager_src})
endif()

set(audio_alert_player_src ${CMAKE_CURRENT_LIST_DIR}/audio_alert_player.c)
list(FIND main_common_srcs ${audio_alert_player_src} audio_alert_player_index)
if(audio_alert_player_index EQUAL -1)
    list(APPEND main_common_srcs ${audio_alert_player_src})
endif()

set(display_alert_adapter_src ${CMAKE_CURRENT_LIST_DIR}/display_alert_adapter.c)
list(FIND main_common_srcs ${display_alert_adapter_src} display_alert_adapter_index)
if(display_alert_adapter_index EQUAL -1)
    list(APPEND main_common_srcs ${display_alert_adapter_src})
endif()

set(danger_detection_view_src ${CMAKE_CURRENT_LIST_DIR}/ui/custom/danger_detection_view.c)
list(FIND main_common_srcs ${danger_detection_view_src} danger_detection_view_index)
if(danger_detection_view_index EQUAL -1)
    list(APPEND main_common_srcs ${danger_detection_view_src})
endif()

set(danger_detection_controller_src ${CMAKE_CURRENT_LIST_DIR}/ui/custom/danger_detection_controller.c)
list(FIND main_common_srcs ${danger_detection_controller_src} danger_detection_controller_index)
if(danger_detection_controller_index EQUAL -1)
    list(APPEND main_common_srcs ${danger_detection_controller_src})
endif()

list(REMOVE_ITEM main_common_srcs
    ${CMAKE_CURRENT_LIST_DIR}/111.c
    ${CMAKE_CURRENT_LIST_DIR}/main_ai_chat_experiment.c
)

set(app_entry_src ${CMAKE_CURRENT_LIST_DIR}/111.c)
if(CONFIG_APP_AI_CHAT_EXPERIMENT)
    set(app_entry_src ${CMAKE_CURRENT_LIST_DIR}/main_ai_chat_experiment.c)
endif()

set(srcs ${main_common_srcs} ${app_entry_src})

set(include_dirs
    .
    ui/custom
    ui/generated
)

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS ${include_dirs}
    REQUIRES
        lvgl_port
        z_print_esp32
        get_time
        sd_card
        audio_codec
        mp3_player
        traffic_inference
        wifi_provision
        official_chat
        78__xiaozhi_fonts
        chmorgan__esp-audio-player
        nvs_flash
        esp_wifi
        esp_event
        esp_http_client
        esp_netif
        esp_system
        esp-tls
        json
        driver
        freertos
        spiffs
)
