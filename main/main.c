// Codex Buddy for FoloToy AI Passport. The baseline hardware-test menu is not started.
#include "codex_ble.h"
#include "codex_gesture.h"
#include "codex_model.h"
#include "codex_prefs.h"
#include "codex_protocol.h"
#include "codex_tone.h"
#include "codex_ui.h"
#include "key_guard.h"

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "codex";

#define KEY_GUARD_MAX_US (10LL * 1000 * 1000)
#define APP_QUEUE_DEPTH 8

typedef enum {
    APP_EVT_KEY = 1,
    APP_EVT_BLE,
} app_evt_type_t;

typedef struct {
    app_evt_type_t type;
    bsp_btn_t btn;
    bsp_btn_ev_t event;
    codex_ble_evt_t ble;
} app_evt_t;

static QueueHandle_t s_queue;
static key_guard_t s_key_guard;
static codex_model_t s_model;
static volatile bool s_input_ready;
static uint32_t s_last_battery_ms;
static uint8_t s_saved_brightness = 2;
static bool s_saved_ble = true;
static bool s_saved_sound = true;
static bool s_audio_ready;
static uint32_t s_last_decide_ms;
static bool s_lowbat_latched;
static QueueHandle_t s_sound_queue;
static codex_gesture_state_t s_gesture;
static int16_t s_pcm[8000];

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    app_evt_t evt;
    (void)user;
    if (key_guard_consume(&s_key_guard, ev == BSP_BTN_RELEASE, esp_timer_get_time())) return;
    if (!s_input_ready || s_queue == NULL) return;
    if (ev != BSP_BTN_PRESS && ev != BSP_BTN_RELEASE) return;
    memset(&evt, 0, sizeof evt);
    evt.type = APP_EVT_KEY;
    evt.btn = btn;
    evt.event = ev;
    (void)xQueueSend(s_queue, &evt, 0);
}

static void on_ble(const codex_ble_evt_t *event, void *ctx)
{
    app_evt_t evt;
    (void)ctx;
    if (s_queue == NULL || event == NULL) return;
    memset(&evt, 0, sizeof evt);
    evt.type = APP_EVT_BLE;
    evt.ble = *event;
    (void)xQueueSend(s_queue, &evt, 0);
}

static esp_err_t send_action(const codex_action_t *action);
static void save_prefs_if_needed(void);

static void request_sound(codex_sound_t sound)
{
    if (!s_audio_ready || !s_model.sound_enabled || sound == CODEX_SOUND_NONE || s_sound_queue == NULL) return;
    (void)xQueueSend(s_sound_queue, &sound, 0);
}

static void sound_task(void *param)
{
    (void)param;
    for (;;) {
        codex_sound_t sound = CODEX_SOUND_NONE;
        size_t count;
        if (xQueueReceive(s_sound_queue, &sound, portMAX_DELAY) != pdTRUE) continue;
        if (!s_model.sound_enabled || sound == CODEX_SOUND_NONE) continue;
        count = codex_tone_render(sound, s_pcm, sizeof s_pcm / sizeof s_pcm[0]);
        if (count > 0) (void)bsp_audio_write(s_pcm, count * sizeof s_pcm[0]);
    }
}

static codex_key_t gesture_key(const codex_gesture_t *gesture)
{
    if (gesture->kind == CODEX_GESTURE_DOUBLE) return CODEX_KEY_OK_DOUBLE;
    if (gesture->kind == CODEX_GESTURE_LONG) return CODEX_KEY_OK_LONG;
    if (gesture->button == BSP_BTN_UP) return CODEX_KEY_UP;
    if (gesture->button == BSP_BTN_DOWN) return CODEX_KEY_DOWN;
    return CODEX_KEY_OK;
}

static void apply_gesture(const codex_gesture_t *gesture, uint32_t now)
{
    codex_action_t action;
    if (gesture == NULL || gesture->kind == CODEX_GESTURE_NONE) return;
    if (gesture->kind == CODEX_GESTURE_LONG && gesture->button != CODEX_GESTURE_BTN_OK) return;
    codex_model_on_key(&s_model, gesture_key(gesture), now, &action);
    if (send_action(&action) != ESP_OK && action.kind != CODEX_ACTION_NONE) request_sound(CODEX_SOUND_ERROR);
    else request_sound(action.sound);
    save_prefs_if_needed();
}

