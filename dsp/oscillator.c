/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Oscillator bank: three phase accumulators at 48 kHz, summed. Each step and corner is smoothed by
 * a windowed-sinc kernel reaching EDGE_HALF samples either side (tabulated BLEP and BLAMP residues),
 * so the output is the ideal waveform low-passed by that kernel, sampled, EDGE_HALF samples late; no
 * oversampling.
 * Uses 32-bit arithmetic only: a mod links no libgcc 64-bit helpers.
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
#include "edge_kernel.h"

#define HALF   0x80000000u
#define LO_INC 5592u            /* 1/16 Hz */
#define HI_INC 1789569706u      /* 20000 Hz */

/* Per shape, levels / 16 in units of 2^-18 full scale: triangle and saw parts of the line shapes,
 * and the pulses' half widths. From the reference: saw peak 0.94775948, triangle 0.88925678 plus
 * 0.0168924 saw, combination 0.6362014 triangle plus 0.1489326 saw, pulses high 49%, 31% and 19.5%
 * of the cycle. */
static const struct { int32_t tri, saw; } lines[4] = {
    { 14570, 262 }, { 9269, 2480 }, { 0, -15528 }, { 0, 15528 },
};
static const uint32_t pulse_half[3] = { 0x3eb851ecu, 0x27ae147bu, 0x18f5c28fu };   /* half widths, Q0.32 */
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
static inline __attribute__((always_inline)) uint32_t mulhi(uint32_t a, uint32_t b)
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
static inline __attribute__((always_inline)) uint32_t modulate(uint32_t inc, int32_t octaves)
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

/* floor(d * 65536 / inc) for 0 <= d < inc < 2^31, by shift and subtract. */
static inline __attribute__((always_inline)) uint32_t fraction(uint32_t d, uint32_t inc)
{
    uint32_t q = 0;
    for (unsigned k = 0; k < 16; ++k) {
        d <<= 1;
        q <<= 1;
        if (d >= inc) {
            d -= inc;
            q |= 1;
        }
    }
    return q;
}

/* The same quotient for repeated steps: floor(2^48 / inc), or 0 for inc <= 2^16, where a plain
 * 32-bit division is exact. */
static uint32_t reciprocal(uint32_t inc)
{
    if (inc <= 65536)
        return 0;
    uint32_t whole = 0xffffffffu / inc, rest = 0xffffffffu % inc + 1;   /* 2^32 = whole * inc + rest */
    if (rest == inc) {
        whole += 1;
        rest = 0;
    }
    return whole << 16 | fraction(rest, inc);
}

/* floor(d * 65536 / inc) from inc's reciprocal: the estimate is low by at most one, and the
 * remainder, below 2 * inc < 2^32, decides. */
static inline __attribute__((always_inline)) uint32_t quotient(uint32_t d, uint32_t inc, uint32_t recip)
{
    if (!recip)
        return (d << 16) / inc;
    uint32_t q = mulhi(d, recip);
    return (d << 16) - q * inc >= inc ? q + 1 : q;
}

#define TAPS (2 * EDGE_HALF)
typedef char tail_layout[sizeof ((modeldigi_osc_state *)0)->tail == (TAPS - 1) * sizeof(int32_t) ? 1 : -1];

/* The reciprocal of the step it was computed for. */
typedef struct {
    uint32_t inc, recip;
} divisor;

/* An event `behind` (0 <= behind < last) before a sample whose previous step was last: add its step
 * of step_q14 * 2^14 output units and its slope change of 8 * kink Q16 units a cycle, (last >> 16)
 * * kink output units a sample, to the TAPS samples w[0..TAPS - 1] around it, the sample after it
 * at w[EDGE_HALF]. */
/* w[j] += (f1 * residue_j + f2 * step_j) >> shift for the TAPS packed pairs of a row, with 16-bit
 * f1 and f2: every product fits 32 bits, as does their sum. */
