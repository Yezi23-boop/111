file(GLOB_RECURSE main_common_srcs
    ${CMAKE_CURRENT_LIST_DIR}/*.c
)

set(official_chat_service_src ${CMAKE_CURRENT_LIST_DIR}/official_chat_service.c)
list(FIND main_common_srcs ${official_chat_service_src} official_chat_service_index)
if(official_chat_service_index EQUAL -1)
    list(APPEND main_common_srcs ${official_chat_service_src})
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
        wifi_provision
        official_chat
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
