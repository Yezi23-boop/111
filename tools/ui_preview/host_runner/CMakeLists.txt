cmake_minimum_required(VERSION 3.20)

project(agent_preview_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(UI_DIR "${REPO_ROOT}/main/ui")
set(LVGL_DIR "${REPO_ROOT}/managed_components/lvgl__lvgl")

set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)

add_subdirectory("${LVGL_DIR}" "${CMAKE_BINARY_DIR}/lvgl")

find_package(SDL2 REQUIRED)

file(GLOB GENERATED_SRCS "main.c" "${UI_DIR}/generated/*.c")
file(GLOB GENERATED_FONTS "${UI_DIR}/generated/guider_fonts/*.c")
file(GLOB GENERATED_CUSTOMER_FONTS "${UI_DIR}/generated/guider_customer_fonts/*.c")
file(GLOB GENERATED_IMAGES "${UI_DIR}/generated/images/*.c")
file(GLOB CUSTOM_SRCS "${UI_DIR}/custom/*.c" "${UI_DIR}/ui_refresh_policy.c")
list(REMOVE_ITEM CUSTOM_SRCS "${UI_DIR}/custom/ui_font_assets.c")
file(GLOB CUSTOM_FONTS "${UI_DIR}/custom/fonts/*.c")
file(GLOB MOCK_SRCS "${CMAKE_CURRENT_LIST_DIR}/mock/*.c")

add_executable(agent_preview_host
    main.c
    ${GENERATED_SRCS}
    ${GENERATED_FONTS}
    ${GENERATED_CUSTOMER_FONTS}
    ${GENERATED_IMAGES}
    ${CUSTOM_SRCS}
    ${CUSTOM_FONTS}
    ${MOCK_SRCS}
    "${REPO_ROOT}/main/features/mini_games/mini_game_2048.c"
    "${REPO_ROOT}/main/features/mini_games/mini_game_flappy.c"
    "${REPO_ROOT}/main/features/mini_games/mini_game_dino.c"
    "${UI_DIR}/custom/mini_game_sprites.c"
)

target_include_directories(agent_preview_host PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${UI_DIR}/generated"
    "${CMAKE_CURRENT_LIST_DIR}/mock"
    "${LVGL_DIR}/src"
    "${UI_DIR}/custom"
    "${UI_DIR}"
    "${REPO_ROOT}/main"
    "${REPO_ROOT}/main/features"
    "${REPO_ROOT}/main/features/alerts"
    "${REPO_ROOT}/main/services"
)

target_link_libraries(agent_preview_host PRIVATE lvgl SDL2::SDL2)

target_compile_definitions(agent_preview_host PRIVATE AGENT_PREVIEW_HOST=1)

if(WIN32)
    add_custom_command(TARGET agent_preview_host POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SDL2::SDL2>"
            "$<TARGET_FILE_DIR:agent_preview_host>"
    )
endif()
