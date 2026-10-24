#include "codex_gesture.h"

#include <stdio.h>

static int g_failed;

static void expect(int cond, const char *what)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", what);
        g_failed = 1;
    }
}

static void press(codex_gesture_state_t *state, uint8_t button, uint32_t now, codex_gesture_t *out)
{
    *out = codex_gesture_on_press(state, button, now);
}

static void release(codex_gesture_state_t *state, uint8_t button, uint32_t now, codex_gesture_t *out)
{
    *out = codex_gesture_on_release(state, button, now);
}

int main(void)
{
    codex_gesture_state_t state;
    codex_gesture_t gesture;

    codex_gesture_init(&state);
    press(&state, 0, 0, &gesture);
    expect(gesture.kind == CODEX_GESTURE_NONE, "press is not a gesture");
    release(&state, 0, 100, &gesture);
    expect(gesture.kind == CODEX_GESTURE_SHORT && gesture.button == 0, "up short is immediate");

    codex_gesture_init(&state);
    press(&state, CODEX_GESTURE_BTN_OK, 0, &gesture);
    release(&state, CODEX_GESTURE_BTN_OK, 120, &gesture);
    expect(gesture.kind == CODEX_GESTURE_NONE, "ok short waits for a double");
    gesture = codex_gesture_poll(&state, 120 + CODEX_GESTURE_DOUBLE_MS - 1);
    expect(gesture.kind == CODEX_GESTURE_NONE, "ok short stays pending");
    gesture = codex_gesture_poll(&state, 120 + CODEX_GESTURE_DOUBLE_MS);
    expect(gesture.kind == CODEX_GESTURE_SHORT && gesture.button == CODEX_GESTURE_BTN_OK, "ok short after the window");

    codex_gesture_init(&state);
    press(&state, CODEX_GESTURE_BTN_OK, 0, &gesture);
    release(&state, CODEX_GESTURE_BTN_OK, 80, &gesture);
    press(&state, CODEX_GESTURE_BTN_OK, 80 + CODEX_GESTURE_DOUBLE_MS, &gesture);
    expect(gesture.kind == CODEX_GESTURE_NONE, "second press inside the window is quiet");
    release(&state, CODEX_GESTURE_BTN_OK, 80 + CODEX_GESTURE_DOUBLE_MS + 90, &gesture);
    expect(gesture.kind == CODEX_GESTURE_DOUBLE, "two ok shorts are a double");

    codex_gesture_init(&state);
    press(&state, CODEX_GESTURE_BTN_OK, 0, &gesture);
    release(&state, CODEX_GESTURE_BTN_OK, 50, &gesture);
    press(&state, 0, 100, &gesture);
    expect(gesture.kind == CODEX_GESTURE_SHORT && gesture.button == CODEX_GESTURE_BTN_OK,
           "another key commits the pending ok short");
    release(&state, 0, 140, &gesture);
    expect(gesture.kind == CODEX_GESTURE_SHORT && gesture.button == 0, "the other key is still a short");

    codex_gesture_init(&state);
    press(&state, CODEX_GESTURE_BTN_OK, 1000, &gesture);
    release(&state, CODEX_GESTURE_BTN_OK, 1000 + CODEX_GESTURE_LONG_MIN_MS, &gesture);
    expect(gesture.kind == CODEX_GESTURE_LONG && gesture.button == CODEX_GESTURE_BTN_OK, "ok long on release");

    codex_gesture_init(&state);
    press(&state, 1, 0, &gesture);
    release(&state, 1, CODEX_GESTURE_LONG_MIN_MS, &gesture);
    expect(gesture.kind == CODEX_GESTURE_LONG && gesture.button == 1, "down long is reported");

    codex_gesture_init(&state);
    press(&state, CODEX_GESTURE_BTN_OK, 0, &gesture);
    release(&state, CODEX_GESTURE_BTN_OK, 500, &gesture);
    expect(gesture.kind == CODEX_GESTURE_NONE, "gap between short and long is ignored");
    gesture = codex_gesture_poll(&state, 5000);
    expect(gesture.kind == CODEX_GESTURE_NONE, "a medium press does not become a short later");

    if (g_failed) return 1;
    puts("test_codex_gesture: PASS");
    return 0;
}
