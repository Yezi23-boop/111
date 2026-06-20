cmake_minimum_required(VERSION 3.20)

project(agent_preview_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(LVGL_DIR "${REPO_ROOT}/managed_components/lvgl__lvgl")

set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)

add_subdirectory("${LVGL_DIR}" "${CMAKE_BINARY_DIR}/lvgl")

find_package(SDL2 REQUIRED)

file(GLOB GENERATED_SRCS "${CMAKE_CURRENT_LIST_DIR}/../../generated/*.c")
file(GLOB GENERATED_FONTS "${CMAKE_CURRENT_LIST_DIR}/../../generated/guider_fonts/*.c")
file(GLOB GENERATED_CUSTOMER_FONTS "${CMAKE_CURRENT_LIST_DIR}/../../generated/guider_customer_fonts/*.c")
file(GLOB GENERATED_IMAGES "${CMAKE_CURRENT_LIST_DIR}/../../generated/images/*.c")
file(GLOB CUSTOM_SRCS "${CMAKE_CURRENT_LIST_DIR}/../../custom/*.c" "${CMAKE_CURRENT_LIST_DIR}/../../ui_refresh_policy.c")
file(GLOB CUSTOM_FONTS "${CMAKE_CURRENT_LIST_DIR}/../../custom/fonts/*.c")
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
)

target_include_directories(agent_preview_host PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/../../generated"
    "${CMAKE_CURRENT_LIST_DIR}/mock"
    "${LVGL_DIR}/src"
    "${CMAKE_CURRENT_LIST_DIR}/../../custom"
    "${CMAKE_CURRENT_LIST_DIR}/../../"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../main"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../main/ui"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../main/ui/custom"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../main/features"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../main/features/alerts"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../main/services"
)

target_link_libraries(agent_preview_host PRIVATE lvgl SDL2::SDL2)

if(WIN32)
    add_custom_command(TARGET agent_preview_host POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SDL2::SDL2>"
            "$<TARGET_FILE_DIR:agent_preview_host>"
    )
endif()
