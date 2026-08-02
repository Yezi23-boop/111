#pragma once

#include "esp_err.h"

esp_err_t audio_codec_set_volume(int volume);
esp_err_t audio_codec_set_volume_preference(int volume);
esp_err_t audio_codec_get_volume(int *volume);
