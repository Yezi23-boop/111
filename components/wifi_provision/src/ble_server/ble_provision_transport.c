#include "ble_provision_transport.h"

#include <string.h>

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"

#define BLE_PROVISION_MAX_JSON_LEN 256
#define BLE_PROVISION_RX_FRAME_LEN (BLE_PROVISION_MAX_JSON_LEN + 32)

static const char *TAG = "ble_prov";
void ble_store_config_init(void);

static ble_provision_transport_rx_cb_t s_rx_cb = NULL;
static ble_provision_transport_state_cb_t s_state_cb = NULL;
static void *s_user_data = NULL;

static bool s_initialized = false;
static bool s_synced = false;
static bool s_active = false;
static bool s_connected = false;
static bool s_notify_enabled = false;
static uint8_t s_own_addr_type = BLE_OWN_ADDR_PUBLIC;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_rx_val_handle = 0;
static uint16_t s_tx_val_handle = 0;
static char s_device_name[20] = "ESP32S3";
static char s_last_payload[BLE_PROVISION_MAX_JSON_LEN] =
    "{\"evt\":\"status\",\"state\":\"idle\"}";
static char s_rx_frame_buffer[BLE_PROVISION_RX_FRAME_LEN] = {0};
static size_t s_rx_frame_len = 0;

static const ble_uuid128_t s_service_uuid = BLE_UUID128_INIT(
    0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
    0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02);
static const ble_uuid128_t s_rx_uuid = BLE_UUID128_INIT(
    0xb5, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
    0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02);
static const ble_uuid128_t s_tx_uuid = BLE_UUID128_INIT(
    0xb6, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
    0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02);

static void ble_provision_transport_advertise(void);
static void ble_provision_transport_reset_runtime_state(void);
static void ble_provision_transport_reset_rx_frame(void);
static void ble_provision_transport_consume_rx_chunk(const char *chunk,
                                                     size_t chunk_len);

static void ble_provision_transport_reset_rx_frame(void) {
    s_rx_frame_len = 0;
    s_rx_frame_buffer[0] = '\0';
}

static void ble_provision_transport_dispatch_rx_payload(const char *payload,
                                                        size_t payload_len) {
    char framed_payload[BLE_PROVISION_RX_FRAME_LEN] = {0};

    if (payload == NULL || payload_len == 0 || s_rx_cb == NULL) {
        return;
    }

    while (payload_len > 0 &&
           (payload[payload_len - 1] == '\r' || payload[payload_len - 1] == '\n'
            || payload[payload_len - 1] == '\0')) {
        --payload_len;
    }
    if (payload_len == 0 || payload_len >= sizeof(framed_payload)) {
        return;
    }

    memcpy(framed_payload, payload, payload_len);
    framed_payload[payload_len] = '\0';
    s_rx_cb(framed_payload, payload_len, s_user_data);
}

static void ble_provision_transport_consume_rx_chunk(const char *chunk,
                                                     size_t chunk_len) {
    size_t start = 0;

    if (chunk == NULL || chunk_len == 0) {
        return;
    }

    if (s_rx_frame_len == 0 && chunk[0] == '{' && chunk[chunk_len - 1] == '}') {
        ble_provision_transport_dispatch_rx_payload(chunk, chunk_len);
        return;
    }

    while (start < chunk_len) {
        const char *terminator = memchr(chunk + start, '\n', chunk_len - start);
        size_t part_len = terminator != NULL
                              ? (size_t)(terminator - (chunk + start)) + 1
                              : chunk_len - start;

        if (s_rx_frame_len + part_len >= sizeof(s_rx_frame_buffer)) {
            ESP_LOGW(TAG, "BLE RX frame overflow, drop partial payload");
            ble_provision_transport_reset_rx_frame();
            start += part_len;
            continue;
        }

        memcpy(s_rx_frame_buffer + s_rx_frame_len, chunk + start, part_len);
        s_rx_frame_len += part_len;
        s_rx_frame_buffer[s_rx_frame_len] = '\0';

        if (terminator != NULL) {
            ble_provision_transport_dispatch_rx_payload(s_rx_frame_buffer,
                                                        s_rx_frame_len);
            ble_provision_transport_reset_rx_frame();
        }

        start += part_len;
    }
}

