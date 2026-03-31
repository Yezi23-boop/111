#include "ui_font_assets.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cJSON.h"
#include "cbin_font_bridge.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "gui_guider.h"

static const char *TAG = "ui_font_assets";
// 这里通过 cbin_font_bridge_create 间接使用上游的 cbin_font_create。

enum {
    UI_FONT_ASSETS_NAME_LEN = 32,
    UI_FONT_ASSETS_ENTRY_SIZE = 44,
    UI_FONT_ASSETS_HEADER_SIZE = 12,
};

typedef struct {
    char name[UI_FONT_ASSETS_NAME_LEN + 1];
    uint32_t size;
    uint32_t offset;
    uint16_t width;
    uint16_t height;
} ui_font_assets_entry_t;

typedef struct {
    bool init_done;
    bool ready;
    esp_err_t init_error;
    const esp_partition_t *partition;
    esp_partition_mmap_handle_t mmap_handle;
    const uint8_t *mmap_root;
    lv_font_t *title_font;
    lv_font_t *body_font;
    lv_font_t *meta_font;
    lv_font_t *icon_font;
} ui_font_assets_runtime_t;

static ui_font_assets_runtime_t s_runtime = {0};

static uint32_t ui_font_assets_read_u32(const uint8_t *data) {
    return ((uint32_t)data[0]) | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool ui_font_assets_read_entry(const uint8_t *table, size_t index,
                                      ui_font_assets_entry_t *entry) {
    const uint8_t *cursor = table + (index * UI_FONT_ASSETS_ENTRY_SIZE);
    memcpy(entry->name, cursor, UI_FONT_ASSETS_NAME_LEN);
    entry->name[UI_FONT_ASSETS_NAME_LEN] = '\0';
    entry->size = ui_font_assets_read_u32(cursor + UI_FONT_ASSETS_NAME_LEN);
    entry->offset = ui_font_assets_read_u32(cursor + UI_FONT_ASSETS_NAME_LEN + 4);
    entry->width = (uint16_t)(cursor[UI_FONT_ASSETS_NAME_LEN + 8] |
                              ((uint16_t)cursor[UI_FONT_ASSETS_NAME_LEN + 9] << 8));
    entry->height = (uint16_t)(cursor[UI_FONT_ASSETS_NAME_LEN + 10] |
                               ((uint16_t)cursor[UI_FONT_ASSETS_NAME_LEN + 11] << 8));
    return true;
}

static bool ui_font_assets_entry_matches(const ui_font_assets_entry_t *entry,
                                         const char *name) {
    return strncmp(entry->name, name, UI_FONT_ASSETS_NAME_LEN) == 0;
}

static bool ui_font_assets_find_entry(const uint8_t *table, size_t total_files,
                                      const char *name,
                                      ui_font_assets_entry_t *entry) {
    for (size_t i = 0; i < total_files; ++i) {
        ui_font_assets_entry_t candidate = {0};
        ui_font_assets_read_entry(table, i, &candidate);
        if (ui_font_assets_entry_matches(&candidate, name)) {
            if (entry != NULL) {
                *entry = candidate;
            }
            return true;
        }
    }
    return 0;
}

static const uint8_t *ui_font_assets_entry_data(
    const uint8_t *combined_data, size_t combined_len,
    const ui_font_assets_entry_t *entry) {
    if (entry->offset > combined_len) {
        return NULL;
    }
    if (entry->size > combined_len - entry->offset) {
        return NULL;
    }
    if (entry->size < 2) {
        return NULL;
    }
    if (entry->offset + 2 > combined_len) {
        return NULL;
    }
    if (entry->size > combined_len - entry->offset - 2) {
        return NULL;
    }
    return combined_data + entry->offset + 2;
}

static void ui_font_assets_destroy_font(lv_font_t **font) {
    if (font != NULL && *font != NULL) {
        cbin_font_bridge_destroy(*font);
        *font = NULL;
    }
}

static void ui_font_assets_reset_runtime(void) {
    if (s_runtime.icon_font != NULL && s_runtime.icon_font != s_runtime.title_font) {
        cbin_font_bridge_destroy(s_runtime.icon_font);
    }
    s_runtime.icon_font = NULL;

    ui_font_assets_destroy_font(&s_runtime.title_font);
    s_runtime.body_font = NULL;
    s_runtime.meta_font = NULL;

    if (s_runtime.mmap_root != NULL) {
        esp_partition_munmap(s_runtime.mmap_handle);
    }
    s_runtime.mmap_root = NULL;
    s_runtime.mmap_handle = 0;
    s_runtime.partition = NULL;
    s_runtime.ready = false;
}

static esp_err_t ui_font_assets_load_from_partition(void) {
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "assets");
    if (partition == NULL) {
        ESP_LOGW(TAG, "assets partition not found, fallback to compiled fonts");
        return ESP_ERR_NOT_FOUND;
    }

    const void *mapped_root = NULL;
    esp_partition_mmap_handle_t mmap_handle = 0;
    esp_err_t err = esp_partition_mmap(partition, 0, partition->size,
                                       ESP_PARTITION_MMAP_DATA, &mapped_root,
                                       &mmap_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_partition_mmap failed for assets: %s",
                 esp_err_to_name(err));
        return err;
    }

    s_runtime.partition = partition;
    s_runtime.mmap_handle = mmap_handle;
    s_runtime.mmap_root = (const uint8_t *)mapped_root;

    const uint8_t *root = s_runtime.mmap_root;
    const uint32_t total_files = ui_font_assets_read_u32(root);
    const uint32_t checksum = ui_font_assets_read_u32(root + 4);
    const uint32_t combined_len = ui_font_assets_read_u32(root + 8);
    const size_t table_len = (size_t)total_files * UI_FONT_ASSETS_ENTRY_SIZE;
    const size_t image_len = UI_FONT_ASSETS_HEADER_SIZE + table_len + combined_len;

    if (total_files == 0 || image_len > partition->size) {
        ESP_LOGE(TAG, "invalid assets package: files=%" PRIu32 ", combined=%" PRIu32,
                 total_files, combined_len);
        return ESP_ERR_INVALID_SIZE;
    }

    const uint8_t *table = root + UI_FONT_ASSETS_HEADER_SIZE;
    const uint8_t *combined_data = table + table_len;

    ESP_LOGI(TAG,
             "assets image loaded: files=%" PRIu32 ", checksum=0x%08" PRIx32
             ", combined=%" PRIu32,
             total_files, checksum, combined_len);

    ui_font_assets_entry_t index_entry = {0};
    if (!ui_font_assets_find_entry(table, total_files, "index.json", &index_entry)) {
        ESP_LOGE(TAG, "index.json not found in assets package");
        return ESP_ERR_NOT_FOUND;
    }

    const uint8_t *index_data =
        ui_font_assets_entry_data(combined_data, combined_len, &index_entry);
    if (index_data == NULL) {
        ESP_LOGE(TAG, "index.json entry is out of range");
        return ESP_ERR_INVALID_SIZE;
    }

    cJSON *index_root = cJSON_ParseWithLength((const char *)index_data, index_entry.size);
    if (index_root == NULL) {
        ESP_LOGE(TAG, "failed to parse index.json");
        return ESP_FAIL;
    }

    cJSON *text_item = cJSON_GetObjectItem(index_root, "text_font");
    if (!cJSON_IsString(text_item) || text_item->valuestring == NULL ||
        text_item->valuestring[0] == '\0') {
        cJSON_Delete(index_root);
        ESP_LOGE(TAG, "index.json missing text_font");
        return ESP_ERR_INVALID_ARG;
    }
    const char *text_font_name = text_item->valuestring;

    ui_font_assets_entry_t text_entry = {0};
    if (!ui_font_assets_find_entry(table, total_files, text_font_name, &text_entry)) {
        cJSON_Delete(index_root);
        ESP_LOGE(TAG, "text font asset not found: %s", text_font_name);
        return ESP_ERR_NOT_FOUND;
    }

    const uint8_t *text_data =
        ui_font_assets_entry_data(combined_data, combined_len, &text_entry);
    if (text_data == NULL) {
        cJSON_Delete(index_root);
        ESP_LOGE(TAG, "text font asset is out of range: %s",
                 text_font_name);
        return ESP_ERR_INVALID_SIZE;
    }

    s_runtime.title_font = cbin_font_bridge_create((void *)text_data);
    if (s_runtime.title_font == NULL) {
        cJSON_Delete(index_root);
        ESP_LOGE(TAG, "failed to create title font from %s", text_item->valuestring);
        return ESP_ERR_NO_MEM;
    }
    s_runtime.body_font = s_runtime.title_font;
    s_runtime.meta_font = s_runtime.title_font;

    cJSON *icon_item = cJSON_GetObjectItem(index_root, "icon_font");
    if (cJSON_IsString(icon_item) && icon_item->valuestring != NULL &&
        icon_item->valuestring[0] != '\0') {
        ui_font_assets_entry_t icon_entry = {0};
        if (ui_font_assets_find_entry(table, total_files, icon_item->valuestring,
                                      &icon_entry)) {
            const uint8_t *icon_data =
                ui_font_assets_entry_data(combined_data, combined_len, &icon_entry);
            if (icon_data != NULL) {
                s_runtime.icon_font = cbin_font_bridge_create((void *)icon_data);
                if (s_runtime.icon_font == NULL) {
                    ESP_LOGW(TAG, "failed to create icon font from %s, keep fallback",
                             icon_item->valuestring);
                }
            } else {
                ESP_LOGW(TAG, "icon font asset out of range: %s",
                         icon_item->valuestring);
            }
        } else {
            ESP_LOGW(TAG, "icon font asset not found: %s", icon_item->valuestring);
        }
    } else {
        ESP_LOGW(TAG, "index.json missing icon_font, keep compiled fallback");
    }

    cJSON_Delete(index_root);
    s_runtime.ready = true;
    s_runtime.init_error = ESP_OK;
    ESP_LOGI(TAG, "font assets ready: title/body/meta use %s", text_font_name);
    return ESP_OK;
}

