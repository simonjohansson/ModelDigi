/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "oscillator.h"

/* Host refinement changes the complete engine rate; no public quality control.
 * shortcut: factor 8 is a host-quality candidate; select a device factor after full-render timing. */
#ifndef MODELDIGI_OVERSAMPLE
#define MODELDIGI_OVERSAMPLE 8
#endif
#if MODELDIGI_OVERSAMPLE != 1 && MODELDIGI_OVERSAMPLE != 2 && MODELDIGI_OVERSAMPLE != 4 && MODELDIGI_OVERSAMPLE != 8 && MODELDIGI_OVERSAMPLE != 16
#error "MODELDIGI_OVERSAMPLE must be 1, 2, 4, 8 or 16"
#endif

#define ONE (UINT64_C(1) << 48)
#define MASK (ONE - 1)
#define Q INT32_C(67108864)
#define RATE (48000u * MODELDIGI_OVERSAMPLE)
#define SAW INT32_C(63603062)
#define TRI INT32_C(59677012)
#define RESIDUE INT32_C(1133630)
#define MIX_TRI INT32_C(42694753)
#define MIX_SAW INT32_C(9994698)

static const uint64_t widths[3] = {
    UINT64_C(137922738588221), UINT64_C(87257242780303), UINT64_C(54887620458578)
};

/* 97-tap Blackman-windowed halfband: sinc(k/2) * (.42+.5*cos(pi*k/48)
 * +.08*cos(2*pi*k/48)); Q30, odd distances from center. Center fixes DC gain. */
static const int32_t halfband[24] = {
    341182827, -112139226, 65412640, -44780229, 32900357, -25054594,
    19435097, -15201818, 11910993, -9304609, 7220318, -5548558,
    4210528, -3146229, 2307764, -1655493, 1155849, -780094,
    503642, -305675, 168929, -79510, 26718, -2808
};

static int32_t mul(int32_t a, int32_t b)
{
    return (int32_t)((int64_t)a * b / Q);
}

static int32_t value(unsigned shape, uint64_t phase)
{
    int32_t p = (int32_t)(phase >> 22);
    int32_t saw = mul(SAW, Q - 2 * p);
    int32_t triangle = mul(TRI, Q - 4 * (p < Q / 2 ? p : Q - p))
                     + mul(RESIDUE, saw);
    switch (shape) {
    case 0: return triangle;
    case 1: return mul(MIX_TRI, triangle) + mul(MIX_SAW, saw);
    case 2: return -saw;
    case 3: return saw;
    default: {
        uint64_t width = widths[shape - 4];
        bool high = phase < width / 2 || phase >= ONE - width / 2;
        return (high ? 2 * Q : 0) - (int32_t)(width >> 21);
    }
    }
}

static int32_t slope(unsigned shape, uint64_t phase)
{
    int32_t triangle = (phase < ONE / 2 ? -4 * TRI : 4 * TRI)
                     + mul(RESIDUE, -2 * SAW);
    switch (shape) {
    case 0: return triangle;
    case 1: return mul(MIX_TRI, triangle) + mul(MIX_SAW, -2 * SAW);
    case 2: return 2 * SAW;
    case 3: return -2 * SAW;
    default: return 0;
    }
}

/* Two-sample polynomial step/slope residuals; previous sample is held back.
 * Q26 audio, Q16 time since event. Products stay in signed 64 bits. */
static void correct(int32_t *previous, int32_t *current,
                    uint32_t t, int32_t jump, int32_t change)
{
    int64_t a = t, b = 65536 - t;
    int64_t aa = a * a / 65536, bb = b * b / 65536;
    *previous += (int32_t)((int64_t)jump * aa / 131072);
    *current -= (int32_t)((int64_t)jump * bb / 131072);
    *previous += (int32_t)((int64_t)change * (aa * a / 65536) / 393216);
    *current += (int32_t)((int64_t)change * (bb * b / 65536) / 393216);
}

static void edge(int32_t *previous, int32_t *current, uint64_t old,
                 uint64_t increment, uint64_t at, int32_t jump, int32_t change)
{
    uint64_t distance = (at - old) & MASK;
    if (distance && distance <= increment) {
        uint32_t t = (uint32_t)(((increment - distance) << 16) / increment);
        int32_t m = (int32_t)((int64_t)change * (int64_t)(increment >> 18) / (INT64_C(1) << 30));
        correct(previous, current, t, jump, m);
    }
}

static int32_t tick(modeldigi_osc_state *s, unsigned v, unsigned shape, uint64_t increment)
{
    uint64_t old = s->voice[v].phase;
    uint64_t phase = (old + increment) & MASK;
    int32_t y = value(shape, phase);
    if (!s->voice[v].primed) {
        s->voice[v].previous = value(shape, old);
        s->voice[v].primed = true;
    } else if (s->voice[v].waveform != shape || s->voice[v].increment != increment) {
        unsigned before = s->voice[v].waveform;
        int32_t jump = value(shape, old) - value(before, old);
        int32_t change = (int32_t)(((int64_t)slope(shape, old) * (int64_t)(increment >> 18)
                      - (int64_t)slope(before, old) * (int64_t)(s->voice[v].increment >> 18)) / (INT64_C(1) << 30));
        correct(&s->voice[v].previous, &y, 65536, jump, change);
    }
    if (shape < 4) {
        int32_t reset = shape == 0 ? mul(RESIDUE, 2 * SAW)
                      : shape == 1 ? mul(MIX_TRI, mul(RESIDUE, 2 * SAW)) + mul(MIX_SAW, 2 * SAW)
                      : shape == 2 ? -2 * SAW : 2 * SAW;
        int32_t knee = shape == 0 ? 8 * TRI : shape == 1 ? mul(MIX_TRI, 8 * TRI) : 0;
        edge(&s->voice[v].previous, &y, old, increment, 0, reset, -knee);
        if (knee)
            edge(&s->voice[v].previous, &y, old, increment, ONE / 2, 0, knee);
    } else {
        uint64_t width = widths[shape - 4];
        edge(&s->voice[v].previous, &y, old, increment, width / 2, -2 * Q, 0);
        edge(&s->voice[v].previous, &y, old, increment, ONE - width / 2, 2 * Q, 0);
    }
    int32_t output = s->voice[v].previous;
    s->voice[v].previous = y;
    s->voice[v].phase = phase;
    s->voice[v].increment = increment;
    s->voice[v].waveform = (uint8_t)shape;
    return output;
}

