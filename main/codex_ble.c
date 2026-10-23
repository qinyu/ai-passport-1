#include "codex_ble.h"

#include "esp_log.h"
#include "esp_random.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"
#include "nimble/ble.h"

void ble_store_config_init(void);

#include <stdio.h>
#include <string.h>

static const char *TAG = "codex_ble";

static const ble_uuid128_t s_svc_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
static const ble_uuid128_t s_rx_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
static const ble_uuid128_t s_tx_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

static uint16_t s_tx_handle;
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint8_t s_addr_type;
static bool s_subscribed;
static bool s_encrypted;
static char s_name[16] = "Codex";
static char s_line[CODEX_BLE_LINE_MAX];
static size_t s_line_len;
static bool s_line_overflow;
static codex_ble_cb_t s_cb;
static void *s_ctx;
static bool s_started;

static int gap_event(struct ble_gap_event *event, void *arg);
static int gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def s_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_rx_uuid.u,
                .access_cb = gatt_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP |
                         BLE_GATT_CHR_F_WRITE_ENC,
            },
            {
                .uuid = &s_tx_uuid.u,
                .access_cb = gatt_access,
                .val_handle = &s_tx_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC |
                         BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC,
            },
            {0},
        },
    },
    {0},
};

static void emit(const codex_ble_evt_t *event)
{
    if (s_cb != NULL) s_cb(event, s_ctx);
}

static void emit_link(void)
{
    codex_ble_evt_t event = {
        .type = CODEX_BLE_EVT_LINK,
        .link_up = s_conn != BLE_HS_CONN_HANDLE_NONE,
        .encrypted = s_encrypted,
    };
    emit(&event);
}

static void feed_byte(uint8_t byte)
{
    if (byte == '\r') return;
    if (byte == '\n') {
        if (!s_line_overflow && s_line_len > 0) {
            codex_ble_evt_t event = {.type = CODEX_BLE_EVT_LINE};
            memcpy(event.line, s_line, s_line_len);
            event.line[s_line_len] = '\0';
            emit(&event);
        }
        s_line_len = 0;
        s_line_overflow = false;
        return;
    }
    if (s_line_overflow) return;
    if (s_line_len + 1 >= sizeof s_line) {
        s_line_overflow = true;
        s_line_len = 0;
        return;
    }
    s_line[s_line_len++] = (char)byte;
}

static int gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint8_t tmp[244];
    uint16_t out_len = 0;
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, tmp, sizeof tmp, &out_len) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    for (uint16_t i = 0; i < out_len; i++) feed_byte(tmp[i]);
    return 0;
}

static void advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    struct ble_hs_adv_fields rsp = {0};
    struct ble_gap_adv_params params = {0};
    int rc;

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (const uint8_t *)s_name;
    fields.name_len = strlen(s_name);
    fields.name_is_complete = 1;
    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv fields failed: %d", rc);
        return;
    }

    rsp.uuids128 = (ble_uuid128_t *)&s_svc_uuid;
    rsp.num_uuids128 = 1;
    rsp.uuids128_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) {
        ESP_LOGE(TAG, "scan response failed: %d", rc);
        return;
    }

    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0) ESP_LOGE(TAG, "advertise failed: %d", rc);
}

static bool link_is_secure(const struct ble_gap_conn_desc *desc)
{
    return desc->sec_state.encrypted && desc->sec_state.authenticated;
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn = event->connect.conn_handle;
            s_subscribed = false;
            s_encrypted = false;
            s_line_len = 0;
            s_line_overflow = false;
            emit_link();
        } else {
            advertise();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        s_subscribed = false;
        s_encrypted = false;
        emit_link();
        advertise();
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) {
            s_encrypted = link_is_secure(&desc);
        } else {
            s_encrypted = false;
        }
        if (s_encrypted) {
            codex_ble_evt_t hide = {.type = CODEX_BLE_EVT_PASSKEY, .passkey_visible = false};
            emit(&hide);
        }
        emit_link();
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_tx_handle) s_subscribed = event->subscribe.cur_notify;
        return 0;
    case BLE_GAP_EVENT_MTU:
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            struct ble_sm_io io = {0};
            uint32_t passkey = esp_random() % 1000000u;
            codex_ble_evt_t shown = {
                .type = CODEX_BLE_EVT_PASSKEY,
                .passkey_visible = true,
                .passkey = passkey,
            };
            io.action = BLE_SM_IOACT_DISP;
            io.passkey = passkey;
            emit(&shown);
            return ble_sm_inject_io(event->passkey.conn_handle, &io);
        }
        return 0;
    default:
        return 0;
    }
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE reset: %d", reason);
}

static void on_sync(void)
{
    uint8_t addr[6] = {0};
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &s_addr_type);
    if (rc == 0) rc = ble_hs_id_copy_addr(s_addr_type, addr, NULL);
    if (rc == 0) {
        snprintf(s_name, sizeof s_name, "Codex-%02X%02X", addr[1], addr[0]);
        ble_svc_gap_device_name_set(s_name);
        ESP_LOGI(TAG, "advertising %s", s_name);
    }
    advertise();
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t codex_ble_start(codex_ble_cb_t cb, void *ctx)
{
    esp_err_t err;
    int rc;
    if (s_started) return ESP_ERR_INVALID_STATE;
    s_cb = cb;
    s_ctx = ctx;

    err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE init failed: %s", esp_err_to_name(err));
        return err;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    rc = ble_gatts_count_cfg(s_svcs);
    if (rc == 0) rc = ble_gatts_add_svcs(s_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT register failed: %d", rc);
        return ESP_FAIL;
    }
    ble_store_config_init();
    nimble_port_freertos_init(host_task);
    s_started = true;
    return ESP_OK;
}

esp_err_t codex_ble_send(const char *json)
{
    char frame[CODEX_BLE_LINE_MAX];
    size_t len;
    size_t offset = 0;
    uint16_t mtu;
    if (json == NULL) return ESP_ERR_INVALID_ARG;
    if (!s_encrypted || !s_subscribed || s_conn == BLE_HS_CONN_HANDLE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }
    len = strlen(json);
    if (len + 2 > sizeof frame) return ESP_ERR_INVALID_SIZE;
    memcpy(frame, json, len);
    if (len == 0 || json[len - 1] != '\n') frame[len++] = '\n';
    frame[len] = '\0';

    mtu = ble_att_mtu(s_conn);
    if (mtu < 23) mtu = 23;
    while (offset < len) {
        uint16_t chunk = (uint16_t)(mtu - 3);
        struct os_mbuf *om;
        if (chunk > len - offset) chunk = (uint16_t)(len - offset);
        om = ble_hs_mbuf_from_flat(frame + offset, chunk);
        if (om == NULL) return ESP_ERR_NO_MEM;
        if (ble_gatts_notify_custom(s_conn, s_tx_handle, om) != 0) return ESP_FAIL;
        offset += chunk;
    }
    return ESP_OK;
}

esp_err_t codex_ble_forget(void)
{
    if (s_conn != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
    }
    return ble_store_clear() == 0 ? ESP_OK : ESP_FAIL;
}

void codex_ble_copy_name(char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) return;
    snprintf(out, out_size, "%s", s_name);
}

bool codex_ble_is_secure(void)
{
    return s_encrypted;
}