#if defined(__mcoldfire__) && !defined(MODELDIGI_NO_ASM)
#define TAPS_ASM 1
#define MODELDIGI_STR2(x) #x
#define MODELDIGI_STR(x) MODELDIGI_STR2(x)
void modeldigi_edge_taps(int32_t *w, const int32_t *pairs, uint32_t factors, uint32_t shift);
/* EMAC, integer mode (the render sets MACSR): two multiply-accumulates a tap, the odd tap's pair
 * loading on the even tap's first, alternating accumulators. factors: f1 high, f2 low. */
__asm__(
    "	.pushsection .text\n"
    "	.even\n"
    "	.type	modeldigi_edge_taps, @function\n"
    "modeldigi_edge_taps:\n"
    "	lea	-16(%sp),%sp\n"
    "	movem.l	%d2-%d5,(%sp)\n"
    "	move.l	20(%sp),%a0\n"
    "	move.l	24(%sp),%a1\n"
    "	move.l	28(%sp),%d1\n"
    "	move.l	32(%sp),%d2\n"
    "	moveq	#" MODELDIGI_STR(EDGE_HALF) ",%d5\n"
    "	move.l	(%a1)+,%d3\n"
    "1:\n"
    "	mac.w	%d3u,%d1u,(%a1)+,%d0,%acc0\n"
    "	mac.w	%d3l,%d1l,%acc0\n"
    "	mac.w	%d0u,%d1u,%acc1\n"
    "	mac.w	%d0l,%d1l,%acc1\n"
    "	movclr.l	%acc0,%d4\n"
    "	asr.l	%d2,%d4\n"
    "	add.l	%d4,(%a0)+\n"
    "	movclr.l	%acc1,%d4\n"
    "	asr.l	%d2,%d4\n"
    "	add.l	%d4,(%a0)+\n"
    "	subq.l	#1,%d5\n"
    "	beq.s	2f\n"
    "	move.l	(%a1)+,%d3\n"
    "	bra.s	1b\n"
    "2:\n"
    "	movem.l	(%sp),%d2-%d5\n"
    "	lea	16(%sp),%sp\n"
    "	rts\n"
    "	.size	modeldigi_edge_taps, .-modeldigi_edge_taps\n"
    "	.popsection\n");
static inline __attribute__((always_inline)) void taps(int32_t *w, const int32_t *pairs, int32_t f1, int32_t f2, uint32_t shift)
{
    modeldigi_edge_taps(w, pairs, (uint32_t)f1 << 16 | ((uint32_t)f2 & 0xffff), shift);
}
#else
static inline __attribute__((always_inline)) void taps(int32_t *w, const int32_t *pairs, int32_t f1, int32_t f2, uint32_t shift)
{
    for (unsigned j = 0; j < TAPS; ++j)
        w[j] += (f1 * (pairs[j] >> 16) + f2 * (int16_t)pairs[j]) >> shift;
}
#endif

/* Out of line: one copy of the corrections serves every edge, which the instruction cache rewards. */
static __attribute__((noinline)) void event(int32_t *w, uint32_t behind, uint32_t last, divisor *q,
                                            int32_t step_q14, int32_t kink)
{
    if (q->inc != last) {
        q->inc = last;
        q->recip = reciprocal(last);
    }
    int32_t d = (int32_t)quotient(behind, last, q->recip), row = d >> 10, f = d & 1023;
    /* Residue j is pair high + low * f / 1024: with the step folded into both factors once, each
     * sample takes two 16-bit products. The row sums give what was added, so the step's residues
     * can be completed to their exact sum d - 1/2: the sampled output keeps the ideal waveform's
     * mean. */
    int32_t s2 = step_q14 * f >> 10;
    taps(w, step_pairs[row], step_q14, s2, 1);
    w[EDGE_HALF] += (step_q14 * (d - 32768) >> 2) - ((step_q14 * step_sums[row][0] + s2 * step_sums[row][1]) >> 1);
    if (kink) {
        /* (last >> 16) * kink >> 14 stays within 16 bits: (inc >> 16) * tri peaks at 27306 * 14570. */
        int32_t bend = ((int32_t)(last >> 16) * kink) >> 14, b2 = bend * f >> 10;
        taps(w, bend_pairs[row], bend, b2, 3);
        int32_t target = (int32_t)((uint32_t)d * (uint32_t)d >> 16) - d + 10923 + BEND_SUM_OFFSET;
        w[EDGE_HALF] += (bend * target - bend * bend_sums[row][0] - b2 * bend_sums[row][1]) >> 3;
    }
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
        /* A step of 2 / 16 full scale, 2^14 units of 2^14; levels from it, so edges and levels
         * agree exactly and high * duty + low * (1 - duty) is zero within a unit. */
        c->half = pulse_half[shape - 4];
        c->step = scaled(16384, level);
        c->high = (int32_t)mulhi((uint32_t)c->step << 14, 0u - 2 * c->half);
        c->low = c->high - (c->step << 14);
    }
}

