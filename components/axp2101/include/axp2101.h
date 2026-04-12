#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    bool vbus_good;
    bool battery_present;
    bool battfet_on;
    bool charging;
    bool discharging;
    uint16_t battery_mv;
    uint16_t vbus_mv;
    uint16_t vsys_mv;
    int8_t battery_percent;
} axp2101_snapshot_t;

typedef struct
{
    uint8_t irq0;
    uint8_t irq1;
    uint8_t irq2;
} axp2101_irq_status_t;

esp_err_t axp2101_init(void);
esp_err_t axp2101_probe(bool *present);
esp_err_t axp2101_read_snapshot(axp2101_snapshot_t *snapshot);
esp_err_t axp2101_read_irq_status(axp2101_irq_status_t *status);
/* Not atomic: each IRQ bank is cleared with a separate RW1C write, so a later
 * bank failure may leave earlier banks already cleared. */
esp_err_t axp2101_clear_irq_status(const axp2101_irq_status_t *status);

#ifdef __cplusplus
}
#endif
