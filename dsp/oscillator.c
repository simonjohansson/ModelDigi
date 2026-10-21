/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Oscillator bank: three phase accumulators at 48 kHz, summed. Each step and corner is smoothed by
 * a triangular kernel one sample wide on each side (2-point PolyBLEP and PolyBLAMP), so the output
 * is the ideal waveform with a sinc^2 response, sampled; no oversampling. Uses 32-bit arithmetic
 * only: a mod links no libgcc 64-bit helpers.
 *
 * Shapes follow the frozen reference conventions: the saw falls from its peak and resets at
 * phase 0, the triangle peaks at phase 0, pulses are centred on phase 0 with zero mean and a
 * high-to-low step of 2. The bank output is those levels / 16. */
#include "oscillator.h"

#define HALF 0x80000000u

/* Per shape, levels / 16 in units of 2^-18 full scale: triangle and saw parts of the line shapes,
 * or a pulse's high level, half width (Q0.32) and low/high ratio d / (1 - d) (Q0.32). From the
 * reference: saw peak 0.94775948, triangle 0.88925678 plus 0.0168924 saw, combination 0.6362014
 * triangle plus 0.1489326 saw, pulses high 49%, 31% and 19.5% of the cycle. */
static const struct { int32_t tri, saw; } lines[4] = {
    { 14570, 262 }, { 9269, 2480 }, { 0, -15528 }, { 0, 15528 },
};
static const struct { int32_t high; uint32_t half, ratio; } pulses[3] = {
    { 16712, 0x3eb851ecu, 0xf5f5f5fau }, { 22610, 0x27ae147bu, 0x7303b5cdu }, { 26378, 0x18f5c28fu, 0x3e032e1cu },
};

/* Hz * 65536 to Q0.32 cycles per 48 kHz sample: f * 2^16 / 48000 = f + f * 137 / 375. */
static uint32_t increment(uint32_t f)
{
    if (f < 4096) f = 4096;                 /* 1/16 Hz */
    if (f > 20000u * 65536) f = 20000u * 65536;
    return f + f / 375 * 137 + f % 375 * 137 / 375;
}

/* floor(a * b / 2^32) from 16-bit partial products. */
static uint32_t mulhi(uint32_t a, uint32_t b)
{
    uint32_t ah = a >> 16, al = a & 0xffff, bh = b >> 16, bl = b & 0xffff;
    uint32_t mid = (al * bl >> 16) + (ah * bl & 0xffff) + (al * bh & 0xffff);
    return ah * bh + (ah * bl >> 16) + (al * bh >> 16) + (mid >> 16);
}

/* k * level / 65535, rounded, keeping the sign. */
static int32_t scaled(int32_t k, uint16_t level)
{
    int32_t m = (int32_t)(((uint32_t)(k < 0 ? -k : k) * level + 32767) / 65535);
    return k < 0 ? -m : m;
}

void modeldigi_osc_init(modeldigi_osc_state *state)
{
    for (unsigned v = 0; v < 3; ++v)
        state->phase[v] = 0;
}


/* Kernel residues in Q15 at distance x (Q15, 0..1 sample) from an event: a unit step leaves
 * (1 - x)^2 / 2 (positive before the event, negative after), a unit slope change (1 - x)^3 / 6. */
static int32_t step_residue(int32_t x)
{
    int32_t a = 32768 - x;
    return (a * a) >> 16;
}

static int32_t corner_residue(int32_t x)
{
    int32_t a = 32768 - x;
    return (((a * a) >> 15) * a >> 15) / 6;
}

/* Corrections for an event at phase e on the samples either side of it: a step of step_q14 * 2^14
 * output units and a slope change of bend output units a sample. x is the distance from the event
 * in samples, Q15: (e - p) / inc before it, (p - e) / inc after. */
#define EVENT(e, step_q14, bend)                                                          \
    do {                                                                                      \
        uint32_t ahead = (e) - p - 1, behind = p - (e);                                       \
        if (ahead < inc) {                                                                    \
            int32_t x = (int32_t)(((ahead >> shift) << 15) / (inc >> shift));                 \
            y += ((step_q14) * step_residue(x) >> 1) + (((bend) >> 11) * corner_residue(x) >> 4); \
        }                                                                                     \
        if (behind < inc) {                                                                   \
            int32_t x = (int32_t)(((behind >> shift) << 15) / (inc >> shift));                \
            y += (((bend) >> 11) * corner_residue(x) >> 4) - ((step_q14) * step_residue(x) >> 1); \
        }                                                                                     \
    } while (0)

void modeldigi_osc_render(modeldigi_osc_state *state, const modeldigi_osc_params *params,
                          int32_t *output, size_t frames)
{
    for (size_t i = 0; i < frames; ++i)
        output[i] = 0;
    for (unsigned v = 0; v < 3; ++v) {
        uint32_t p = state->phase[v], inc = increment(params->frequency_q16_16[v]);
        unsigned shift = 0;   /* inc >> shift fits 15 bits, for the event fractions */
        while (inc >> shift >= 32768)
            ++shift;
        unsigned shape = params->waveform[v] < 7 ? params->waveform[v] : 0;
        if (shape < 4) {
            int32_t tri = scaled(lines[shape].tri, params->level[v]), saw = scaled(lines[shape].saw, params->level[v]);
            /* The reset steps the saw part by 2 (Q16 units 2^17, so 2^14 output units per coefficient
             * unit); the triangle bends by 8 a cycle, inc >> 13 Q16 units a sample, at 0 and at half,
             * which is (inc >> 16) * tri output units. */
            int32_t bend = (int32_t)(inc >> 16) * tri;
            for (size_t i = 0; i < frames; ++i, p += inc) {
                /* Q16 units: the saw falls from +1 to -1, the triangle runs +1, -1, +1. */
                uint32_t fold = p < HALF ? p : ~p;
                int32_t s = 65536 - (int32_t)(((p >> 14) + 1) >> 1);
                int32_t t = 65536 - (int32_t)(((fold >> 13) + 1) >> 1);
                int32_t y = (t * tri + s * saw) >> 3;
                EVENT(0u, saw, -bend);
                if (tri)
                    EVENT(HALF, 0, bend);
                output[i] += y;
            }
        } else {
            int32_t high = scaled(pulses[shape - 4].high, params->level[v]) << 13;
            int32_t low = -(int32_t)mulhi((uint32_t)high, pulses[shape - 4].ratio);   /* zero mean */
            int32_t step = (high - low) >> 14;
            uint32_t half = pulses[shape - 4].half;
            for (size_t i = 0; i < frames; ++i, p += inc) {
                int32_t y = p + half < 2 * half ? high : low;
                EVENT(0u - half, step, 0);
                EVENT(half, -step, 0);
                output[i] += y;
            }
        }
        state->phase[v] = p;
    }
}
