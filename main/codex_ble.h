#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CODEX_BLE_LINE_MAX 384

typedef enum {
    CODEX_BLE_EVT_LINK = 1,
    CODEX_BLE_EVT_PASSKEY,
    CODEX_BLE_EVT_LINE,
} codex_ble_evt_type_t;

typedef struct {
    codex_ble_evt_type_t type;
    bool link_up;
    bool encrypted;
    bool passkey_visible;
    uint32_t passkey;
    char line[CODEX_BLE_LINE_MAX];
} codex_ble_evt_t;

/* Invoked on the NimBLE host task. Copy the event and return; do not touch LVGL. */
typedef void (*codex_ble_cb_t)(const codex_ble_evt_t *event, void *ctx);

esp_err_t codex_ble_start(codex_ble_cb_t cb, void *ctx);
esp_err_t codex_ble_send(const char *json);
esp_err_t codex_ble_forget(void);
void codex_ble_copy_name(char *out, size_t out_size);
bool codex_ble_is_secure(void);
