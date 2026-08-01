file(GLOB app_srcs

    ${CMAKE_CURRENT_LIST_DIR}/app/*.c

)



file(GLOB_RECURSE service_srcs

    ${CMAKE_CURRENT_LIST_DIR}/services/*.c
    ${CMAKE_CURRENT_LIST_DIR}/services/*.cc

)



file(GLOB_RECURSE feature_srcs

    ${CMAKE_CURRENT_LIST_DIR}/features/*.c

)



file(GLOB ui_runtime_srcs

    ${CMAKE_CURRENT_LIST_DIR}/ui/*.c
    ${CMAKE_CURRENT_LIST_DIR}/ui/custom/*.c

)



file(GLOB_RECURSE ui_generated_srcs

    ${CMAKE_CURRENT_LIST_DIR}/ui/generated/*.c

)

file(GLOB_RECURSE ui_custom_font_srcs

    ${CMAKE_CURRENT_LIST_DIR}/ui/custom/fonts/*.c

)



set(srcs

    ${app_srcs}

    ${service_srcs}

    ${feature_srcs}

    ${ui_runtime_srcs}

    ${ui_generated_srcs}

    ${ui_custom_font_srcs}

)



set(include_dirs

    .

    ui

    ui/custom

    ui/generated

)

set(BUILTIN_TEXT_FONT font_puhui_common_20_4 CACHE STRING "内置到固件的 xiaozhi cbin 文本字体")
set_property(CACHE BUILTIN_TEXT_FONT PROPERTY STRINGS
    font_puhui_common_14_1
    font_puhui_common_16_4
    font_puhui_common_20_4
    font_puhui_common_30_4
    font_noto_qwen_14_1
    font_noto_qwen_16_4
    font_noto_qwen_20_4
    font_noto_qwen_30_4
)
string(REGEX REPLACE "\\.bin$" "" BUILTIN_TEXT_FONT_ID "${BUILTIN_TEXT_FONT}")
set(BUILTIN_TEXT_FONT_FILE
    ${CMAKE_CURRENT_LIST_DIR}/../managed_components/78__xiaozhi-fonts/cbin/${BUILTIN_TEXT_FONT_ID}.bin
)
if(NOT EXISTS ${BUILTIN_TEXT_FONT_FILE})
    message(FATAL_ERROR "BUILTIN_TEXT_FONT not found: ${BUILTIN_TEXT_FONT_FILE}")
endif()
message(STATUS "Builtin text font: ${BUILTIN_TEXT_FONT_ID}.bin")



idf_component_register(

    SRCS ${srcs}

    INCLUDE_DIRS ${include_dirs}

    EMBED_FILES ${BUILTIN_TEXT_FONT_FILE}

    REQUIRES

        lvgl_port

        z_print_esp32

        system_time

        sd_card

        audio_codec

        mp3_player

        espdl_inference
        fall_detection_inference

        network_manager

        wifi_control

        ap_portal_adapter

        official_chat

        axp2101

        ds2413

        espressif__onewire_bus

        pcf85063atl

        imu_sensor

        78__xiaozhi-fonts

        chmorgan__esp-audio-player

        espressif__esp_audio_codec

        nvs_flash

        esp_wifi

        esp_event

        esp_http_client

        esp_netif

        esp_system

        esp-tls

        esp_https_ota

        mbedtls

        json

        driver

        freertos

        spiffs

        joltwallet__littlefs

)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    "UI_FONT_ASSETS_BUILTIN_TEXT_NAME=\"${BUILTIN_TEXT_FONT_ID}.bin\""
    "UI_FONT_ASSETS_BUILTIN_TEXT_START_SYMBOL=\"_binary_${BUILTIN_TEXT_FONT_ID}_bin_start\""
    "UI_FONT_ASSETS_BUILTIN_TEXT_END_SYMBOL=\"_binary_${BUILTIN_TEXT_FONT_ID}_bin_end\""
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Os)

