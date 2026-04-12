#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    bool available;
    bool battery_data_valid;
    bool snapshot_stale;
    bool charging;
    bool discharging;
    bool external_power_present;
    bool battery_present;
    uint16_t battery_mv;
    uint16_t system_mv;
    /* Valid only when battery_data_valid is true; otherwise UINT8_MAX. */
    uint8_t battery_percent;
} board_power_state_t;

esp_err_t board_power_init(void);
esp_err_t board_power_refresh(board_power_state_t *state);
const board_power_state_t *board_power_get_cached_state(void);

#ifdef __cplusplus
}
#endif
