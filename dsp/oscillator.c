/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Naive oscillator bank: three phase accumulators at 48 kHz, plain waveforms, summed.
 * No oversampling or alias suppression. Uses 32-bit arithmetic only: a mod links no
 * libgcc 64-bit helpers. */
#include "oscillator.h"

#define HALF   0x80000000u
#define WIDE   0x4ccccccdu   /* 30% high */
#define NARROW 0x1999999au   /* 10% high */

/* Hz * 65536 to Q0.32 cycles per 48 kHz sample: f * 2^16 / 48000 = f + f * 137 / 375. */
static uint32_t increment(uint32_t f)
{
    if (f < 4096) f = 4096;                 /* 1/16 Hz */
    if (f > 20000u * 65536) f = 20000u * 65536;
    return f + f / 375 * 137 + f % 375 * 137 / 375;
}

/* One sample of a shape at phase p, full scale. */
static int32_t wave(unsigned shape, uint32_t p)
{
    int32_t saw = (int32_t)(p ^ HALF);                              /* rising -1..1 */
    int32_t tri = (int32_t)(((p < HALF ? p : ~p) << 1) ^ HALF);     /* -1 up to 1 and back */
    switch (shape) {
    case 1: return (tri >> 1) + (saw >> 1);
    case 2: return ~saw;
    case 3: return saw;
    case 4: return p < HALF ? INT32_MAX : INT32_MIN;
    case 5: return p < WIDE ? INT32_MAX : INT32_MIN;
    case 6: return p < NARROW ? INT32_MAX : INT32_MIN;
    default: return tri;
    }
}

void modeldigi_osc_init(modeldigi_osc_state *state)
{
    for (unsigned v = 0; v < 3; ++v)
        state->phase[v] = 0;
}

void modeldigi_osc_render(modeldigi_osc_state *state, const modeldigi_osc_params *params,
                          int32_t *output, size_t frames)
{
    uint32_t inc[3];
    for (unsigned v = 0; v < 3; ++v)
        inc[v] = increment(params->frequency_q16_16[v]);
    for (size_t i = 0; i < frames; ++i) {
        int32_t mix = 0;
        for (unsigned v = 0; v < 3; ++v) {
            /* (sample / 2^16) * level fits 32 bits; the bank gain is another 1/16. */
            mix += ((wave(params->waveform[v], state->phase[v]) >> 16) * params->level[v]) >> 4;
            state->phase[v] += inc[v];
        }
        output[i] = mix;
    }
}
