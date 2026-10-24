#include "codex_tone.h"

#include <stdio.h>
#include <string.h>

static int g_failed;

static void expect(int cond, const char *what)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", what);
        g_failed = 1;
    }
}

static int first_flip(const int16_t *pcm, size_t count)
{
    size_t i;
    if (count == 0 || pcm[0] == 0) return -1;
    for (i = 1; i < count; i++) {
        if ((pcm[0] > 0 && pcm[i] < 0) || (pcm[0] < 0 && pcm[i] > 0)) return (int)i;
    }
    return -1;
}

int main(void)
{
    static const codex_sound_t sounds[] = {
        CODEX_SOUND_TICK,   CODEX_SOUND_GLIDE, CODEX_SOUND_POSE,  CODEX_SOUND_DECIDE,
        CODEX_SOUND_ERROR,  CODEX_SOUND_SUCCESS, CODEX_SOUND_DENY, CODEX_SOUND_LOWBAT,
    };
    int16_t pcm[9000];
    unsigned long sums[8];
    size_t counts[8];
    size_t i;
    int gap;

    expect(codex_tone_render(CODEX_SOUND_NONE, pcm, 9000) == 0, "silence renders nothing");
    expect(codex_tone_render(CODEX_SOUND_TICK, NULL, 10) == 0, "missing buffer renders nothing");

    for (i = 0; i < 8; i++) {
        size_t j;
        unsigned long sum = 0;
        counts[i] = codex_tone_render(sounds[i], pcm, 8000);
        expect(counts[i] > 0 && counts[i] <= 8000, "tone fits the playback buffer");
        for (j = 0; j < counts[i]; j++) sum += (unsigned long)(pcm[j] < 0 ? -pcm[j] : pcm[j]);
        sums[i] = sum;
        expect(sum > 0, "tone is audible");
    }
    for (i = 0; i < 8; i++) {
        size_t j;
        for (j = i + 1; j < 8; j++) {
            expect(counts[i] != counts[j] || sums[i] != sums[j], "tones are distinguishable");
        }
    }

    expect(codex_tone_render(CODEX_SOUND_TICK, pcm, 8000) < codex_tone_render(CODEX_SOUND_ERROR, pcm, 8000) ||
               first_flip(pcm, 200) > 8,
           "error is not the short tick");
    memset(pcm, 1, sizeof pcm);
    expect(codex_tone_render(CODEX_SOUND_TICK, pcm, 8000) > 0, "tick renders");
    expect(first_flip(pcm, 80) > 0 && first_flip(pcm, 80) < 12, "tick flips quickly");
    memset(pcm, 1, sizeof pcm);
    expect(codex_tone_render(CODEX_SOUND_ERROR, pcm, 8000) > 0, "error renders");
    expect(first_flip(pcm, 200) > 20, "error stays low");

    memset(pcm, 1, sizeof pcm);
    expect(codex_tone_render(CODEX_SOUND_LOWBAT, pcm, 8000) > 0, "low battery renders");
    gap = 0;
    for (i = 0; i < 8000; i++) {
        if (pcm[i] == 0) gap++;
        else if (gap > 1000) break;
        else gap = 0;
    }
    expect(gap > 1000, "low battery leaves a gap between beeps");

    if (g_failed) return 1;
    puts("test_codex_tone: PASS");
    return 0;
}
