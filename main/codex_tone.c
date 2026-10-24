#include "codex_tone.h"

#define TONE_HZ 16000

static size_t add_tone(int16_t *dst, size_t used, size_t max, int freq, int ms, int amp)
{
    size_t count = (size_t)ms * TONE_HZ / 1000u;
    size_t i;
    if (used >= max) return used;
    if (used + count > max) count = max - used;
    for (i = 0; i < count; i++) {
        int phase = (int)((i * freq) % TONE_HZ);
        int sample = phase < TONE_HZ / 2 ? amp : -amp;
        int fade = (int)(count - i);
        if (fade < TONE_HZ / 50) sample = sample * fade / (TONE_HZ / 50);
        dst[used + i] = (int16_t)sample;
    }
    return used + count;
}

static size_t add_silence(int16_t *dst, size_t used, size_t max, int ms)
{
    size_t count = (size_t)ms * TONE_HZ / 1000u;
    size_t i;
    if (used >= max) return used;
    if (used + count > max) count = max - used;
    for (i = 0; i < count; i++) dst[used + i] = 0;
    return used + count;
}

size_t codex_tone_render(codex_sound_t sound, int16_t *dst, size_t max_samples)
{
    size_t used = 0;
    if (dst == NULL || max_samples == 0 || sound == CODEX_SOUND_NONE) return 0;
    switch (sound) {
    case CODEX_SOUND_TICK:
        used = add_tone(dst, used, max_samples, 1800, 24, 5000);
        break;
    case CODEX_SOUND_GLIDE:
        used = add_tone(dst, used, max_samples, 880, 40, 6000);
        used = add_tone(dst, used, max_samples, 1320, 50, 6000);
        break;
    case CODEX_SOUND_POSE:
        used = add_tone(dst, used, max_samples, 700, 30, 5500);
        used = add_tone(dst, used, max_samples, 1400, 40, 5500);
        break;
    case CODEX_SOUND_DECIDE:
        used = add_tone(dst, used, max_samples, 1046, 70, 8000);
        used = add_silence(dst, used, max_samples, 40);
        used = add_tone(dst, used, max_samples, 1568, 90, 8000);
        break;
    case CODEX_SOUND_ERROR:
        used = add_tone(dst, used, max_samples, 220, 90, 7000);
        used = add_silence(dst, used, max_samples, 30);
        used = add_tone(dst, used, max_samples, 160, 110, 7000);
        break;
    case CODEX_SOUND_SUCCESS:
        used = add_tone(dst, used, max_samples, 880, 40, 6500);
        used = add_tone(dst, used, max_samples, 1320, 40, 6500);
        used = add_tone(dst, used, max_samples, 1760, 60, 6500);
        break;
    case CODEX_SOUND_DENY:
        used = add_tone(dst, used, max_samples, 440, 50, 6500);
        used = add_tone(dst, used, max_samples, 220, 80, 6500);
        break;
    case CODEX_SOUND_LOWBAT:
        used = add_tone(dst, used, max_samples, 392, 80, 5000);
        used = add_silence(dst, used, max_samples, 120);
        used = add_tone(dst, used, max_samples, 392, 80, 5000);
        used = add_silence(dst, used, max_samples, 120);
        used = add_tone(dst, used, max_samples, 392, 80, 5000);
        break;
    case CODEX_SOUND_NONE:
    default:
        break;
    }
    return used;
}