static esp_err_t send_action(const codex_action_t *action)
{
    char json[320];
    char name[16];
    esp_err_t err = ESP_OK;
    if (action == NULL || action->kind == CODEX_ACTION_NONE) return ESP_OK;
    if (action->kind == CODEX_ACTION_PERMISSION) {
        if (codex_protocol_permission_json(json, sizeof json, action->permission_id, action->decision) != 0) return ESP_FAIL;
        err = codex_ble_send(json);
    } else if (action->kind == CODEX_ACTION_ACK) {
        if (codex_protocol_ack_json(json, sizeof json, action->ack_cmd, action->ack_ok) != 0) return ESP_FAIL;
        err = codex_ble_send(json);
    } else if (action->kind == CODEX_ACTION_STATUS) {
        codex_ble_copy_name(name, sizeof name);
        if (codex_protocol_status_json(json, sizeof json, name, codex_ble_is_secure(),
                                       s_model.battery_percent, s_model.battery_mv,
                                       s_model.approvals, s_model.denials) != 0) return ESP_FAIL;
        err = codex_ble_send(json);
    } else if (action->kind == CODEX_ACTION_UNPAIR) {
        err = codex_ble_clear_bonds();
        if (codex_protocol_ack_json(json, sizeof json, "unpair", err == ESP_OK) == 0) {
            esp_err_t send_err = codex_ble_send(json);
            if (send_err != ESP_OK) err = send_err;
        } else {
            err = ESP_FAIL;
        }
        (void)codex_ble_disconnect();
    } else if (action->kind == CODEX_ACTION_BLE_SET) {
        err = codex_ble_set_enabled(action->ble_enable);
    } else if (action->kind == CODEX_ACTION_RESET_PREFS) {
        err = codex_prefs_reset();
        if (err == ESP_OK) err = codex_ble_set_enabled(true);
    }
    if (err != ESP_OK) ESP_LOGW(TAG, "BLE reply failed: %s", esp_err_to_name(err));
    return err;
}

static void save_prefs_if_needed(void)
{
    codex_prefs_t prefs;
    if (s_model.brightness == s_saved_brightness && s_model.ble_enabled == s_saved_ble &&
        s_model.sound_enabled == s_saved_sound) return;
    prefs.brightness = s_model.brightness;
    prefs.ble_enabled = s_model.ble_enabled;
    prefs.sound_enabled = s_model.sound_enabled;
    if (codex_prefs_save(&prefs) == ESP_OK) {
        s_saved_brightness = s_model.brightness;
        s_saved_ble = s_model.ble_enabled;
        s_saved_sound = s_model.sound_enabled;
    }
}

static void refresh(uint32_t now)
{
    char name[16];
    char mac[18];
    bool dim = codex_model_is_dim(&s_model, now);
    uint8_t light = dim ? 8 : codex_brightness_percent(s_model.brightness);
    const esp_app_desc_t *app = esp_app_get_description();
    bsp_display_backlight(light);
    codex_ble_copy_name(name, sizeof name);
    codex_ble_copy_mac(mac, sizeof mac);
    codex_model_set_identity(&s_model, name, mac, app != NULL ? app->version : "");
    if (!bsp_lvgl_lock(200)) return;
    codex_ui_apply(&s_model, now);
    bsp_lvgl_unlock();
}

static void sample_battery(uint32_t now)
{
    if ((uint32_t)(now - s_last_battery_ms) < 5000) return;
    s_last_battery_ms = now;
    int percent = bsp_battery_soc();
    codex_model_set_battery(&s_model, percent, bsp_battery_mv());
    if (percent >= 0 && percent <= 15 && !s_lowbat_latched) {
        s_lowbat_latched = true;
        request_sound(CODEX_SOUND_LOWBAT);
    } else if (percent > 20) {
        s_lowbat_latched = false;
    }
}

static void handle_ble(const codex_ble_evt_t *event, uint32_t now)
{
    codex_action_t action;
    codex_packet_t packet;
    if (event->type == CODEX_BLE_EVT_LINK) {
        codex_model_set_link(&s_model, event->link_up, event->encrypted);
        return;
    }
    if (event->type == CODEX_BLE_EVT_PASSKEY) {
        codex_model_set_passkey(&s_model, event->passkey_visible, event->passkey);
        return;
    }
    if (codex_protocol_parse(event->line, strlen(event->line), &packet) != 0) {
        ESP_LOGW(TAG, "ignored malformed packet");
        return;
    }
    codex_model_apply_packet(&s_model, &packet, now, &action);
    if (send_action(&action) != ESP_OK && action.kind != CODEX_ACTION_NONE) request_sound(CODEX_SOUND_ERROR);
}

