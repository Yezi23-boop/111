#pragma once
#include "esp_err.h"
typedef struct {
    int hour;
    int min;
    int sec;
} system_time_local_t;
esp_err_t system_time_get_local_time(system_time_local_t *t);
