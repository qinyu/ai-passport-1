#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* App gesture timing. Short is under 400 ms. Long is at least 800 ms and is
 * reported when the key is released. A double is two OK shorts whose second
 * press starts within 400 ms of the first release. */

#define CODEX_GESTURE_SHORT_MAX_MS 400u
#define CODEX_GESTURE_LONG_MIN_MS 800u
#define CODEX_GESTURE_DOUBLE_MS 400u
#define CODEX_GESTURE_BTN_OK 2u

typedef enum {
    CODEX_GESTURE_NONE = 0,
    CODEX_GESTURE_SHORT,
    CODEX_GESTURE_LONG,
    CODEX_GESTURE_DOUBLE,
} codex_gesture_kind_t;

typedef struct {
    codex_gesture_kind_t kind;
    uint8_t button;
} codex_gesture_t;

typedef struct {
    bool held[3];
    uint32_t pressed_ms[3];
    bool ok_pending;
    uint32_t ok_pending_ms;
    bool ok_second_down;
} codex_gesture_state_t;

void codex_gesture_init(codex_gesture_state_t *state);
codex_gesture_t codex_gesture_on_press(codex_gesture_state_t *state, uint8_t button, uint32_t now_ms);
codex_gesture_t codex_gesture_on_release(codex_gesture_state_t *state, uint8_t button, uint32_t now_ms);
codex_gesture_t codex_gesture_poll(codex_gesture_state_t *state, uint32_t now_ms);
