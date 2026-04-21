set(app_srcs

    ${CMAKE_CURRENT_LIST_DIR}/app/app_main.c

    ${CMAKE_CURRENT_LIST_DIR}/app/hardware_init.c

    ${CMAKE_CURRENT_LIST_DIR}/app/board_power.c

)



set(service_srcs

    ${CMAKE_CURRENT_LIST_DIR}/services/network_service.c

    ${CMAKE_CURRENT_LIST_DIR}/services/official_chat_service.c

    ${CMAKE_CURRENT_LIST_DIR}/services/power_service.c

)



set(feature_srcs

    ${CMAKE_CURRENT_LIST_DIR}/features/alerts/app_alert_manager.c

    ${CMAKE_CURRENT_LIST_DIR}/features/alerts/audio_alert_player.c

    ${CMAKE_CURRENT_LIST_DIR}/features/alerts/display_alert_adapter.c

    ${CMAKE_CURRENT_LIST_DIR}/features/audio/audio_app.c

    ${CMAKE_CURRENT_LIST_DIR}/features/danger_detection/danger_detection_service.c

    ${CMAKE_CURRENT_LIST_DIR}/features/weather/hptts.c

    ${CMAKE_CURRENT_LIST_DIR}/features/weather/time_weather.c

)



set(ui_runtime_srcs

    ${CMAKE_CURRENT_LIST_DIR}/ui/lvgl_task.c

    ${CMAKE_CURRENT_LIST_DIR}/ui/ui_refresh_policy.c

    ${CMAKE_CURRENT_LIST_DIR}/ui/custom/ai_chat_view.c

    ${CMAKE_CURRENT_LIST_DIR}/ui/custom/ai_ui_controller.c

    ${CMAKE_CURRENT_LIST_DIR}/ui/custom/cbin_font_bridge.c

    ${CMAKE_CURRENT_LIST_DIR}/ui/custom/clock_functions.c

    ${CMAKE_CURRENT_LIST_DIR}/ui/custom/custom.c

    ${CMAKE_CURRENT_LIST_DIR}/ui/custom/danger_detection_controller.c

    ${CMAKE_CURRENT_LIST_DIR}/ui/custom/danger_detection_view.c

    ${CMAKE_CURRENT_LIST_DIR}/ui/custom/main_dropdown_controller.c

    ${CMAKE_CURRENT_LIST_DIR}/ui/custom/wifi_management_controller.c

    ${CMAKE_CURRENT_LIST_DIR}/ui/custom/scroll_functions.c

    ${CMAKE_CURRENT_LIST_DIR}/ui/custom/ui_font_assets.c

)



file(GLOB_RECURSE ui_generated_srcs

    ${CMAKE_CURRENT_LIST_DIR}/ui/generated/*.c

)



set(srcs

    ${app_srcs}

    ${service_srcs}

    ${feature_srcs}

    ${ui_runtime_srcs}

    ${ui_generated_srcs}

)



set(include_dirs

    .

    ui

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

        network_manager

        ap_portal_adapter

        official_chat

        axp2101

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

