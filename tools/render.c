/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../dsp/oscillator.h"

static void put32(FILE *f, uint32_t n)
{
    for (unsigned i = 0; i < 4; ++i) fputc((int)((n >> (8 * i)) & 255), f);
}

int main(int argc, char **argv)
{
    modeldigi_osc_params p = {0};
    modeldigi_osc_state s;
    uint64_t initial_phase[3] = {0};
    bool phase_set[3] = {false};
    double seconds = 3;
    p.frequency_q16_16[0] = p.frequency_q16_16[1] = 440 * 65536;
    p.frequency_q16_16[2] = 33564;
    p.waveform[0] = p.waveform[1] = 3;
    p.level[0] = 65535;
    if (argc < 2) {
        fprintf(stderr, "usage: render OUT.wav [seconds=3 freq1=440 freq2=440 freq3=.512 wave1=3 wave2=3 wave3=0 level1=1 level2=0 level3=0 depth=0 mod=0 control=0 phaseN=CYCLES]\n");
        return 2;
    }
    for (int i = 2; i < argc; ++i) {
        char key[24], tail;
        double value;
        if (sscanf(argv[i], "%23[^=]=%lf%c", key, &value, &tail) != 2 || !isfinite(value)) goto invalid;
        if (!strcmp(key, "seconds") && value > 0 && value <= 180) seconds = value;
        else if (!strcmp(key, "depth") && value >= 0 && value <= 2) p.modulation_depth_q16_16 = (uint32_t)llround(value * 65536);
        else if (!strcmp(key, "mod") && (value == 0 || value == 1)) p.modulation_enabled = value != 0;
        else if (!strcmp(key, "control") && (value == 0 || value == 1)) p.osc3_control = value != 0;
        else {
            size_t n = strlen(key);
            if (n < 2 || key[n - 1] < '1' || key[n - 1] > '3') goto invalid;
            unsigned v = (unsigned)(key[n - 1] - '1');
            key[n - 1] = 0;
            if (!strcmp(key, "freq") && value >= .0625 && value <= 20000) p.frequency_q16_16[v] = (uint32_t)llround(value * 65536);
            else if (!strcmp(key, "wave") && value >= 0 && value <= 6 && value == floor(value)) p.waveform[v] = (uint8_t)value;
            else if (!strcmp(key, "level") && value >= 0 && value <= 1) p.level[v] = (uint16_t)llround(value * 65535);
            else if (!strcmp(key, "phase") && value >= 0 && value < 1) {
                initial_phase[v] = (uint64_t)(value * (double)(UINT64_C(1) << 48)) << 16;
                phase_set[v] = true;
            }
            else goto invalid;
        }
        continue;
invalid:
        fprintf(stderr, "render: invalid setting: %s\n", argv[i]);
        return 2;
    }
    uint32_t frames = (uint32_t)llround(seconds * 48000);
    if (!frames) { fprintf(stderr, "render: duration rounds to zero frames\n"); return 2; }
    FILE *file = fopen(argv[1], "wb");
    if (!file) { perror(argv[1]); return 1; }
    fputs("RIFF", file); put32(file, 36 + 4 * frames); fputs("WAVEfmt ", file);
    put32(file, 16); fputc(1, file); fputc(0, file); fputc(1, file); fputc(0, file);
    put32(file, 48000); put32(file, 192000);
    fputc(4, file); fputc(0, file); fputc(32, file); fputc(0, file);
    fputs("data", file); put32(file, 4 * frames);
    modeldigi_osc_init(&s);
    /* Host diagnostics override only initial phase; the engine API is unchanged. */
    for (unsigned v = 0; v < 3; ++v)
        if (phase_set[v]) s.voice[v].phase = initial_phase[v];
    for (uint32_t at = 0; at < frames;) {
        int32_t block[32];
        unsigned count = frames - at < 32 ? frames - at : 32;
        modeldigi_osc_render(&s, &p, block, count);
        for (unsigned i = 0; i < count; ++i) put32(file, (uint32_t)block[i]);
        at += count;
    }
    if (ferror(file)) { fprintf(stderr, "render: write failed\n"); fclose(file); return 1; }
    if (fclose(file)) { perror(argv[1]); return 1; }
    return 0;
}
