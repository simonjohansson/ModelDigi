/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Oscillator bank: three phase accumulators at 48 kHz, summed. Each step and corner is smoothed by
 * a triangular kernel one sample wide on each side (2-point PolyBLEP and PolyBLAMP), so the output
 * is the ideal waveform with a sinc^2 response, sampled; no oversampling. Uses 32-bit arithmetic
 * only: a mod links no libgcc 64-bit helpers.
 *
 * Shapes follow the frozen reference conventions: the saw falls from its peak and resets at
 * phase 0, the triangle peaks at phase 0, pulses are centred on phase 0 with zero mean and a
 * high-to-low step of 2. The bank output is those levels / 16.
 *
 * Modulation: oscillator 3, before its level, sets a pitch factor of 2^octaves each sample from
 * its phase before the step. Lines give depth * (-value / 2 + bias), pulses depth while their
 * audio is low and 0 while high. The factor multiplies oscillators 1 and 2, and oscillator 3 too
 * when CONTROL is on, clamped to 1/16..20000 Hz. */
#include "oscillator.h"

#define HALF   0x80000000u
#define LO_INC 5592u            /* 1/16 Hz */
#define HI_INC 1789569706u      /* 20000 Hz */

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
/* Modulation bias of the line shapes, Q30 octaves per octave of depth. */
static const int32_t bias[4] = { 28024656, 0, -3758096, -6549824 };
/* 2^(i/64), Q15. */
static const uint32_t exp2_q15[65] = { 32768, 33125, 33486, 33850, 34219, 34591, 34968, 35349, 35734, 36123, 36516, 36914, 37316, 37722, 38133, 38548, 38968, 39392, 39821, 40255, 40693, 41136, 41584, 42037, 42495, 42958, 43425, 43898, 44376, 44859, 45348, 45842, 46341, 46846, 47356, 47871, 48393, 48920, 49452, 49991, 50535, 51085, 51642, 52204, 52773, 53347, 53928, 54515, 55109, 55709, 56316, 56929, 57549, 58176, 58809, 59449, 60097, 60751, 61413, 62081, 62757, 63441, 64132, 64830, 65536 };

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

/* An increment times 2^(octaves / 65536), clamped to the frequency bounds. */
static uint32_t modulate(uint32_t inc, int32_t octaves)
{
    uint32_t frac = (uint32_t)octaves & 0xffff, i = frac >> 10;
    uint32_t m = exp2_q15[i] + (((uint32_t)(exp2_q15[i + 1] - exp2_q15[i]) * (frac & 1023)) >> 10);
    uint32_t r = inc + mulhi(inc, (m - 32768) << 17);
    for (int32_t whole = octaves >> 16; whole > 0 && r < HI_INC; --whole)
        r <<= 1;
    int32_t down = -(octaves >> 16);
    if (down > 0)
        r = down < 32 ? r >> down : 0;
    return r < LO_INC ? LO_INC : r > HI_INC ? HI_INC : r;
}