esp_err_t ui_font_assets_init(void) {
    if (s_runtime.init_done) {
        return s_runtime.init_error;
    }

    s_runtime.init_done = true;
    s_runtime.init_error = ui_font_assets_load_from_partition();
    if (s_runtime.init_error != ESP_OK) {
        ESP_LOGW(TAG, "font assets fallback to compiled fonts: %s",
                 esp_err_to_name(s_runtime.init_error));
        ui_font_assets_reset_runtime();
    }
    return s_runtime.init_error;
}

bool ui_font_assets_ready(void) {
    if (!s_runtime.init_done) {
        (void)ui_font_assets_init();
    }
    return s_runtime.ready;
}

const lv_font_t *ui_font_assets_title(void) {
    if (!s_runtime.init_done) {
        (void)ui_font_assets_init();
    }
    return s_runtime.ready && s_runtime.title_font != NULL
               ? s_runtime.title_font
               : &lv_font_SourceHanSerifSC_Regular_22;
}

const lv_font_t *ui_font_assets_body(void) {
    if (!s_runtime.init_done) {
        (void)ui_font_assets_init();
    }
    return s_runtime.ready && s_runtime.body_font != NULL ? s_runtime.body_font
                                                          : &lv_font_SourceHanSerifSC_Regular_22;
}

const lv_font_t *ui_font_assets_meta(void) {
    if (!s_runtime.init_done) {
        (void)ui_font_assets_init();
    }
    return s_runtime.ready && s_runtime.meta_font != NULL ? s_runtime.meta_font
                                                          : &lv_font_montserratMedium_16;
}