static int ble_provision_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt,
                                   void *arg) {
    uint16_t copy_len = 0;
    char buffer[BLE_PROVISION_MAX_JSON_LEN] = {0};
    int rc = 0;

    (void)arg;
    (void)conn_handle;

    switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_READ_CHR:
            if (attr_handle != s_tx_val_handle) {
                return BLE_ATT_ERR_UNLIKELY;
            }
            rc = os_mbuf_append(ctxt->om, s_last_payload,
                                strlen(s_last_payload));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            if (attr_handle != s_rx_val_handle) {
                return BLE_ATT_ERR_UNLIKELY;
            }
            rc = ble_hs_mbuf_to_flat(ctxt->om, buffer, sizeof(buffer) - 1,
                                     &copy_len);
            if (rc != 0) {
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }
            buffer[copy_len] = '\0';
            ble_provision_transport_consume_rx_chunk(buffer, copy_len);
            return 0;
        default:
            return BLE_ATT_ERR_UNLIKELY;
    }
}

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]) {
                {
                    .uuid = &s_rx_uuid.u,
                    .access_cb = ble_provision_access_cb,
                    .flags = BLE_GATT_CHR_F_WRITE |
                             BLE_GATT_CHR_F_WRITE_NO_RSP,
                    .val_handle = &s_rx_val_handle,
                },
                {
                    .uuid = &s_tx_uuid.u,
                    .access_cb = ble_provision_access_cb,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                    .val_handle = &s_tx_val_handle,
                },
                {0},
            },
    },
    {0},
};

static void ble_provision_on_reset(int reason) {
    ESP_LOGW(TAG, "nimble reset, reason=%d", reason);
    s_synced = false;
}

static int ble_provision_gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;

    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                s_connected = true;
                s_conn_handle = event->connect.conn_handle;
                ESP_LOGI(TAG, "BLE client connected");
                if (s_state_cb != NULL) {
                    s_state_cb(true, s_user_data);
                }
            } else if (s_active) {
                ESP_LOGW(TAG, "BLE connect failed, status=%d",
                         event->connect.status);
                ble_provision_transport_advertise();
            }
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            s_connected = false;
            s_notify_enabled = false;
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            ESP_LOGI(TAG, "BLE client disconnected");
            if (s_state_cb != NULL) {
                s_state_cb(false, s_user_data);
            }
            if (s_active) {
                ble_provision_transport_advertise();
            }
            return 0;
        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == s_tx_val_handle) {
                s_notify_enabled = event->subscribe.cur_notify != 0;
                ESP_LOGI(TAG, "BLE notify=%d", s_notify_enabled ? 1 : 0);
            }
            return 0;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            if (s_active && !s_connected) {
                ble_provision_transport_advertise();
            }
            return 0;
        default:
            return 0;
    }
}

static void ble_provision_transport_advertise(void) {
    struct ble_gap_adv_params adv_params = {0};
    struct ble_hs_adv_fields fields = {0};
    struct ble_hs_adv_fields scan_rsp_fields = {0};
    int rc = 0;

    if (!s_synced || !s_active) {
        return;
    }

    if (ble_gap_adv_active()) {
        ble_gap_adv_stop();
    }

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&s_service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "set adv fields failed, rc=%d", rc);
        return;
    }

    scan_rsp_fields.name = (uint8_t *)s_device_name;
    scan_rsp_fields.name_len = strlen(s_device_name);
    scan_rsp_fields.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&scan_rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "set scan rsp fields failed, rc=%d", rc);
        return;
    }

    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                           ble_provision_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "start adv failed, rc=%d", rc);
        return;
    }

    ESP_LOGI(TAG, "BLE provisioning advertising: %s", s_device_name);
}

static void ble_provision_on_sync(void) {
    int rc = 0;

    s_synced = true;
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "infer addr type failed, rc=%d", rc);
        return;
    }

    ble_provision_transport_advertise();
}

