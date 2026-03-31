#pragma once

#include "driver/i2s_std.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_codec_bus_init(void);
esp_err_t audio_codec_bus_deinit(void);

i2s_chan_handle_t audio_codec_bus_get_tx_handle(void);
i2s_chan_handle_t audio_codec_bus_get_rx_handle(void);
i2s_port_t audio_codec_bus_get_port(void);

#ifdef __cplusplus
}
#endif
