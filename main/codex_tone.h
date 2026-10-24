#pragma once

#include "codex_model.h"

#include <stddef.h>
#include <stdint.h>

/* 16 kHz mono tones. The caller owns the buffer. Returns the sample count. */
size_t codex_tone_render(codex_sound_t sound, int16_t *dst, size_t max_samples);
