idf_component_register(
    SRCS "lv_port.c"
    INCLUDE_DIRS "."
    REQUIRES co5300_panel touch_ft5x06 esp_lcd esp_timer heap freertos lvgl
)
