# ===========================================================
# PCB 通讯测试模式（测试分支）
# 还原方法：把 app_main_test.c 换回 app_main.c，
#           取消下方 service_srcs / feature_srcs / ui 相关行的注释。
# ===========================================================
set(app_srcs

    # [测试模式] 使用测试入口，去掉 LVGL/UI/业务服务依赖
    ${CMAKE_CURRENT_LIST_DIR}/app/app_main_test.c

    # app_main.c 暂时注释掉，测试完成后换回
    # ${CMAKE_CURRENT_LIST_DIR}/app/app_main.c

    # hardware_init / board_* 暂时不需要（测试入口自己管各模块初始化）
    # ${CMAKE_CURRENT_LIST_DIR}/app/hardware_init.c
    # ${CMAKE_CURRENT_LIST_DIR}/app/board_button.c
    # ${CMAKE_CURRENT_LIST_DIR}/app/board_power.c
    # ${CMAKE_CURRENT_LIST_DIR}/app/board_imu.c
    # ${CMAKE_CURRENT_LIST_DIR}/app/resource_fs.c

)



# [测试模式] service_srcs 全部注释掉——测试固件不启动任何后台服务
set(service_srcs
    # ${CMAKE_CURRENT_LIST_DIR}/services/network_service.c
    # ${CMAKE_CURRENT_LIST_DIR}/services/official_chat_service.c
    # ${CMAKE_CURRENT_LIST_DIR}/services/memory_watch_service.c
    # ${CMAKE_CURRENT_LIST_DIR}/services/memory_watch_ogg_opus_muxer.c
    # ${CMAKE_CURRENT_LIST_DIR}/services/memory_watch_recorder.c
    # ${CMAKE_CURRENT_LIST_DIR}/services/memory_watch_voice_client.c
    # ${CMAKE_CURRENT_LIST_DIR}/services/power_service.c
    # ${CMAKE_CURRENT_LIST_DIR}/services/power_policy.c
    # ${CMAKE_CURRENT_LIST_DIR}/services/sleep_coordinator.c
    # ${CMAKE_CURRENT_LIST_DIR}/services/wakeup_evidence_service.c
    # ${CMAKE_CURRENT_LIST_DIR}/services/system_time_service.c
    # ${CMAKE_CURRENT_LIST_DIR}/services/imu_service.c
    # ${CMAKE_CURRENT_LIST_DIR}/services/startup_readiness.c
    # ${CMAKE_CURRENT_LIST_DIR}/services/safety_monitor_session.c
    # ${CMAKE_CURRENT_LIST_DIR}/services/background_service_manager.c
)



# [测试模式] feature_srcs 全部注释掉——测试固件不需要业务功能
set(feature_srcs
    # ${CMAKE_CURRENT_LIST_DIR}/features/alerts/app_alert_manager.c
    # ${CMAKE_CURRENT_LIST_DIR}/features/alerts/audio_alert_player.c
    # ${CMAKE_CURRENT_LIST_DIR}/features/alerts/display_alert_adapter.c
    # ${CMAKE_CURRENT_LIST_DIR}/features/audio/audio_app.c
    # ${CMAKE_CURRENT_LIST_DIR}/features/danger_detection/danger_detection_service.c
    # ${CMAKE_CURRENT_LIST_DIR}/features/mini_games/mini_game_2048.c
    # ${CMAKE_CURRENT_LIST_DIR}/features/mini_games/mini_game_flappy.c
    # ${CMAKE_CURRENT_LIST_DIR}/features/mini_games/mini_game_dino.c
    # ${CMAKE_CURRENT_LIST_DIR}/features/weather/hptts.c
    # ${CMAKE_CURRENT_LIST_DIR}/features/weather/time_weather.c
)



# [测试模式] UI 运行时全部注释掉——去掉 LVGL 依赖
set(ui_runtime_srcs
    # ${CMAKE_CURRENT_LIST_DIR}/ui/lvgl_task.c
    # ${CMAKE_CURRENT_LIST_DIR}/ui/ui_refresh_policy.c
    # ${CMAKE_CURRENT_LIST_DIR}/ui/custom/ai_chat_view.c
    # ${CMAKE_CURRENT_LIST_DIR}/ui/custom/ai_ui_controller.c
    # ${CMAKE_CURRENT_LIST_DIR}/ui/custom/cbin_font_bridge.c
    # ${CMAKE_CURRENT_LIST_DIR}/ui/custom/clock_functions.c
    # ${CMAKE_CURRENT_LIST_DIR}/ui/custom/custom.c
    # ${CMAKE_CURRENT_LIST_DIR}/ui/custom/danger_detection_controller.c
    # ${CMAKE_CURRENT_LIST_DIR}/ui/custom/danger_detection_view.c
    # ${CMAKE_CURRENT_LIST_DIR}/ui/custom/main_dropdown_controller.c
    # ${CMAKE_CURRENT_LIST_DIR}/ui/custom/memory_watch_controller.c
    # ${CMAKE_CURRENT_LIST_DIR}/ui/custom/memory_watch_view.c
    # ${CMAKE_CURRENT_LIST_DIR}/ui/custom/mini_games_controller.c
    # ${CMAKE_CURRENT_LIST_DIR}/ui/custom/wifi_management_controller.c
    # ${CMAKE_CURRENT_LIST_DIR}/ui/custom/scroll_functions.c
    # ${CMAKE_CURRENT_LIST_DIR}/ui/custom/ui_font_assets.c
    # ${CMAKE_CURRENT_LIST_DIR}/ui/custom/mini_game_sprites.c
)



# [测试模式] UI 生成文件和字体文件全部排除
set(ui_generated_srcs)
set(ui_custom_font_srcs)



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



# [测试模式] 精简 REQUIRES，只保留通讯测试实际用到的组件
idf_component_register(

    SRCS ${srcs}

    INCLUDE_DIRS ${include_dirs}

    # [测试模式] 去掉字体 EMBED_FILES，字体文件只在完整固件需要
    # EMBED_FILES ${BUILTIN_TEXT_FONT_FILE}

    REQUIRES

        i2c_manager

        co5300_panel

        audio_codec

        sd_card

        axp2101

        pcf85063atl

        qmi8658c

        touch_ft5x06

        ble_control

        bt

        nvs_flash

        esp_wifi


        esp_event

        esp_netif

        esp_system

        driver

        freertos

)

# [测试模式] 字体相关编译定义注释掉，测试固件不嵌入字体文件
# target_compile_definitions(${COMPONENT_LIB} PRIVATE
#     "UI_FONT_ASSETS_BUILTIN_TEXT_NAME=\"${BUILTIN_TEXT_FONT_ID}.bin\""
#     "UI_FONT_ASSETS_BUILTIN_TEXT_START_SYMBOL=\"_binary_${BUILTIN_TEXT_FONT_ID}_bin_start\""
#     "UI_FONT_ASSETS_BUILTIN_TEXT_END_SYMBOL=\"_binary_${BUILTIN_TEXT_FONT_ID}_bin_end\""
# )


target_compile_options(${COMPONENT_LIB} PRIVATE -Os)