static void app_task(void *param)
{
    (void)param;
    for (;;) {
        app_evt_t evt;
        uint32_t now;
        if (xQueueReceive(s_queue, &evt, pdMS_TO_TICKS(40)) == pdTRUE) {
            now = now_ms();
            if (evt.type == APP_EVT_KEY) {
                codex_gesture_t gesture = {CODEX_GESTURE_NONE, 0};
                if (evt.event == BSP_BTN_PRESS) {
                    gesture = codex_gesture_on_press(&s_gesture, (uint8_t)evt.btn, now);
                } else if (evt.event == BSP_BTN_RELEASE) {
                    gesture = codex_gesture_on_release(&s_gesture, (uint8_t)evt.btn, now);
                }
                apply_gesture(&gesture, now);
            } else if (evt.type == APP_EVT_BLE) {
                handle_ble(&evt.ble, now);
            }
        }
        now = now_ms();
        {
            codex_gesture_t pending = codex_gesture_poll(&s_gesture, now);
            apply_gesture(&pending, now);
        }
        codex_model_tick(&s_model, now);
        if (s_model.prompt_active && s_model.sound_enabled) {
            if (s_last_decide_ms == 0 || (uint32_t)(now - s_last_decide_ms) >= 2500u) {
                request_sound(CODEX_SOUND_DECIDE);
                s_last_decide_ms = now;
            }
        } else {
            s_last_decide_ms = 0;
        }
        sample_battery(now);
        refresh(now);
    }
}

void app_main(void)
{
    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    esp_err_t button_err;
    ESP_LOGI(TAG, "Codex Buddy Passport start");
    if (wake_causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), KEY_GUARD_MAX_US);
    }

    bsp_i2c_init();
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "display init failed (MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }

    codex_model_init(&s_model);
    {
        codex_prefs_t prefs = {.brightness = 2, .ble_enabled = true, .sound_enabled = true};
        if (codex_prefs_load(&prefs) == ESP_OK) {
            codex_model_set_prefs(&s_model, prefs.brightness, prefs.ble_enabled, prefs.sound_enabled);
        }
        s_saved_brightness = s_model.brightness;
        s_saved_ble = s_model.ble_enabled;
        s_saved_sound = s_model.sound_enabled;
    }
    codex_gesture_init(&s_gesture);
    if (bsp_audio_init() == ESP_OK) {
        bsp_audio_set_volume(40);
        if (bsp_audio_set_format(16000, 16, 1) == ESP_OK) {
            s_audio_ready = true;
            s_sound_queue = xQueueCreate(4, sizeof(codex_sound_t));
            if (s_sound_queue == NULL ||
                xTaskCreate(sound_task, "codex_snd", 4096, NULL, 4, NULL) != pdPASS) {
                s_audio_ready = false;
                ESP_LOGE(TAG, "sound task did not start");
            }
        } else {
            ESP_LOGE(TAG, "audio format was not accepted");
        }
    }
    codex_model_note_input(&s_model, now_ms());
    if (bsp_battery_init() == ESP_OK) {
        codex_model_set_battery(&s_model, bsp_battery_soc(), bsp_battery_mv());
    }
    s_last_battery_ms = now_ms();

    if (bsp_lvgl_lock(1000)) {
        codex_ui_create();
        codex_ui_apply(&s_model, now_ms());
        bsp_lvgl_unlock();
    }
    bsp_display_backlight(codex_brightness_percent(s_model.brightness));

    s_queue = xQueueCreate(APP_QUEUE_DEPTH, sizeof(app_evt_t));
    button_err = bsp_button_init(on_key, NULL);
    if (button_err != ESP_OK) ESP_LOGE(TAG, "buttons unavailable: %s", esp_err_to_name(button_err));
    else if (s_key_guard.armed) {
        key_guard_sync_released(&s_key_guard, bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN);
    }

    (void)codex_ble_set_enabled(s_model.ble_enabled);
    if (codex_ble_start(on_ble, NULL) != ESP_OK) ESP_LOGE(TAG, "BLE did not start");
    s_input_ready = true;
    if (xTaskCreate(app_task, "codex_app", 8192, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "app task create failed");
    }
}
