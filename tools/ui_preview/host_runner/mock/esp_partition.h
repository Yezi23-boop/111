#pragma once
#include <stdint.h>
#include "esp_err.h"
#define ESP_ERR_INVALID_CRC -8
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_ANY 0
#define ESP_PARTITION_MMAP_DATA 0
typedef uint32_t esp_partition_mmap_handle_t;
typedef struct {
    uint32_t address;
    uint32_t size;
    char label[17];
} esp_partition_t;
const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label);
esp_err_t esp_partition_mmap(const esp_partition_t *partition, uint32_t offset, uint32_t size, int memory_type, const void** out_ptr, esp_partition_mmap_handle_t* out_handle);
void esp_partition_munmap(esp_partition_mmap_handle_t handle);
