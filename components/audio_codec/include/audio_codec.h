#ifndef AUDIO_CODEC_H
#define AUDIO_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define AUDIO_I2C_SDA_GPIO (15)
#define AUDIO_I2C_SCL_GPIO (14)
#define AUDIO_I2S_ASDOUT_GPIO (40)
#define AUDIO_I2S_LRCK_GPIO (45)
#define AUDIO_I2S_MCLK_GPIO (16)
#define AUDIO_I2S_SCLK_GPIO (41)
#define AUDIO_I2S_DSDIN_GPIO (42)
#define AUDIO_PA_CTRL_GPIO (46)

esp_err_t audio_codec_init(void);
esp_err_t audio_codec_deinit(void);

esp_err_t audio_codec_read(void *buffer, size_t bytes, size_t *bytes_read,
                           TickType_t ticks_to_wait);
esp_err_t audio_codec_write(const void *buffer, size_t bytes);
esp_err_t audio_codec_flush_output(void);

esp_err_t audio_codec_set_volume(int volume);
esp_err_t audio_codec_get_volume(int *volume);
esp_err_t audio_codec_set_mute(bool enable);
esp_err_t audio_codec_set_pa_enable(bool enable);
esp_err_t audio_codec_set_record_gain(float db);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_CODEC_H
