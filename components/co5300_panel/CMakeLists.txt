idf_component_register(
    SRCS "co5300_panel.c"
    INCLUDE_DIRS "include" "."
    REQUIRES driver esp_lcd esp_lcd_co5300 freertos log
)