/* d / inc in Q15, for 0 <= d < inc. */
static int32_t fraction(uint32_t d, uint32_t inc)
{
    unsigned shift = inc >> 15 ? 17u - (unsigned)__builtin_clz(inc) : 0;
    return (int32_t)(((d >> shift) << 15) / (inc >> shift));
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

/* Corrections for an event at phase e on the samples either side of it, for a voice now at p that
 * steps by inc next and stepped by last before: a step of step_q14 * 2^14 output units and a
 * triangle corner whose slope changes by 8 * kink Q16 units a cycle, (inc >> 16) * kink output
 * units a sample. */
static int32_t event(uint32_t e, uint32_t p, uint32_t inc, uint32_t last, int32_t step_q14, int32_t kink)
{
    int32_t y = 0;
    uint32_t ahead = e - p - 1, behind = p - e;
    if (ahead < inc) {
        int32_t x = fraction(ahead, inc), bend = (int32_t)(inc >> 16) * kink;
        y += (step_q14 * step_residue(x) >> 1) + ((bend >> 11) * corner_residue(x) >> 4);
    }
    if (behind < last) {
        int32_t x = fraction(behind, last), bend = (int32_t)(last >> 16) * kink;
        y += ((bend >> 11) * corner_residue(x) >> 4) - (step_q14 * step_residue(x) >> 1);
    }
    return y;
}

typedef struct {
    unsigned shape;
    int32_t tri, saw, high, low, step;   /* line parts, or pulse levels and step (Q14 units) */
    uint32_t half;
} voice;

static void describe(voice *c, unsigned shape, uint16_t level)
{
    c->shape = shape;
    if (shape < 4) {
        c->tri = scaled(lines[shape].tri, level);
        c->saw = scaled(lines[shape].saw, level);
    } else {
        c->high = scaled(pulses[shape - 4].high, level) << 13;
        c->low = -(int32_t)mulhi((uint32_t)c->high, pulses[shape - 4].ratio);   /* zero mean */
        c->step = (c->high - c->low) >> 14;
        c->half = pulses[shape - 4].half;
    }
}

static int pulse_high(uint32_t p, uint32_t half)
{
    return p + half < 2 * half;
}

/* Q16 units: the saw falls from +1 to -1, the triangle runs +1, -1, +1. */
static int32_t saw_unit(uint32_t p)
{
    return 65536 - (int32_t)(((p >> 14) + 1) >> 1);
}

static int32_t tri_unit(uint32_t p)
{
    uint32_t fold = p < HALF ? p : ~p;
    return 65536 - (int32_t)(((fold >> 13) + 1) >> 1);
}

static int32_t sample(const voice *c, uint32_t p, uint32_t inc, uint32_t last)
{
    if (c->shape < 4) {
        int32_t y = (tri_unit(p) * c->tri + saw_unit(p) * c->saw) >> 3;
        y += event(0u, p, inc, last, c->saw, -c->tri);
        if (c->tri)
            y += event(HALF, p, inc, last, 0, c->tri);
        return y;
    }
    return (pulse_high(p, c->half) ? c->high : c->low)
        + event(0u - c->half, p, inc, last, c->step, 0) + event(c->half, p, inc, last, -c->step, 0);
}

/* signed a * b / 2^32 for b >= 0. */
static int32_t smulhi(int32_t a, uint32_t b)
{
    int32_t m = (int32_t)mulhi((uint32_t)(a < 0 ? -a : a), b);
    return a < 0 ? -m : m;
}

void modeldigi_osc_init(modeldigi_osc_state *state)
{
    for (unsigned v = 0; v < 3; ++v) {
        state->phase[v] = v * 0x55555555u;   /* 0, 1/3 and 2/3 of a cycle */
        state->last[v] = 0;
    }
}

void modeldigi_osc_render(modeldigi_osc_state *state, const modeldigi_osc_params *params,
                          int32_t *output, size_t frames)
{
    voice c[3];
    uint32_t step[3];
    for (unsigned v = 0; v < 3; ++v) {
        describe(&c[v], params->waveform[v] < 7 ? params->waveform[v] : 0, params->level[v]);
        step[v] = increment(params->frequency_q16_16[v]);
    }
    uint32_t depth = params->modulation_depth_q16_16 > 131072 ? 131072 : params->modulation_depth_q16_16;
    int modulated = params->modulation_enabled && depth;
    unsigned source = c[2].shape;
    for (size_t i = 0; i < frames; ++i) {
        int32_t octaves = 0;
        if (modulated) {
            uint32_t x = state->phase[2];
            if (source < 4) {
                int32_t value = tri_unit(x) * lines[source].tri + saw_unit(x) * lines[source].saw;   /* Q30 */
                octaves = smulhi(bias[source] - value / 2, depth << 2);
            } else {
                octaves = pulse_high(x, pulses[source - 4].half) ? 0 : (int32_t)depth;
            }
        }
        int32_t y = 0;
        for (unsigned v = 0; v < 3; ++v) {
            uint32_t inc = octaves && (v < 2 || params->osc3_control) ? modulate(step[v], octaves) : step[v];
            y += sample(&c[v], state->phase[v], inc, state->last[v]);
            state->phase[v] += inc;
            state->last[v] = inc;
        }
        output[i] = y;
    }
}
