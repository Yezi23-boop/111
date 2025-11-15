idf_component_register(
    SRCS "touch_ft5x06.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd_touch esp_lcd_touch_ft5x06 driver co5300_panel
)
