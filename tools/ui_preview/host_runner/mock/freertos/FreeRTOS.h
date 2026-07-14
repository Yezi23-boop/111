#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef void * TaskHandle_t;
typedef void * QueueHandle_t;
typedef void * EventGroupHandle_t;
typedef void * SemaphoreHandle_t;
typedef uint32_t TickType_t;
typedef uint32_t BaseType_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY 0xFFFFFFFF
#define portTICK_PERIOD_MS 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
