#pragma once
#include "esp_err.h"
typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int min;
    int sec;
    char time_str[64];
} system_time_local_t;
esp_err_t system_time_get_local_time(system_time_local_t *t);