static inline __attribute__((always_inline)) int pulse_high(uint32_t p, uint32_t half)
{
    return p + half < 2 * half;
}

/* Q16 units: the saw falls from +1 to -1, the triangle runs +1, -1, +1. */
static inline __attribute__((always_inline)) int32_t saw_unit(uint32_t p)
{
    return 65536 - (int32_t)(((p >> 14) + 1) >> 1);
}

static inline __attribute__((always_inline)) int32_t tri_unit(uint32_t p)
{
    uint32_t fold = p < HALF ? p : ~p;
    return 65536 - (int32_t)(((fold >> 13) + 1) >> 1);
}

/* signed a * b / 2^32 for b >= 0. */
static int32_t smulhi(int32_t a, uint32_t b)
{
    int32_t m = (int32_t)mulhi((uint32_t)(a < 0 ? -a : a), b);
    return a < 0 ? -m : m;
}

/* One sample of a line shape or a pulse at phase p into w[EDGE_HALF], with the corrections for edges
 * in the step just taken, of last, spread over w[0..TAPS - 1]. */
static inline __attribute__((always_inline)) void line_sample(const voice *c, int32_t *w, uint32_t p, uint32_t last, divisor *q)
{
    w[EDGE_HALF] += (tri_unit(p) * c->tri + saw_unit(p) * c->saw) >> 3;
    if (p < last)
        event(w, p, last, q, c->saw, -c->tri);
    if (c->tri && p - HALF < last)
        event(w, p - HALF, last, q, 0, c->tri);
}

static inline __attribute__((always_inline)) void pulse_sample(const voice *c, int32_t *w, uint32_t p, uint32_t last, divisor *q)
{
    w[EDGE_HALF] += pulse_high(p, c->half) ? c->high : c->low;
    if (p + c->half < last)
        event(w, p + c->half, last, q, c->step, 0);
    if (p - c->half < last)
        event(w, p - c->half, last, q, -c->step, 0);
}

/* One voice over n samples into w (sample i's own value at w[i + EDGE_HALF]), from its phase and previous
 * step. Its step is modulated by octaves[i] where octaves is given and nonzero; source, when
 * given, records oscillator 3's own modulation from each sample's phase before the step (CONTROL
 * feeding it back when control is set). */
static void run(const voice *c, uint32_t *phase, uint32_t *previous, uint32_t step, int32_t *octaves,
                const voice *source, uint32_t depth, int control, int32_t *w, unsigned n)
{
    uint32_t p = *phase, last = *previous;
    divisor q = { 0, 0 };   /* computed at the first edge */
    if (!octaves) {
        /* Unmodulated: a fixed step, so a loop per shape class. */
        if (c->shape < 4)
            for (unsigned i = 0; i < n; ++i, p += step, last = step)
                line_sample(c, w + i, p, last, &q);
        else
            for (unsigned i = 0; i < n; ++i, p += step, last = step)
                pulse_sample(c, w + i, p, last, &q);
        *phase = p;
        *previous = last;
        return;
    }
    for (unsigned i = 0; i < n; ++i) {
        int32_t oct = 0;
        if (source) {
            if (source->shape < 4) {
                int32_t value = tri_unit(p) * lines[source->shape].tri + saw_unit(p) * lines[source->shape].saw;   /* Q30 */
                oct = smulhi(bias[source->shape] - value / 2, depth << 2);
            } else {
                oct = pulse_high(p, source->half) ? 0 : (int32_t)depth;
            }
            octaves[i] = oct;
            if (!control)
                oct = 0;
        } else {
            oct = octaves[i];
        }
        uint32_t inc = oct ? modulate(step, oct) : step;
        if (c->shape < 4)
            line_sample(c, w + i, p, last, &q);
        else
            pulse_sample(c, w + i, p, last, &q);
        p += inc;
        last = inc;
    }
    *phase = p;
    *previous = last;
}

