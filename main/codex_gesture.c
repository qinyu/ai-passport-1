#include "codex_gesture.h"

#include <stddef.h>

static codex_gesture_t none_gesture(void)
{
    codex_gesture_t gesture = {CODEX_GESTURE_NONE, 0};
    return gesture;
}

static codex_gesture_t make_gesture(codex_gesture_kind_t kind, uint8_t button)
{
    codex_gesture_t gesture = {kind, button};
    return gesture;
}

static void clear_ok_pending(codex_gesture_state_t *state)
{
    state->ok_pending = false;
    state->ok_second_down = false;
}

void codex_gesture_init(codex_gesture_state_t *state)
{
    unsigned int i;
    if (state == NULL) return;
    for (i = 0; i < 3; i++) {
        state->held[i] = false;
        state->pressed_ms[i] = 0;
    }
    clear_ok_pending(state);
}

codex_gesture_t codex_gesture_on_press(codex_gesture_state_t *state, uint8_t button, uint32_t now_ms)
{
    codex_gesture_t fired = none_gesture();
    if (state == NULL || button > 2) return fired;
    if (state->ok_pending && !state->ok_second_down &&
        (button != CODEX_GESTURE_BTN_OK ||
         (uint32_t)(now_ms - state->ok_pending_ms) > CODEX_GESTURE_DOUBLE_MS)) {
        fired = make_gesture(CODEX_GESTURE_SHORT, CODEX_GESTURE_BTN_OK);
        clear_ok_pending(state);
    }
    state->held[button] = true;
    state->pressed_ms[button] = now_ms;
    if (button == CODEX_GESTURE_BTN_OK && state->ok_pending) state->ok_second_down = true;
    return fired;
}

codex_gesture_t codex_gesture_on_release(codex_gesture_state_t *state, uint8_t button, uint32_t now_ms)
{
    uint32_t held;
    if (state == NULL || button > 2 || !state->held[button]) return none_gesture();
    state->held[button] = false;
    held = now_ms - state->pressed_ms[button];
    if (held >= CODEX_GESTURE_LONG_MIN_MS) {
        if (button == CODEX_GESTURE_BTN_OK) clear_ok_pending(state);
        return make_gesture(CODEX_GESTURE_LONG, button);
    }
    if (held >= CODEX_GESTURE_SHORT_MAX_MS) {
        if (button == CODEX_GESTURE_BTN_OK) clear_ok_pending(state);
        return none_gesture();
    }
    if (button != CODEX_GESTURE_BTN_OK) return make_gesture(CODEX_GESTURE_SHORT, button);
    if (state->ok_pending && state->ok_second_down &&
        (uint32_t)(state->pressed_ms[button] - state->ok_pending_ms) <= CODEX_GESTURE_DOUBLE_MS) {
        clear_ok_pending(state);
        return make_gesture(CODEX_GESTURE_DOUBLE, button);
    }
    state->ok_pending = true;
    state->ok_pending_ms = now_ms;
    state->ok_second_down = false;
    return none_gesture();
}

codex_gesture_t codex_gesture_poll(codex_gesture_state_t *state, uint32_t now_ms)
{
    if (state == NULL || !state->ok_pending || state->ok_second_down) return none_gesture();
    if ((uint32_t)(now_ms - state->ok_pending_ms) < CODEX_GESTURE_DOUBLE_MS) return none_gesture();
    clear_ok_pending(state);
    return make_gesture(CODEX_GESTURE_SHORT, CODEX_GESTURE_BTN_OK);
}
