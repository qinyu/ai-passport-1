#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

/* App preferences in the "codex" NVS namespace. Bonds stay in the NimBLE store. */

typedef struct {
    uint8_t brightness;
    bool ble_enabled;
} codex_prefs_t;

esp_err_t codex_prefs_load(codex_prefs_t *out);
esp_err_t codex_prefs_save(const codex_prefs_t *prefs);
esp_err_t codex_prefs_reset(void);