#ifdef MODELDIGI_ANALOG
/* Analog character, a design choice rather than a measurement: each oscillator's pitch wanders
 * slowly and independently, as a smoothed Ornstein-Uhlenbeck process stepped every DRIFT_FRAMES
 * on an absolute frame grid. Uniform noise of +-2^10 units (2^-24 octave) a step against a leak of
 * 1/4096 gives about 1.9 cents RMS with a correlation time of about 2.7 s; a 32-step smoother
 * removes the steps, and the drift is held within +-6 cents. */
#include "mixer_curve.h"
#define DRIFT_FRAMES 32
#define DRIFT_LIMIT 83886      /* 6 cents in 2^-24 octave */
static uint32_t drift_instances;   /* seeds banks apart */

static int32_t limit(int32_t x, int32_t bound)
{
    return x < -bound ? -bound : x > bound ? bound : x;
}

static void drift_step(modeldigi_osc_state *s)
{
    for (unsigned v = 0; v < 3; ++v) {
        uint32_t x = s->seed;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        s->seed = x;
        s->wander[v] = limit(s->wander[v] - (s->wander[v] >> 12) + ((int32_t)x >> 21), 2 * DRIFT_LIMIT);
        s->drift[v] = limit(s->drift[v] + ((s->wander[v] - s->drift[v]) >> 5), DRIFT_LIMIT);
    }
}
#endif

#ifdef MODELDIGI_ANALOG
/* The mixer, a circuit-derived model with a chosen drive (docs/analog.md): the sources' sum, as the
 * filter's input pair receives it, tanh(drive * s) / drive in engine units, unity for small
 * signals; then a 5 Hz DC blocker, since compressing asymmetric pulses shifts their mean. The bank's
 * sums stay below about 0.35 of full scale, so every product below fits 32 bits. */
/* A sixth of the distortion, saturate(q) - q, at q: one 32-bit product and an interpolated table.
 * Table steps (at most 5.8e5) times a 12-bit fraction fit 32 bits; the input is read to 2^10 units
 * (-126 dBFS), within which the curve can step back. */
static int32_t distortion6(int32_t q)
{
    uint32_t m = q < 0 ? 0u - (uint32_t)q : (uint32_t)q, a = ((m >> 10) * MIX_DRIVE) >> 2, i = a >> 18;
    int32_t r = i >= 512 ? mix_distortion[512]
                         : mix_distortion[i] - (int32_t)((uint32_t)(mix_distortion[i] - mix_distortion[i + 1]) * ((a >> 6) & 4095) >> 12);
    return q < 0 ? -r : r;
}

/* One mixer sample: the input passes straight through and only the distortion is averaged over the
 * sample interval (Simpson's rule, a division-free antiderivative anti-aliasing), which keeps
 * quiet signals exact. */
static int32_t mixer(modeldigi_osc_state *s, int32_t q)
{
    int32_t now = distortion6(q), half = distortion6((q >> 1) + (s->mix_in >> 1));
    int32_t average = s->mix_residue + 4 * half + now;
    s->mix_in = q;
    s->mix_residue = now;
    return q + average;
}

/* 5 Hz one-pole DC blocker: out = in - previous in + out * (1 - 1341 / 2^21). The leak's product
 * fits 32 bits for outputs below 0.75 of full scale; it rounds to zero within 2^10 units (-126 dBFS). */
static int32_t dc_block(modeldigi_osc_state *s, int32_t x)
{
    s->dc_out = x - s->dc_in + s->dc_out - (((s->dc_out >> 10) * 1341 + 1024) >> 11);
    s->dc_in = x;
    return s->dc_out;
}
#endif