static void ble_provision_host_task(void *param) {
    (void)param;

    ESP_LOGI(TAG, "BLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void ble_provision_transport_reset_runtime_state(void) {
    s_initialized = false;
    s_synced = false;
    s_active = false;
    s_connected = false;
    s_notify_enabled = false;
    s_own_addr_type = BLE_OWN_ADDR_PUBLIC;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_rx_val_handle = 0;
    s_tx_val_handle = 0;
    s_rx_cb = NULL;
    s_state_cb = NULL;
    s_user_data = NULL;
    ble_provision_transport_reset_rx_frame();
}

bool ble_provision_transport_is_active(void) {
    return s_active;
}

bool ble_provision_transport_is_connected(void) {
    return s_connected;
}

esp_err_t ble_provision_transport_get_device_name(char *device_name,
                                                  size_t device_name_len) {
    if (device_name == NULL || device_name_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(device_name, device_name_len, "%s", s_device_name);
    return ESP_OK;
}

esp_err_t ble_provision_transport_notify_json(const char *json_payload) {
    int rc = 0;

    if (json_payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(s_last_payload, sizeof(s_last_payload), "%s", json_payload);
    if (!s_active || !s_connected || !s_notify_enabled) {
        return ESP_OK;
    }

    rc = ble_gatts_notify(s_conn_handle, s_tx_val_handle);
    if (rc != 0) {
        ESP_LOGW(TAG, "BLE notify failed, rc=%d", rc);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t ble_provision_transport_start(
    const char *device_name, ble_provision_transport_rx_cb_t rx_cb,
    ble_provision_transport_state_cb_t state_cb, void *user_data) {
    int rc = 0;

    if (device_name == NULL || rx_cb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_rx_cb = rx_cb;
    s_state_cb = state_cb;
    s_user_data = user_data;
    snprintf(s_device_name, sizeof(s_device_name), "%s", device_name);
    s_active = true;

    if (!s_initialized) {
        rc = nimble_port_init();
        if (rc != ESP_OK) {
            ESP_LOGE(TAG, "nimble init failed, rc=%d", rc);
            s_active = false;
            return ESP_FAIL;
        }

        ble_hs_cfg.reset_cb = ble_provision_on_reset;
        ble_hs_cfg.sync_cb = ble_provision_on_sync;
        ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

        ble_svc_gap_init();
        ble_svc_gatt_init();

        rc = ble_gatts_count_cfg(s_gatt_svcs);
        if (rc != 0) {
            ESP_LOGE(TAG, "count gatt cfg failed, rc=%d", rc);
            nimble_port_deinit();
            ble_provision_transport_reset_runtime_state();
            return ESP_FAIL;
        }

        rc = ble_gatts_add_svcs(s_gatt_svcs);
        if (rc != 0) {
            ESP_LOGE(TAG, "add gatt services failed, rc=%d", rc);
            nimble_port_deinit();
            ble_provision_transport_reset_runtime_state();
            return ESP_FAIL;
        }

        rc = ble_svc_gap_device_name_set(s_device_name);
        if (rc != 0) {
            ESP_LOGE(TAG, "set device name failed, rc=%d", rc);
            nimble_port_deinit();
            ble_provision_transport_reset_runtime_state();
            return ESP_FAIL;
        }

        ble_store_config_init();
        nimble_port_freertos_init(ble_provision_host_task);
        s_initialized = true;
        return ESP_OK;
    }

    rc = ble_svc_gap_device_name_set(s_device_name);
    if (rc != 0) {
        ESP_LOGW(TAG, "refresh device name failed, rc=%d", rc);
    }

    ble_provision_transport_advertise();
    return ESP_OK;
}

esp_err_t ble_provision_transport_stop(void) {
    s_active = false;
    s_notify_enabled = false;
    ble_provision_transport_reset_rx_frame();

    if (s_connected) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }

    if (s_synced && ble_gap_adv_active()) {
        ble_gap_adv_stop();
    }

    return ESP_OK;
}
