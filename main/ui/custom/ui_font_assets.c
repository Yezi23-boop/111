#include "ui_font_assets.h"

#include <inttypes.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "gui_guider.h"

static const char *TAG = "ui_font_assets";

static bool s_probe_done = false;

static void ui_font_assets_probe_once(void) {
    if (s_probe_done) {
        return;
    }

    s_probe_done = true;

    const esp_partition_t *assets_partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
                                 "assets");
    if (assets_partition == NULL) {
        ESP_LOGW(TAG, "assets partition not found, fallback to compiled fonts");
        return;
    }

    ESP_LOGI(TAG, "assets partition found: size=%" PRIu32,
             (uint32_t)assets_partition->size);
}

esp_err_t ui_font_assets_init(void) {
    ui_font_assets_probe_once();
    return ESP_ERR_NOT_SUPPORTED;
}

bool ui_font_assets_ready(void) {
    ui_font_assets_probe_once();
    return false;
}

const lv_font_t *ui_font_assets_title(void) {
    ui_font_assets_probe_once();
    return &lv_font_SourceHanSerifSC_Regular_22;
}

const lv_font_t *ui_font_assets_body(void) {
    ui_font_assets_probe_once();
    return &lv_font_SourceHanSerifSC_Regular_22;
}

const lv_font_t *ui_font_assets_meta(void) {
    ui_font_assets_probe_once();
    return &lv_font_montserratMedium_16;
}