void modeldigi_osc_init(modeldigi_osc_state *state)
{
    for (unsigned v = 0; v < 3; ++v) {
        state->phase[v] = v * 0x55555555u;   /* 0, 1/3 and 2/3 of a cycle */
        state->last[v] = 0;
    }
    for (unsigned k = 0; k < TAPS - 1; ++k)
        state->tail[k] = 0;
#ifdef MODELDIGI_ANALOG
    state->frame = 0;
    state->seed = 2654435761u * ++drift_instances | 1;
    for (unsigned v = 0; v < 3; ++v)
        state->wander[v] = state->drift[v] = 0;
    state->mix_in = state->mix_residue = 0;
    state->dc_in = state->dc_out = 0;
#endif
}

void modeldigi_osc_render(modeldigi_osc_state *state, const modeldigi_osc_params *params,
                          int32_t *output, size_t frames)
{
#ifdef TAPS_ASM
    /* The firmware's render uses the EMAC too: keep its MACSR, and run the taps in integer mode. */
    uint32_t macsr;
    __asm__ volatile ("move.l %%macsr,%0\n\tmove.l %1,%%macsr" : "=&d"(macsr) : "d"(0u));
#endif
    voice c[3];
    uint32_t step[3];
    for (unsigned v = 0; v < 3; ++v) {
        describe(&c[v], params->waveform[v] < 7 ? params->waveform[v] : 0, params->level[v]);
        step[v] = increment(params->frequency_q16_16[v]);
    }
    uint32_t depth = params->modulation_depth_q16_16 > 131072 ? 131072 : params->modulation_depth_q16_16;
    int modulated = params->modulation_enabled && depth;
    for (size_t done = 0; done < frames;) {
        unsigned n = frames - done < 32 ? (unsigned)(frames - done) : 32;
        uint32_t base[3] = { step[0], step[1], step[2] };
#ifdef MODELDIGI_ANALOG
        /* Chunks follow the drift grid, so the split of a render does not change its output. */
        unsigned left = DRIFT_FRAMES - state->frame % DRIFT_FRAMES;
        if (left == DRIFT_FRAMES)
            drift_step(state);
        if (n > left)
            n = left;
        for (unsigned v = 0; v < 3 && params->analog; ++v)
            base[v] = modulate(step[v], state->drift[v] >> 8);
        state->frame += n;
#endif
        int32_t w[32 + TAPS - 1], octaves[32];   /* w[k]: sample k - EDGE_HALF of this block */
        for (unsigned k = 0; k < TAPS - 1; ++k)
            w[k] = state->tail[k];
        for (unsigned k = TAPS - 1; k < n + TAPS - 1; ++k)
            w[k] = 0;
        /* Oscillator 3 first: its phases set the block's modulation. */
        run(&c[2], &state->phase[2], &state->last[2], base[2], modulated ? octaves : 0, modulated ? &c[2] : 0, depth,
            params->osc3_control, w, n);
        for (unsigned v = 0; v < 2; ++v)
            run(&c[v], &state->phase[v], &state->last[v], base[v], modulated ? octaves : 0, 0, 0, 0, w, n);
#ifdef MODELDIGI_ANALOG
        if (params->analog) {
            for (unsigned i = 0; i < n; ++i)
                output[done + i] = dc_block(state, mixer(state, w[i]));
        } else {
            /* Off is the clean output; turning it on starts the mixer as a fresh bank does. */
            for (unsigned i = 0; i < n; ++i)
                output[done + i] = w[i];
            state->mix_in = state->mix_residue = state->dc_in = state->dc_out = 0;
        }
#else
        for (unsigned i = 0; i < n; ++i)
            output[done + i] = w[i];
#endif
        for (unsigned k = 0; k < TAPS - 1; ++k)
            state->tail[k] = w[n + k];
        done += n;
    }
#ifdef TAPS_ASM
    __asm__ volatile ("move.l %0,%%macsr" : : "d"(macsr));
#endif
}
