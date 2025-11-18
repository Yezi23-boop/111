idf_component_register(
    SRCS "audio_codec.c"
    INCLUDE_DIRS "include"
    REQUIRES 
        driver
        esp_codec_dev
        i2c_manager
)
