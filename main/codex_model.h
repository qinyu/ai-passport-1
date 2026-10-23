#pragma once

#include "codex_protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Product state for the Passport Codex Buddy. Independent of LVGL and NimBLE. */

#define CODEX_LINK_STALE_MS 30000u
#define CODEX_HEART_MS 1800u
#define CODEX_DIM_IDLE_MS 45000u

typedef enum {
    CODEX_POSE_SLEEP = 0,
    CODEX_POSE_IDLE,
    CODEX_POSE_BUSY,
    CODEX_POSE_ATTENTION,
    CODEX_POSE_COMPLETED,
    CODEX_POSE_DIZZY,
    CODEX_POSE_HEART,
    CODEX_POSE_UNKNOWN,
} codex_pose_t;

typedef enum {
    CODEX_BAR_BLUE = 0,
    CODEX_BAR_GREEN,
    CODEX_BAR_ORANGE,
} codex_bar_band_t;

typedef enum {
    CODEX_RESET_GREEN = 0,
    CODEX_RESET_ORANGE,
    CODEX_RESET_RED,
    CODEX_RESET_UNKNOWN,
} codex_reset_band_t;

typedef enum {
    CODEX_KEY_UP = 0,
    CODEX_KEY_DOWN,
    CODEX_KEY_OK,
    CODEX_KEY_OK_LONG,
} codex_key_t;

typedef enum {
    CODEX_ACTION_NONE = 0,
    CODEX_ACTION_PERMISSION,
    CODEX_ACTION_ACK,
    CODEX_ACTION_STATUS,
    CODEX_ACTION_UNPAIR,
    CODEX_ACTION_BLE_SET,
    CODEX_ACTION_RESET_PREFS,
} codex_action_kind_t;

typedef enum {
    CODEX_PAGE_DASH = 0,
    CODEX_PAGE_PET,
    CODEX_PAGE_INFO,
    CODEX_PAGE_COUNT,
} codex_page_t;

typedef enum {
    CODEX_INFO_ABOUT = 0,
    CODEX_INFO_BUTTONS,
    CODEX_INFO_DEVICE,
    CODEX_INFO_BLE,
    CODEX_INFO_CREDITS,
    CODEX_INFO_COUNT,
} codex_info_t;

typedef enum {
    CODEX_SET_BRIGHT = 0,
    CODEX_SET_SOUND,
    CODEX_SET_BLE,
    CODEX_SET_WIFI,
    CODEX_SET_LED,
    CODEX_SET_CLOCK,
    CODEX_SET_UNPAIR,
    CODEX_SET_RESET,
    CODEX_SET_COUNT,
} codex_setting_t;

typedef enum {
    CODEX_CONFIRM_NONE = 0,
    CODEX_CONFIRM_UNPAIR,
    CODEX_CONFIRM_RESET,
} codex_confirm_t;

typedef struct {
    codex_action_kind_t kind;
    char permission_id[CODEX_PROMPT_ID_MAX];
    char decision[8];
    char ack_cmd[CODEX_CMD_MAX];
    bool ack_ok;
    bool ble_enable;
} codex_action_t;

typedef struct {
    bool link_up;
    bool encrypted;
    bool passkey_visible;
    uint32_t passkey;
    bool packet_seen;
    uint32_t last_packet_ms;
    bool stale;
    char state[CODEX_STATE_MAX];
    uint32_t tokens;
    uint8_t primary;
    uint8_t secondary;
    uint32_t primary_resets_at;
    uint32_t secondary_resets_at;
    char primary_label[CODEX_LABEL_MAX];
    char secondary_label[CODEX_LABEL_MAX];
    bool clock_valid;
    uint32_t epoch_at_sync;
    uint32_t sync_ms;
    bool prompt_active;
    char prompt_id[CODEX_PROMPT_ID_MAX];
    char prompt_tool[CODEX_PROMPT_TOOL_MAX];
    char prompt_hint[CODEX_PROMPT_HINT_MAX];
    uint32_t approvals;
    uint32_t denials;
    bool heart_until_valid;
    uint32_t heart_until_ms;
    bool forced_dim;
    bool input_seen;
    uint32_t last_input_ms;
    codex_page_t page;
    codex_info_t info;
    bool settings_open;
    codex_setting_t setting;
    codex_confirm_t confirm;
    uint8_t brightness;
    bool ble_enabled;
    char device_name[16];
    char mac[18];
    char fw_version[32];
    int battery_percent;
    int battery_mv;
} codex_model_t;

void codex_action_clear(codex_action_t *action);
void codex_model_init(codex_model_t *model);
void codex_model_note_input(codex_model_t *model, uint32_t now_ms);
void codex_model_set_link(codex_model_t *model, bool up, bool encrypted);
void codex_model_set_passkey(codex_model_t *model, bool visible, uint32_t passkey);
void codex_model_set_battery(codex_model_t *model, int percent, int mv);
void codex_model_set_prefs(codex_model_t *model, uint8_t brightness, bool ble_enabled);
void codex_model_set_identity(codex_model_t *model, const char *name, const char *mac,
                              const char *version);
uint8_t codex_brightness_percent(uint8_t level);
bool codex_setting_available(codex_setting_t item);
void codex_model_apply_packet(codex_model_t *model, const codex_packet_t *packet,
                              uint32_t now_ms, codex_action_t *action);
void codex_model_tick(codex_model_t *model, uint32_t now_ms);
void codex_model_on_key(codex_model_t *model, codex_key_t key, uint32_t now_ms,
                        codex_action_t *action);

codex_pose_t codex_pose_from_state(const char *state);
codex_pose_t codex_model_pose(const codex_model_t *model, uint32_t now_ms);
codex_bar_band_t codex_bar_band(uint8_t percent);
codex_reset_band_t codex_reset_band(const char *label, uint32_t remaining_sec, bool known);
bool codex_estimate_epoch(const codex_model_t *model, uint32_t now_ms, uint32_t *epoch_out);
bool codex_reset_remaining(const codex_model_t *model, uint32_t now_ms, bool secondary,
                           uint32_t *remaining_out);
void codex_format_countdown(uint32_t remaining_sec, bool known, char *out, size_t out_size);
bool codex_model_is_dim(const codex_model_t *model, uint32_t now_ms);
