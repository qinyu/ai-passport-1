#include "codex_prefs.h"

#include "nvs.h"
#include "nvs_flash.h"

static esp_err_t open_ns(nvs_open_mode_t mode, nvs_handle_t *handle)
{
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) return err;
    return nvs_open("codex", mode, handle);
}

esp_err_t codex_prefs_load(codex_prefs_t *out)
{
    nvs_handle_t handle;
    uint8_t brightness = 2;
    uint8_t ble = 1;
    uint8_t sound = 1;
    esp_err_t err;
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    err = open_ns(NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        out->brightness = 2;
        out->ble_enabled = true;
        out->sound_enabled = true;
        return ESP_OK;
    }
    if (err != ESP_OK) return err;
    if (nvs_get_u8(handle, "br", &brightness) != ESP_OK || brightness > 3) brightness = 2;
    if (nvs_get_u8(handle, "ble", &ble) != ESP_OK) ble = 1;
    if (nvs_get_u8(handle, "snd", &sound) != ESP_OK) sound = 1;
    nvs_close(handle);
    out->brightness = brightness;
    out->ble_enabled = ble != 0;
    out->sound_enabled = sound != 0;
    return ESP_OK;
}

esp_err_t codex_prefs_save(const codex_prefs_t *prefs)
{
    nvs_handle_t handle;
    esp_err_t err;
    if (prefs == NULL) return ESP_ERR_INVALID_ARG;
    err = open_ns(NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(handle, "br", prefs->brightness > 3 ? 3 : prefs->brightness);
    if (err == ESP_OK) err = nvs_set_u8(handle, "ble", prefs->ble_enabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_u8(handle, "snd", prefs->sound_enabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t codex_prefs_reset(void)
{
    nvs_handle_t handle;
    esp_err_t err = open_ns(NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    err = nvs_erase_all(handle);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