static uint32_t bounded(uint32_t hz)
{
    return hz < 4096 ? 4096 : hz > 20000u * 65536 ? 20000u * 65536 : hz;
}

static uint64_t increment(uint32_t hz)
{
    return ((uint64_t)hz << 32) / RATE;
}

static int32_t control(unsigned shape, uint64_t phase)
{
    if (shape >= 4) {
        uint64_t width = widths[shape - 4];
        return phase >= width / 2 && phase < ONE - width / 2 ? Q : 0;
    }
    int32_t bias = shape == 0 ? 1751541 : shape == 2 ? -234881 : -409364;
    return -value(shape, phase) / 2 + bias;
}

static uint32_t modulate(uint32_t hz, int32_t octaves)
{
    /* Degree-six exp(ln(2)*fraction), Q30; integer octave handled exactly. */
    static const int32_t c[7] = {
        1073741824, 744261118, 257941248, 59597083, 10327387, 1431680, 165394
    };
    int whole = octaves / 65536, fraction = octaves % 65536;
    if (fraction < 0) { fraction += 65536; whole--; }
    int64_t factor = c[6];
    for (int i = 5; i >= 0; --i)
        factor = c[i] + factor * fraction / 65536;
    uint64_t scaled = ((uint64_t)hz * (uint64_t)factor) >> 30;
    scaled = whole >= 0 ? scaled << whole : scaled >> -whole;
    return bounded(scaled > UINT32_MAX ? UINT32_MAX : (uint32_t)scaled);
}

static int32_t decimate(modeldigi_osc_state *s, unsigned stage, int32_t sample)
{
    unsigned at = s->filter[stage].position;
    s->filter[stage].samples[at] = sample;
    s->filter[stage].position = (at + 1) % 97;
    if (!s->filter[stage].parity) return 0;
    int64_t total = (int64_t)s->filter[stage].samples[(at + 97 - 48) % 97] * 536868186;
    for (unsigned j = 0; j < 24; ++j) {
        unsigned distance = 2 * j + 1;
        int64_t pair = (int64_t)s->filter[stage].samples[(at + 97 - 48 + distance) % 97]
                     + s->filter[stage].samples[(at + 97 - 48 - distance) % 97];
        total += pair * halfband[j];
    }
    return (int32_t)(total / (INT64_C(1) << 30));
}

void modeldigi_osc_init(modeldigi_osc_state *state)
{
    unsigned char *bytes = (unsigned char *)state;
    for (size_t i = 0; i < sizeof(*state); ++i) bytes[i] = 0;
    state->voice[1].phase = ONE / 3;
    state->voice[2].phase = 2 * (ONE / 3);
}

void modeldigi_osc_render(modeldigi_osc_state *s, const modeldigi_osc_params *p,
                         int32_t *output, size_t frames)
{
    if (!frames) return;
    unsigned shape[3];
    uint32_t base[3];
    uint64_t step[3];
    for (unsigned v = 0; v < 3; ++v) {
        shape[v] = p->waveform[v] <= 6 ? p->waveform[v] : 0;
        base[v] = bounded(p->frequency_q16_16[v]);
        step[v] = increment(base[v]);
    }
    uint32_t depth = p->modulation_depth_q16_16 > 131072 ? 131072 : p->modulation_depth_q16_16;
    for (size_t frame = 0; frame < frames; ++frame) {
        for (unsigned sub = 0; sub < MODELDIGI_OVERSAMPLE; ++sub) {
            int32_t octaves = p->modulation_enabled && depth
                ? (int32_t)((int64_t)control(shape[2], s->voice[2].phase) * depth / Q) : 0;
            int64_t sum = 0;
            for (unsigned v = 0; v < 3; ++v) {
                uint64_t ds = octaves && (v != 2 || p->osc3_control)
                    ? increment(modulate(base[v], octaves)) : step[v];
                sum += (int64_t)tick(s, v, shape[v], ds) * p->level[v] / 65535;
            }
            /* Q26 sum to Q31 with the approved 1/16 gain. */
            int32_t sample = (int32_t)(sum * 2);
            unsigned remaining = MODELDIGI_OVERSAMPLE;
            for (unsigned stage = 0; remaining > 1; ++stage, remaining /= 2) {
                unsigned parity = s->filter[stage].parity;
                sample = decimate(s, stage, sample);
                s->filter[stage].parity = parity ^ 1u;
                if (!parity) break;
            }
            if (remaining == 1) output[frame] = sample;
        }
    }
}
