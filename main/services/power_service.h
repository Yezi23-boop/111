#ifndef POWER_SERVICE_H
#define POWER_SERVICE_H

#include "app/board_power.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 回调参数和 get_state 返回值都是服务层拥有的只读快照视图，
 * 如需长期持有请自行复制。 */
typedef void (*power_state_changed_cb_t)(const board_power_state_t *state);

esp_err_t power_service_init(void);
esp_err_t power_service_start(void);
void power_service_register_callback(power_state_changed_cb_t cb);
/* 返回服务层拥有的只读快照视图；如需长期持有请自行复制。 */
const board_power_state_t *power_service_get_state(void);

#ifdef __cplusplus
}
#endif

#endif  // POWER_SERVICE_H
