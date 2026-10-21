/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "oscillator.h"

/* Host refinement changes the complete engine rate; no public quality control. */
#ifndef MODELDIGI_OVERSAMPLE
#define MODELDIGI_OVERSAMPLE 4
#endif
#if MODELDIGI_OVERSAMPLE != 1 && MODELDIGI_OVERSAMPLE != 2 && MODELDIGI_OVERSAMPLE != 4 && MODELDIGI_OVERSAMPLE != 8 && MODELDIGI_OVERSAMPLE != 16
#error "MODELDIGI_OVERSAMPLE must be 1, 2, 4, 8 or 16"
#endif

/* Hot helpers must inline so a voice's running values stay in registers. */
#define HOT static inline __attribute__((always_inline))

/* Device code links no 64-bit runtime helpers: products use 16-bit halves, 64-bit values are only
 * added, compared and shifted by constants. */

/* Phases and increments are Q0.64 cycles: 48 significant bits above 16 zero bits, so a phase wraps
 * naturally and its top word is the 2^-32-cycle phase used for events and shapes. */
#define ONE (UINT64_C(1) << 48)
#define Q INT32_C(67108864)
#define RATE (48000u * MODELDIGI_OVERSAMPLE)
#define SAW INT64_C(63603062)
#define TRI INT64_C(59677012)
#define RESIDUE INT64_C(1133630)
#define MIX_TRI INT64_C(42694753)
#define MIX_SAW INT64_C(9994698)
/* Compile-time products of Q26 constants. */
#define MULQ(a, b) ((a) * (b) / Q)

/* Unscaled Q26 shapes as value = a + b * phase, phase in cycles; triangle and combination split at 1/2. */
#define TRI_A0 (TRI + MULQ(RESIDUE, SAW))
#define TRI_B0 (-4 * TRI - MULQ(RESIDUE, 2 * SAW))
#define TRI_A1 (-3 * TRI + MULQ(RESIDUE, SAW))
#define TRI_B1 (4 * TRI - MULQ(RESIDUE, 2 * SAW))
static const int32_t line_a[4][2] = {
    { TRI_A0, TRI_A1 },
    { MULQ(MIX_TRI, TRI_A0) + MULQ(MIX_SAW, SAW), MULQ(MIX_TRI, TRI_A1) + MULQ(MIX_SAW, SAW) },
    { -SAW, -SAW },
    { SAW, SAW },
};
static const int32_t line_b[4][2] = {
    { TRI_B0, TRI_B1 },
    { MULQ(MIX_TRI, TRI_B0) - MULQ(MIX_SAW, 2 * SAW), MULQ(MIX_TRI, TRI_B1) - MULQ(MIX_SAW, 2 * SAW) },
    { 2 * SAW, 2 * SAW },
    { -2 * SAW, -2 * SAW },
};
/* Value step at the phase reset, and the slope change at the triangle knee (Q26 per cycle). */
static const int32_t reset_step[4] = {
    MULQ(RESIDUE, 2 * SAW), MULQ(MIX_TRI, MULQ(RESIDUE, 2 * SAW)) + MULQ(MIX_SAW, 2 * SAW), -2 * SAW, 2 * SAW
};
static const int32_t knee_slope[4] = { 8 * TRI, MULQ(MIX_TRI, 8 * TRI), 0, 0 };
static const int32_t control_bias[4] = { 1751541, 0, -234881, -409364 };

static const uint64_t widths[3] = {
    UINT64_C(137922738588221), UINT64_C(87257242780303), UINT64_C(54887620458578)
};

/* 2^(i/256), Q15. */
static const uint32_t exp2_q15[257] = {
    32768, 32857, 32946, 33035, 33125, 33215, 33305, 33395, 33486, 33576, 33667, 33759, 33850, 33942, 34034, 34126,
    34219, 34312, 34405, 34498, 34591, 34685, 34779, 34874, 34968, 35063, 35158, 35253, 35349, 35445, 35541, 35637,
    35734, 35831, 35928, 36025, 36123, 36221, 36319, 36417, 36516, 36615, 36715, 36814, 36914, 37014, 37114, 37215,
    37316, 37417, 37518, 37620, 37722, 37824, 37927, 38030, 38133, 38236, 38340, 38444, 38548, 38653, 38757, 38863,
    38968, 39074, 39180, 39286, 39392, 39499, 39606, 39714, 39821, 39929, 40037, 40146, 40255, 40364, 40473, 40583,
    40693, 40804, 40914, 41025, 41136, 41248, 41360, 41472, 41584, 41697, 41810, 41923, 42037, 42151, 42265, 42380,
    42495, 42610, 42726, 42841, 42958, 43074, 43191, 43308, 43425, 43543, 43661, 43780, 43898, 44017, 44137, 44256,
    44376, 44497, 44617, 44738, 44859, 44981, 45103, 45225, 45348, 45471, 45594, 45718, 45842, 45966, 46091, 46216,
    46341, 46467, 46593, 46719, 46846, 46973, 47100, 47228, 47356, 47484, 47613, 47742, 47871, 48001, 48131, 48262,
    48393, 48524, 48655, 48787, 48920, 49052, 49185, 49319, 49452, 49586, 49721, 49856, 49991, 50126, 50262, 50399,
    50535, 50672, 50810, 50947, 51085, 51224, 51363, 51502, 51642, 51782, 51922, 52063, 52204, 52346, 52488, 52630,
    52773, 52916, 53059, 53203, 53347, 53492, 53637, 53782, 53928, 54074, 54221, 54368, 54515, 54663, 54811, 54960,
    55109, 55258, 55408, 55558, 55709, 55860, 56012, 56163, 56316, 56468, 56622, 56775, 56929, 57083, 57238, 57393,
    57549, 57705, 57861, 58018, 58176, 58333, 58491, 58650, 58809, 58968, 59128, 59289, 59449, 59611, 59772, 59934,
    60097, 60260, 60423, 60587, 60751, 60916, 61081, 61247, 61413, 61579, 61746, 61914, 62081, 62250, 62419, 62588,
    62757, 62928, 63098, 63269, 63441, 63613, 63785, 63958, 64132, 64306, 64480, 64655, 64830, 65006, 65182, 65359,
    65536,
};

/* Elliptic polyphase halfband decimators (Valenzuela-Constantinides): two branches of first-order
 * allpass sections in the decimated rate, alternating coefficients, output the mean of the branches.
 * The pair nulls the stage's Nyquist frequency exactly at any coefficient rounding. Coefficients are
 * Q16, designed for a 20 kHz passband at each stage's input rate: 96 kHz (six sections, -87 dB),
 * 192 kHz (three, -95 dB) and 384 kHz or more (two, -100 dB). */
static const uint16_t allpass96[6] = { 2896, 10760, 21676, 33757, 46010, 58647 };
static const uint16_t allpass192[3] = { 4180, 17484, 43814 };
static const uint16_t allpass384[2] = { 7229, 35178 };
static const uint16_t allpass768[2] = { 6994, 34738 };

/* a * x / 2^16 for x < 2^16, |a| < 2^31: exact floor without a 64-bit product. */
HOT int32_t mul16(int32_t a, uint32_t x)
{
    return (a >> 16) * (int32_t)x + (int32_t)((((uint32_t)a & 0xffff) * x) >> 16);
}

/* floor(a * x / 2^32), a signed, x unsigned. */
HOT int32_t mulhi(int32_t a, uint32_t x)
{
    int32_t ah = a >> 16, xh = (int32_t)(x >> 16), xl = (int32_t)(x & 0xffff);
    uint32_t al = (uint32_t)a & 0xffff;
    int32_t hl = ah * xl, hh = ah * xh;
    uint32_t lh = al * (uint32_t)xh, ll = al * (uint32_t)xl;
    return hh + (hl >> 16) + (int32_t)(lh >> 16)
         + (int32_t)((((uint32_t)hl & 0xffff) + (lh & 0xffff) + (ll >> 16)) >> 16);
}

/* a * b for 32-bit unsigned operands, as 64 bits from 16-bit partial products. */
static uint64_t mul64(uint32_t a, uint32_t b)
{
    uint32_t ah = a >> 16, al = a & 0xffff, bh = b >> 16, bl = b & 0xffff;
    return ((uint64_t)(ah * bh) << 32) + ((uint64_t)(ah * bl) << 16) + ((uint64_t)(al * bh) << 16) + al * bl;
}

/* round(v * level / 65535). */
static int32_t scaled(int32_t v, uint32_t level)
{
    int32_t t = mul16(v, level);
    return t + (t >> 16);
}

static uint64_t increment(uint32_t hz)
{
    /* hz * 2^32 / RATE via a 2^47 / RATE reciprocal, kept to 48 bits. */
    return (mul64(hz, (uint32_t)(((UINT64_C(1) << 47) + RATE / 2) / RATE)) >> 15) << 16;
}

#define HZ_MIN 4096u
#define HZ_MAX (20000u * 65536)

static uint32_t bounded(uint32_t hz)
{
    return hz < HZ_MIN ? HZ_MIN : hz > HZ_MAX ? HZ_MAX : hz;
}

/* 2^octaves (Q16) as a Q15 fraction in [1, 2] and whole octaves. */
typedef struct modeldigi_osc_factor octave_factor;   /* t = 0: unmodulated */

/* Bound each pass by the bank's 64 internal workspace samples, also at 16x refinement. */
#define CHUNK (MODELDIGI_OVERSAMPLE == 16 ? 4 : 8)

HOT octave_factor factor(int32_t octaves)
{
    uint32_t frac = (uint32_t)octaves & 0xffff, i = frac >> 8;
    octave_factor f = { exp2_q15[i] + (((exp2_q15[i + 1] - exp2_q15[i]) * (frac & 255) + 128) >> 8), octaves >> 16 };
    return f;
}

/* An increment times a factor, clamped to the 1/16-20000 Hz increments. Full 48-bit precision:
 * 32-bit modulated increments shift oscillator 3's self-modulated rate with the internal rate
 * enough to move slow-modulation spans by half a cent. */
HOT uint64_t modulate(uint64_t inc, octave_factor f, uint64_t lo, uint64_t hi)
{
    uint32_t high = (uint32_t)(inc >> 32), a = high >> 16, b = high & 0xffff, c = (uint32_t)inc >> 16;
    uint64_t r = ((uint64_t)(a * f.t) << 33) + ((uint64_t)(b * f.t) << 17) + ((uint64_t)((c * f.t) >> 15) << 16);
    int32_t whole = f.whole;
    for (; whole > 0 && r < hi; --whole)
        r <<= 1;
    for (; whole < 0 && r > lo; ++whole)
        r = (r >> 1) & ~UINT64_C(0xffff);
    return r < lo ? lo : r > hi ? hi : r;
}

/* Fraction (Q16) of a sub-sample since an event `distance` before the end of a step `span`, both in
 * 2^-32 cycles. Rounded throughout: a biased event time leaves a DC offset of the step's sign. */
static uint32_t since(uint32_t span, uint32_t distance)
{
    uint32_t part = span - distance;
    /* Rounded halving is one ceiling shift, including the extra shift at a power boundary. */
    if (span >= 65536) {
        unsigned shift = 16u - (unsigned)__builtin_clz(span);
        if (span > (65535u << shift)) ++shift;
        uint32_t round = (1u << shift) - 1;
        span = (span + round) >> shift;
        part = (part + round) >> shift;
    }
    return span ? ((part << 16) + span / 2) / span : 0;
}

/* Two-sample polynomial step and slope residuals around an event in the step from `old`; the
 * previous sample is still held back. Q26 audio; slope_change is per cycle. Called only at events,
 * so it stays out of line and returns its two corrections in r. */
static void __attribute__((noinline)) correct(int32_t r[2], uint32_t span, uint32_t distance,
                                              int32_t jump, int32_t slope_change)
{
    uint32_t t = since(span, distance);
    uint32_t a = t > 65535 ? 65535 : t, b = 65536 - a;
    uint32_t aa = (a * a) >> 16, bb = b > 65535 ? 65535 : (b * b) >> 16;
    r[0] += mul16(jump, aa) >> 1;
    r[1] -= mul16(jump, bb) >> 1;
    if (slope_change) {
        int32_t change = mulhi(slope_change, span);
        r[0] += mul16(mul16(change, (aa * a) >> 16), 10923);
        r[1] += mul16(mul16(change, (bb * b) >> 16), 10923);
    }
}

/* Per-render description of one voice: its level-scaled lines, steps and pulse levels; phases
 * of events are in 2^-32 cycles, the resolution of event detection and pulse levels. */
typedef struct {
    unsigned shape;
    int silent;
    int32_t a[2], b[2], reset, knee, high, low;
    uint32_t rise, fall;
} voice_lines;

static void describe(voice_lines *d, unsigned shape, uint32_t level)
{
    d->shape = shape;
    d->silent = !level;
    if (shape < 4) {
        for (int k = 0; k < 2; ++k) {
            d->a[k] = scaled(line_a[shape][k], level);
            d->b[k] = scaled(line_b[shape][k], level);
        }
        d->reset = scaled(reset_step[shape], level);
        d->knee = scaled(knee_slope[shape], level);
    } else {
        uint64_t w = widths[shape - 4];
        int32_t low = -(int32_t)(w >> 21);
        d->high = scaled(2 * Q + low, level);
        d->low = scaled(low, level);
        d->fall = (uint32_t)(w >> 17);
        d->rise = 0u - d->fall;
    }
}

/* a + b * x / 2^32 to within a unit: a 16-bit phase is too coarse for LO ramps, whose steps the
 * halfband filters would ripple back across the mean. */
HOT int32_t line(int32_t a, int32_t b, uint32_t x)
{
    return a + mul16(b, x >> 16) + (((b >> 16) * (int32_t)(x & 0xffff)) >> 16);
}

/* Value at a phase x in 2^-32 cycles. */
HOT int32_t value(const voice_lines *d, uint32_t x)
{
    if (d->shape >= 4)
        return x < d->fall || x >= d->rise ? d->high : d->low;
    return line(d->a[x >> 31], d->b[x >> 31], x);
}

/* An event at phase `at` falls in the step from `old` of `span` (2^-32 cycles). */
HOT int crossed(uint32_t at, uint32_t old, uint32_t span)
{
    return at - old - 1 < span;
}

/* One voice's running values, kept in registers across a chunk. */
typedef struct {
    uint64_t phase, last;
    int32_t previous, held;
} voice_run;

/* Advance one sub-sample by inc, the previous sub-sample's increment; returns the held-back sample. */
HOT int32_t steady(voice_run *r, const voice_lines *d, uint64_t inc)
{
    uint64_t phase = r->phase + inc;
    uint32_t old = (uint32_t)(r->phase >> 32), x = (uint32_t)(phase >> 32), span = (uint32_t)(inc >> 32);
    int32_t pure = value(d, x), fix[2] = { 0, 0 };
    if (d->shape < 4) {
        if (crossed(0, old, span))
            correct(fix, span, 0u - old, d->reset, -d->knee);
        if (d->knee && crossed(0x80000000u, old, span))
            correct(fix, span, 0x80000000u - old, 0, d->knee);
    } else {
        if (crossed(d->fall, old, span))
            correct(fix, span, d->fall - old, d->low - d->high, 0);
        if (crossed(d->rise, old, span))
            correct(fix, span, d->rise - old, d->high - d->low, 0);
    }
    int32_t output = r->previous + fix[0];
    r->previous = pure + fix[1];
    r->held = pure;
    r->phase = phase;
    r->last = inc;
    return output;
}

/* Advance one sub-sample by inc; returns the held-back sample. */
HOT int32_t advance(voice_run *r, const voice_lines *d, uint64_t inc)
{
    if (inc != r->last && d->shape < 4) {
        /* A rate change at this boundary: a sixth of the slope-rate change, held back. Below 2^-20
         * cycles per sub-sample (smooth modulation) the correction is under -120 dB and skipped. */
        int32_t delta = (int32_t)((int64_t)(inc - r->last) >> 32);
        if (delta >= 4096 || delta <= -4096) {
            int32_t b = d->b[(uint32_t)(r->phase >> 32) >> 31];
            int32_t change = delta >= 0 ? mulhi(b, (uint32_t)delta) : -mulhi(b, (uint32_t)-delta);
            r->previous += mul16(change, 10923);
        }
    }
    return steady(r, d, inc);
}

/* At a render's first sub-sample: prime a fresh voice, or hold back half of a waveform or level
 * step and a sixth of the slope-rate change, the residual of an event at the boundary itself. */
static void begin(struct modeldigi_osc_voice *o, voice_run *r, const voice_lines *d, uint64_t inc, int fresh)
{
    r->phase = o->phase;
    r->last = o->increment;
    r->previous = o->previous;
    r->held = o->held;
    uint32_t old = (uint32_t)(o->phase >> 32);
    if (!o->primed) {
        r->previous = r->held = value(d, old);
        r->last = inc;
        o->primed = true;
    } else if (fresh) {
        unsigned k = old >> 31;
        int32_t change = mulhi(d->shape < 4 ? d->b[k] : 0, (uint32_t)(inc >> 32))
                       - mulhi(o->slope[k], (uint32_t)(o->increment >> 32));
        r->previous += ((value(d, old) - r->held) >> 1) + mul16(change, 10923);
        r->last = inc;
    }
    o->waveform = (uint8_t)d->shape;
    o->slope[0] = d->shape < 4 ? d->b[0] : 0;
    o->slope[1] = d->shape < 4 ? d->b[1] : 0;
}

static void finish(struct modeldigi_osc_voice *o, const voice_run *r)
{
    o->phase = r->phase;
    o->increment = r->last;
    o->previous = r->previous;
    o->held = r->held;
}

/* b * x exactly, b signed: as unsigned, a negative b is b + 2^32. */
static int64_t product(int32_t b, uint32_t x)
{
    return (int64_t)(mul64((uint32_t)b, x) - (b < 0 ? (uint64_t)x << 32 : 0));
}

/* b * v for a Q0.64 v, as 96 bits: the low word and the signed upper 64 bits. */
typedef struct {
    uint32_t low;
    int64_t high;
} wide;

static wide times_wide(int32_t b, uint64_t v)
{
    int64_t low = product(b, (uint32_t)v);
    wide w = { (uint32_t)low, product(b, (uint32_t)(v >> 32)) + (low >> 32) };
    return w;
}

/* An unmodulated line between events: its value a + floor(b * phase / 2^64) is carried as a 96-bit
 * a 2^64 + b phase, which each sub-sample adds b times the constant increment to, exactly. */
typedef struct {
    uint64_t phase, inc;
    wide sum, step;
    int32_t previous, held;
} ramp;

/* Take up the line under q's phase. */
static void anchor(ramp *q, const voice_lines *d)
{
    unsigned k = (unsigned)(q->phase >> 63);
    q->sum = times_wide(d->b[k], q->phase);
    q->sum.high += (int64_t)d->a[k] << 32;
    q->step = times_wide(d->b[k], q->inc);
}

/* Store (first) or add up to count held-back samples of the ramp into mix, as steady would, stopping
 * before a sub-sample that enters the other half of the cycle; returns how many. ColdFire builds run
 * its assembly instead. */
static unsigned __attribute__((unused)) ramp_run(ramp *q, int32_t *mix, unsigned count, int first)
{
    unsigned n = 0;
    for (; n < count; ++n) {
        uint64_t phase = q->phase + q->inc;
        if (((uint32_t)(phase >> 32) ^ (uint32_t)(q->phase >> 32)) >> 31)
            break;
        q->phase = phase;
        uint32_t low = q->sum.low + q->step.low;
        q->sum.high += q->step.high + (low < q->step.low);
        q->sum.low = low;
        int32_t out = q->previous;
        q->previous = q->held = (int32_t)(q->sum.high >> 32);
        mix[n] = first ? out : mix[n] + out;
    }
    return n;
}

/* ColdFire builds run a ramp's sub-samples between events in hand-written code, ramp_run's exact
 * equivalent with every running value in a register. */
#if defined(__mcoldfire__) && !defined(MODELDIGI_NO_ASM)
/* The loop reads and writes a ramp by these offsets. */
typedef char ramp_layout[offsetof(ramp, inc) == 8 && offsetof(ramp, sum) == 16 && offsetof(ramp, step) == 28 &&
                         offsetof(wide, high) == 4 && offsetof(ramp, previous) == 40 &&
                         offsetof(ramp, held) == 44 ? 1 : -1];
unsigned modeldigi_ramp_run(ramp *q, int32_t *mix, unsigned count, int first);
__asm__(
    "	.pushsection .text\n"
    "	.even\n"
    "	.type	modeldigi_ramp_run, @function\n"
    "modeldigi_ramp_run:\n"
    "	lea	-40(%sp),%sp\n"
    "	movem.l	%d2-%d7/%a2-%a5,(%sp)\n"
    "	move.l	44(%sp),%a0\n"          /* r */
    "	move.l	48(%sp),%a3\n"          /* mix */
    "	move.l	52(%sp),%d0\n"
    "	lsl.l	#2,%d0\n"
    "	lea	(%a3,%d0.l),%a5\n"      /* its end */
    "	move.l	(%a0),%d6\n"            /* phase */
    "	move.l	4(%a0),%d5\n"
    "	move.l	8(%a0),%d7\n"           /* increment */
    "	move.l	12(%a0),%a1\n"
    "	move.l	16(%a0),%d0\n"          /* sum: low, then high */
    "	move.l	20(%a0),%d2\n"
    "	move.l	24(%a0),%d1\n"
    "	move.l	28(%a0),%a2\n"          /* step: low, then high */
    "	move.l	32(%a0),%d4\n"
    "	move.l	36(%a0),%d3\n"
    "	move.l	40(%a0),%a4\n"          /* previous */
    "	tst.l	56(%sp)\n"
    "	jeq	2f\n"
    "1:	add.l	%a1,%d5\n"
    "	addx.l	%d7,%d6\n"
    "	jvs	3f\n"
    "	jcs	3f\n"
    "	add.l	%a2,%d0\n"
    "	addx.l	%d3,%d1\n"
    "	addx.l	%d4,%d2\n"
    "	move.l	%a4,(%a3)+\n"
    "	move.l	%d2,%a4\n"
    "	cmpa.l	%a5,%a3\n"
    "	jne	1b\n"
    "	jra	4f\n"
    "2:	add.l	%a1,%d5\n"
    "	addx.l	%d7,%d6\n"
    "	jvs	3f\n"
    "	jcs	3f\n"
    "	add.l	%a2,%d0\n"
    "	addx.l	%d3,%d1\n"
    "	addx.l	%d4,%d2\n"
    "	adda.l	(%a3),%a4\n"
    "	move.l	%a4,(%a3)+\n"
    "	move.l	%d2,%a4\n"
    "	cmpa.l	%a5,%a3\n"
    "	jne	2b\n"
    "	jra	4f\n"
    "3:	sub.l	%a1,%d5\n"              /* undo the event's step: steady takes it */
    "	subx.l	%d7,%d6\n"
    "4:	move.l	%d6,(%a0)\n"
    "	move.l	%d5,4(%a0)\n"
    "	move.l	%d0,16(%a0)\n"
    "	move.l	%d2,20(%a0)\n"
    "	move.l	%d1,24(%a0)\n"
    "	move.l	%a4,40(%a0)\n"
    "	move.l	%a3,%d0\n"
    "	sub.l	48(%sp),%d0\n"
    "	jeq	5f\n"
    "	move.l	%a4,44(%a0)\n"          /* held: the last value */
    "	lsr.l	#2,%d0\n"
    "5:	movem.l	(%sp),%d2-%d7/%a2-%a5\n"
    "	lea	40(%sp),%sp\n"
    "	rts\n"
    "	.size	modeldigi_ramp_run, .-modeldigi_ramp_run\n"
    "	.popsection\n");
#define RAMP_RUN modeldigi_ramp_run
#else
#define RAMP_RUN ramp_run
#endif

/* steps * count, for a silent clock. */
static uint64_t times(uint64_t step, uint32_t count)
{
    return ((uint64_t)((uint32_t)(step >> 32) * count) << 32) + mul64((uint32_t)step, count);
}

HOT int changed(octave_factor a, octave_factor b)
{
    return a.t != b.t || a.whole != b.whole;
}

/* Unmodulated sub-samples into mix: only the first can change rate. Out of line, so the modulated
 * loop keeps its registers. */
static void __attribute__((noinline)) run_plain(voice_run *run, const voice_lines *d, uint64_t inc,
                                                int32_t *mix, unsigned m, int first)
{
    voice_run r = *run;   /* a local copy: mix stores cannot alias it */
    int32_t out = advance(&r, d, inc);
    mix[0] = first ? out : mix[0] + out;
    if (d->shape < 4) {
        /* Ramps between events, each event through steady; then the sum takes up the line again,
         * and steady's value, within a unit of the line, becomes the exact one before it is out. */
        ramp q = { r.phase, inc, { 0, 0 }, { 0, 0 }, r.previous, r.held };
        for (unsigned i = 1;; ++i) {
            anchor(&q, d);
            q.previous += (int32_t)(q.sum.high >> 32) - q.held;
            q.held = (int32_t)(q.sum.high >> 32);
            if (i < m)
                i += RAMP_RUN(&q, mix + i, m - i, first);
            if (i == m)
                break;
            r.phase = q.phase;
            r.previous = q.previous;
            r.held = q.held;
            out = steady(&r, d, inc);
            mix[i] = first ? out : mix[i] + out;
            q.phase = r.phase;
            q.previous = r.previous;
            q.held = r.held;
        }
        r.phase = q.phase;
        r.previous = q.previous;
        r.held = q.held;
        *run = r;
        return;
    }
    /* The empty asm hides the phase's closed form, which would need a 64-bit multiply helper. */
    if (first)
        for (unsigned i = 1; i < m; ++i) {
            __asm__("" : "+r"(r.phase));
            mix[i] = steady(&r, d, inc);
        }
    else
        for (unsigned i = 1; i < m; ++i) {
            __asm__("" : "+r"(r.phase));
            mix[i] += steady(&r, d, inc);
        }
    *run = r;
}

/* ColdFire builds run a modulated voice's sub-samples in hand-written code, the C loop in run_voice
 * exactly: at each factor change modulate's increment and advance's rate-change residual, then
 * steady's step with every running value in a register. A sub-sample that crosses a reset, knee or
 * pulse edge goes through steady itself. */
#if defined(__mcoldfire__) && !defined(MODELDIGI_NO_ASM)
#define VOICE_ASM 1
typedef struct {
    uint64_t step, lo, hi, inc;   /* the voice's unmodulated increment, bounds, the first increment */
    voice_run *r;
    const voice_lines *d;
    const octave_factor *mod;
    int32_t *mix;
    uint32_t m;
    uint32_t scratch[7];
} voice_job;
/* The loop reads these structures by these offsets. */
typedef char voice_job_layout[offsetof(voice_job, r) == 32 && offsetof(voice_job, m) == 48 &&
                              offsetof(voice_job, scratch) == 52 && offsetof(voice_run, last) == 8 &&
                              offsetof(voice_run, previous) == 16 && offsetof(voice_run, held) == 20 &&
                              offsetof(voice_lines, a) == 8 && offsetof(voice_lines, b) == 16 &&
                              offsetof(voice_lines, knee) == 28 && offsetof(voice_lines, high) == 32 &&
                              offsetof(voice_lines, low) == 36 && offsetof(voice_lines, rise) == 40 &&
                              offsetof(voice_lines, fall) == 44 && sizeof(octave_factor) == 8 ? 1 : -1];

/* An event sub-sample for the loop: steady's own step. */
int32_t __attribute__((noinline)) modeldigi_event_step(voice_run *r, const voice_lines *d, uint32_t high,
                                                       uint32_t low)
{
    return steady(r, d, (uint64_t)high << 32 | low);
}

void modeldigi_voice_run(voice_job *j);
/* Registers: d2/d3 phase, d4/a1 increment, d6 held-back sample, a0 lines, a2 factor, a3/a4 mix and
 * its end, a5 job, a6 run. Scratch: 52/56 phase, 60 the mix after the last event, 64/68 the new
 * increment, 72 delta, 76 the old increment's high word. */
__asm__(
    "	.pushsection .text\n"
    "	.even\n"
    "	.type	modeldigi_voice_run, @function\n"
    "modeldigi_voice_run:\n"
    "	lea	-44(%sp),%sp\n"
    "	movem.l	%d2-%d7/%a2-%a6,(%sp)\n"
    "	move.l	48(%sp),%a5\n"
    "	move.l	32(%a5),%a6\n"
    "	move.l	36(%a5),%a0\n"
    "	move.l	40(%a5),%a2\n"
    "	move.l	44(%a5),%a3\n"
    "	move.l	48(%a5),%d0\n"
    "	lsl.l	#2,%d0\n"
    "	lea	(%a3,%d0.l),%a4\n"
    "	clr.l	60(%a5)\n"
    "	move.l	(%a6),%d2\n"
    "	move.l	4(%a6),%d3\n"
    "	move.l	8(%a6),%d4\n"
    "	move.l	12(%a6),%a1\n"
    "	move.l	16(%a6),%d6\n"
    "	move.l	24(%a5),%d0\n"            /* the first sub-sample's increment is given */
    "	move.l	28(%a5),%d1\n"
    "	jra	.Lv_given\n"
    /* A factor change: the increment for mod[i]. */
    ".Lv_change:\n"
    "	tst.l	(%a2)\n"
    "	jne	.Lv_modulate\n"
    "	move.l	(%a5),%d0\n"
    "	move.l	4(%a5),%d1\n"
    "	jra	.Lv_given\n"
    ".Lv_modulate:\n"
    "	move.l	%d2,52(%a5)\n"
    "	move.l	%d3,56(%a5)\n"
    "	move.l	%d4,76(%a5)\n"
    "	move.l	(%a5),%d2\n"
    "	move.l	(%a2),%d4\n"
    "	move.l	%d2,%d0\n"
    "	clr.w	%d0\n"
    "	swap	%d0\n"
    "	mulu.l	%d4,%d0\n"
    "	add.l	%d0,%d0\n"
    "	mvz.w	%d2,%d1\n"
    "	mulu.l	%d4,%d1\n"
    "	move.l	%d1,%d3\n"
    "	moveq	#15,%d5\n"
    "	lsr.l	%d5,%d3\n"
    "	add.l	%d3,%d0\n"
    "	moveq	#17,%d5\n"
    "	lsl.l	%d5,%d1\n"
    "	move.l	4(%a5),%d3\n"
    "	clr.w	%d3\n"
    "	swap	%d3\n"
    "	mulu.l	%d4,%d3\n"
    "	moveq	#15,%d5\n"
    "	lsr.l	%d5,%d3\n"
    "	move.l	%d3,%d5\n"
    "	clr.w	%d5\n"
    "	swap	%d5\n"
    "	add.l	%d5,%d0\n"
    "	swap	%d3\n"
    "	clr.w	%d3\n"
    "	add.l	%d3,%d1\n"
    "	moveq	#0,%d5\n"
    "	addx.l	%d5,%d0\n"
    "	move.l	4(%a2),%d2\n"              /* whole octaves */
    "	jle	.Lv_down\n"
    ".Lv_up:\n"
    "	cmp.l	16(%a5),%d0\n"
    "	jcs	.Lv_shl\n"
    "	jne	.Lv_clamp\n"
    "	cmp.l	20(%a5),%d1\n"
    "	jcc	.Lv_clamp\n"
    ".Lv_shl:\n"
    "	add.l	%d1,%d1\n"
    "	addx.l	%d0,%d0\n"
    "	subq.l	#1,%d2\n"
    "	jgt	.Lv_up\n"
    "	jra	.Lv_clamp\n"
    ".Lv_down:\n"
    "	jeq	.Lv_clamp\n"
    ".Lv_downloop:\n"
    "	cmp.l	8(%a5),%d0\n"
    "	jhi	.Lv_shr\n"
    "	jcs	.Lv_clamp\n"
    "	cmp.l	12(%a5),%d1\n"
    "	jls	.Lv_clamp\n"
    ".Lv_shr:\n"
    "	lsr.l	#1,%d1\n"
    "	move.l	%d0,%d3\n"
    "	moveq	#31,%d5\n"
    "	lsl.l	%d5,%d3\n"
    "	or.l	%d3,%d1\n"
    "	and.l	#0xffff0000,%d1\n"
    "	lsr.l	#1,%d0\n"
    "	addq.l	#1,%d2\n"
    "	jlt	.Lv_downloop\n"
    ".Lv_clamp:\n"
    "	cmp.l	8(%a5),%d0\n"
    "	jhi	.Lv_nolo\n"
    "	jcs	.Lv_lo\n"
    "	cmp.l	12(%a5),%d1\n"
    "	jcc	.Lv_nolo\n"
    ".Lv_lo:\n"
    "	move.l	8(%a5),%d0\n"
    "	move.l	12(%a5),%d1\n"
    "	jra	.Lv_bounded\n"
    ".Lv_nolo:\n"
    "	cmp.l	16(%a5),%d0\n"
    "	jcs	.Lv_bounded\n"
    "	jhi	.Lv_hi\n"
    "	cmp.l	20(%a5),%d1\n"
    "	jls	.Lv_bounded\n"
    ".Lv_hi:\n"
    "	move.l	16(%a5),%d0\n"
    "	move.l	20(%a5),%d1\n"
    ".Lv_bounded:\n"
    "	move.l	52(%a5),%d2\n"
    "	move.l	56(%a5),%d3\n"
    "	move.l	76(%a5),%d4\n"
    /* New increment d0:d1, old d4:a1: a line's residual of the rate change, as advance. */
    ".Lv_given:\n"
    "	moveq	#4,%d5\n"
    "	cmp.l	(%a0),%d5\n"
    "	jls	.Lv_set\n"                  /* pulses: none */
    "	move.l	%d1,%d5\n"
    "	sub.l	%a1,%d5\n"
    "	move.l	%d0,%d7\n"
    "	subx.l	%d4,%d7\n"                 /* delta: the difference's high word */
    "	move.l	%d7,%d5\n"
    "	jpl	.Lv_abs\n"
    "	neg.l	%d5\n"
    ".Lv_abs:\n"
    "	cmp.l	#4096,%d5\n"
    "	jcs	.Lv_set\n"
    "	move.l	%d0,64(%a5)\n"
    "	move.l	%d1,68(%a5)\n"
    "	move.l	%d7,72(%a5)\n"
    "	move.l	%d2,%d1\n"                 /* b of the line under the phase */
    "	add.l	%d1,%d1\n"
    "	subx.l	%d1,%d1\n"
    "	and.l	#4,%d1\n"
    "	move.l	16(%a0,%d1.l),%d1\n"
    "	move.l	%d1,%d0\n"                 /* mulhi(b, |delta|) */
    "	swap	%d0\n"
    "	ext.l	%d0\n"
    "	mvz.w	%d1,%d1\n"
    "	move.l	%d5,%d4\n"
    "	clr.w	%d4\n"
    "	swap	%d4\n"
    "	mvz.w	%d5,%d5\n"
    "	move.l	%d0,%d7\n"
    "	muls.l	%d4,%d7\n"
    "	muls.l	%d5,%d0\n"
    "	mulu.l	%d1,%d4\n"
    "	mulu.l	%d1,%d5\n"
    "	move.l	%d0,%d1\n"
    "	swap	%d1\n"
    "	ext.l	%d1\n"
    "	add.l	%d1,%d7\n"
    "	move.l	%d4,%d1\n"
    "	clr.w	%d1\n"
    "	swap	%d1\n"
    "	add.l	%d1,%d7\n"
    "	mvz.w	%d0,%d0\n"
    "	mvz.w	%d4,%d4\n"
    "	add.l	%d4,%d0\n"
    "	clr.w	%d5\n"
    "	swap	%d5\n"
    "	add.l	%d5,%d0\n"
    "	clr.w	%d0\n"
    "	swap	%d0\n"
    "	add.l	%d0,%d7\n"
    "	tst.l	72(%a5)\n"
    "	jpl	.Lv_pos\n"
    "	neg.l	%d7\n"
    ".Lv_pos:\n"
    "	move.l	%d7,%d0\n"                 /* previous += mul16(change, 10923) */
    "	swap	%d0\n"
    "	ext.l	%d0\n"
    "	move.l	#10923,%d1\n"
    "	muls.l	%d1,%d0\n"
    "	mvz.w	%d7,%d7\n"
    "	mulu.l	%d1,%d7\n"
    "	clr.w	%d7\n"
    "	swap	%d7\n"
    "	add.l	%d7,%d0\n"
    "	add.l	%d0,%d6\n"
    "	move.l	64(%a5),%d0\n"
    "	move.l	68(%a5),%d1\n"
    ".Lv_set:\n"
    "	move.l	%d0,%d4\n"
    "	move.l	%d1,%a1\n"
    "	moveq	#4,%d5\n"
    "	cmp.l	(%a0),%d5\n"
    "	jls	.Lv_pulse\n"
    /* A line's sub-sample. */
    ".Lv_line:\n"
    "	move.l	%d2,%d7\n"
    "	add.l	%a1,%d3\n"
    "	addx.l	%d4,%d2\n"
    "	move.l	%d7,%d0\n"
    "	not.l	%d0\n"
    "	cmp.l	%d4,%d0\n"
    "	jcs	.Lv_event\n"               /* the reset */
    "	tst.l	28(%a0)\n"
    "	jeq	.Lv_value\n"
    "	move.l	#0x7fffffff,%d0\n"
    "	sub.l	%d7,%d0\n"
    "	cmp.l	%d4,%d0\n"
    "	jcs	.Lv_event\n"               /* the knee */
    ".Lv_value:\n"
    "	move.l	%d2,%d1\n"
    "	add.l	%d1,%d1\n"
    "	subx.l	%d1,%d1\n"
    "	and.l	#4,%d1\n"
    "	move.l	16(%a0,%d1.l),%d5\n"       /* b */
    "	move.l	8(%a0,%d1.l),%d1\n"        /* a */
    "	move.l	%d5,%d0\n"
    "	swap	%d0\n"
    "	ext.l	%d0\n"
    "	move.l	%d2,%d7\n"
    "	clr.w	%d7\n"
    "	swap	%d7\n"
    "	mvz.w	%d5,%d5\n"
    "	mulu.l	%d7,%d5\n"
    "	clr.w	%d5\n"
    "	swap	%d5\n"
    "	add.l	%d5,%d1\n"
    "	muls.l	%d0,%d7\n"
    "	add.l	%d7,%d1\n"
    "	mvz.w	%d2,%d7\n"
    "	muls.l	%d0,%d7\n"
    "	swap	%d7\n"
    "	ext.l	%d7\n"
    "	add.l	%d7,%d1\n"
    "	add.l	%d6,(%a3)+\n"
    "	move.l	%d1,%d6\n"
    "	cmpa.l	%a4,%a3\n"
    "	jeq	.Lv_done\n"
    "	addq.l	#8,%a2\n"
    "	move.l	(%a2),%d0\n"
    "	cmp.l	-8(%a2),%d0\n"
    "	jne	.Lv_change\n"
    "	move.l	4(%a2),%d0\n"
    "	cmp.l	-4(%a2),%d0\n"
    "	jne	.Lv_change\n"
    "	jra	.Lv_line\n"
    /* A pulse's sub-sample. */
    ".Lv_pulse:\n"
    "	move.l	%d2,%d7\n"
    "	add.l	%a1,%d3\n"
    "	addx.l	%d4,%d2\n"
    "	move.l	44(%a0),%d0\n"
    "	sub.l	%d7,%d0\n"
    "	subq.l	#1,%d0\n"
    "	cmp.l	%d4,%d0\n"
    "	jcs	.Lv_event\n"               /* the fall */
    "	move.l	40(%a0),%d0\n"
    "	sub.l	%d7,%d0\n"
    "	subq.l	#1,%d0\n"
    "	cmp.l	%d4,%d0\n"
    "	jcs	.Lv_event\n"               /* the rise */
    "	cmp.l	44(%a0),%d2\n"
    "	jcs	.Lv_high\n"
    "	cmp.l	40(%a0),%d2\n"
    "	jcc	.Lv_high\n"
    "	move.l	36(%a0),%d1\n"
    "	jra	.Lv_held\n"
    ".Lv_high:\n"
    "	move.l	32(%a0),%d1\n"
    ".Lv_held:\n"
    "	add.l	%d6,(%a3)+\n"
    "	move.l	%d1,%d6\n"
    "	cmpa.l	%a4,%a3\n"
    "	jeq	.Lv_done\n"
    "	addq.l	#8,%a2\n"
    "	move.l	(%a2),%d0\n"
    "	cmp.l	-8(%a2),%d0\n"
    "	jne	.Lv_change\n"
    "	move.l	4(%a2),%d0\n"
    "	cmp.l	-4(%a2),%d0\n"
    "	jne	.Lv_change\n"
    "	jra	.Lv_pulse\n"
    /* An event: undo the step and let steady take the sub-sample. */
    ".Lv_event:\n"
    "	sub.l	%a1,%d3\n"
    "	subx.l	%d4,%d2\n"
    "	move.l	%d2,(%a6)\n"
    "	move.l	%d3,4(%a6)\n"
    "	move.l	%d4,8(%a6)\n"
    "	move.l	%a1,12(%a6)\n"
    "	move.l	%d6,16(%a6)\n"
    "	move.l	%a1,-(%sp)\n"
    "	move.l	%d4,-(%sp)\n"
    "	move.l	%a0,-(%sp)\n"
    "	move.l	%a6,-(%sp)\n"
    "	jsr	modeldigi_event_step\n"
    "	lea	16(%sp),%sp\n"
    "	add.l	%d0,(%a3)+\n"
    "	move.l	%a3,60(%a5)\n"
    "	move.l	(%a6),%d2\n"
    "	move.l	4(%a6),%d3\n"
    "	move.l	16(%a6),%d6\n"
    "	move.l	36(%a5),%a0\n"
    "	move.l	12(%a6),%a1\n"
    "	cmpa.l	%a4,%a3\n"
    "	jeq	.Lv_done\n"
    "	addq.l	#8,%a2\n"
    "	move.l	(%a2),%d0\n"
    "	cmp.l	-8(%a2),%d0\n"
    "	jne	.Lv_change\n"
    "	move.l	4(%a2),%d0\n"
    "	cmp.l	-4(%a2),%d0\n"
    "	jne	.Lv_change\n"
    "	moveq	#4,%d5\n"
    "	cmp.l	(%a0),%d5\n"
    "	jls	.Lv_pulse\n"
    "	jra	.Lv_line\n"
    ".Lv_done:\n"
    "	move.l	%d2,(%a6)\n"
    "	move.l	%d3,4(%a6)\n"
    "	move.l	%d4,8(%a6)\n"
    "	move.l	%a1,12(%a6)\n"
    "	move.l	%d6,16(%a6)\n"
    "	cmpa.l	60(%a5),%a3\n"
    "	jeq	.Lv_out\n"
    "	move.l	%d6,20(%a6)\n"             /* held: the last value, unless steady took it */
    ".Lv_out:\n"
    "	movem.l	(%sp),%d2-%d7/%a2-%a6\n"
    "	lea	44(%sp),%sp\n"
    "	rts\n"
    "	.size	modeldigi_voice_run, .-modeldigi_voice_run\n"
    "	.popsection\n");

/* Oscillator 3's sub-samples after the first, the C loop in modeldigi_osc_render exactly: the
 * segment under the phase, the factor at each frame start and segment change (and with CONTROL
 * oscillator 3's own increment), each sub-sample's factor for oscillators 1 and 2, and oscillator
 * 3's own steady step, stored. */
typedef struct {
    voice_job v;                  /* step, bounds, increment, run, lines, mod and mix from sub-sample 1 */
    int32_t oa[2], ob[2], on;     /* the control lines, or the pulse's octaves */
    octave_factor f;              /* the factor held */
    uint32_t segment, control, keep;   /* keep: all ones, or zero for a muted oscillator 3 */
    const uint32_t *exp2;
    uint32_t frame, left;         /* sub-samples a frame, and to the next frame start */
    uint32_t spare;
} source_job;
typedef char source_job_layout[offsetof(source_job, oa) == 80 && offsetof(source_job, ob) == 88 &&
                               offsetof(source_job, on) == 96 && offsetof(source_job, f) == 100 &&
                               offsetof(source_job, segment) == 108 && offsetof(source_job, keep) == 116 &&
                               offsetof(source_job, exp2) == 120 && offsetof(source_job, left) == 128 &&
                               offsetof(source_job, spare) == 132 ? 1 : -1];

void modeldigi_source_run(source_job *j);
__asm__(
    "	.pushsection .text\n"
    "	.even\n"
    "	.type	modeldigi_source_run, @function\n"
    "modeldigi_source_run:\n"
    "	lea	-44(%sp),%sp\n"
    "	movem.l	%d2-%d7/%a2-%a6,(%sp)\n"
    "	move.l	48(%sp),%a5\n"
    "	move.l	32(%a5),%a6\n"
    "	move.l	36(%a5),%a0\n"
    "	move.l	40(%a5),%a2\n"
    "	move.l	44(%a5),%a3\n"
    "	move.l	48(%a5),%d0\n"
    "	lsl.l	#2,%d0\n"
    "	lea	(%a3,%d0.l),%a4\n"
    "	clr.l	60(%a5)\n"
    "	move.l	(%a6),%d2\n"
    "	move.l	4(%a6),%d3\n"
    "	move.l	8(%a6),%d4\n"
    "	move.l	12(%a6),%a1\n"
    "	move.l	16(%a6),%d6\n"
    "	moveq	#4,%d5\n"
    "	cmp.l	(%a0),%d5\n"
    "	jls	.Ls_ptop\n"
    /* A line: its segment is the phase's half. */
    ".Ls_ltop:\n"
    "	subq.l	#1,128(%a5)\n"
    "	jeq	.Ls_lframe\n"
    "	move.l	%d2,%d0\n"
    "	moveq	#31,%d1\n"
    "	lsr.l	%d1,%d0\n"
    "	cmp.l	108(%a5),%d0\n"
    "	jne	.Ls_refresh\n"
    "	move.l	100(%a5),(%a2)+\n"        /* the factor held */
    "	move.l	104(%a5),(%a2)+\n"
    "	jra	.Ls_lstep\n"
    ".Ls_lframe:\n"
    "	move.l	124(%a5),128(%a5)\n"
    "	move.l	%d2,%d0\n"
    "	moveq	#31,%d1\n"
    "	lsr.l	%d1,%d0\n"
    "	jra	.Ls_refresh\n"
    /* A pulse: segment 1 while the audio is low. */
    ".Ls_ptop:\n"
    "	subq.l	#1,128(%a5)\n"
    "	jne	.Ls_pseg\n"
    "	move.l	124(%a5),128(%a5)\n"
    "	moveq	#0,%d0\n"
    "	cmp.l	44(%a0),%d2\n"
    "	jcs	.Ls_pk1\n"
    "	cmp.l	40(%a0),%d2\n"
    "	jcc	.Ls_pk1\n"
    "	moveq	#1,%d0\n"
    ".Ls_pk1:\n"
    "	jra	.Ls_refresh\n"
    ".Ls_pseg:\n"
    "	moveq	#0,%d0\n"
    "	cmp.l	44(%a0),%d2\n"
    "	jcs	.Ls_pk2\n"
    "	cmp.l	40(%a0),%d2\n"
    "	jcc	.Ls_pk2\n"
    "	moveq	#1,%d0\n"
    ".Ls_pk2:\n"
    "	cmp.l	108(%a5),%d0\n"
    "	jne	.Ls_refresh\n"
    "	move.l	100(%a5),(%a2)+\n"
    "	move.l	104(%a5),(%a2)+\n"
    "	jra	.Ls_pstep\n"
    /* A frame start or a segment change: the factor, from the segment k in d0. */
    ".Ls_refresh:\n"
    "	move.l	%d0,108(%a5)\n"
    "	moveq	#4,%d5\n"
    "	cmp.l	(%a0),%d5\n"
    "	jls	.Ls_pulseoct\n"
    "	lsl.l	#2,%d0\n"                 /* line(oa[k], ob[k], x) */
    "	move.l	88(%a5,%d0.l),%d5\n"
    "	move.l	80(%a5,%d0.l),%d1\n"
    "	move.l	%d5,%d0\n"
    "	swap	%d0\n"
    "	ext.l	%d0\n"
    "	move.l	%d2,%d7\n"
    "	clr.w	%d7\n"
    "	swap	%d7\n"
    "	mvz.w	%d5,%d5\n"
    "	mulu.l	%d7,%d5\n"
    "	clr.w	%d5\n"
    "	swap	%d5\n"
    "	add.l	%d5,%d1\n"
    "	muls.l	%d0,%d7\n"
    "	add.l	%d7,%d1\n"
    "	mvz.w	%d2,%d7\n"
    "	muls.l	%d0,%d7\n"
    "	swap	%d7\n"
    "	ext.l	%d7\n"
    "	add.l	%d7,%d1\n"
    "	jra	.Ls_octaves\n"
    ".Ls_pulseoct:\n"
    "	moveq	#0,%d1\n"
    "	tst.l	%d0\n"
    "	jeq	.Ls_octaves\n"
    "	move.l	96(%a5),%d1\n"
    ".Ls_octaves:\n"                    /* factor(octaves), t = 0 when none */
    "	move.l	%d1,%d0\n"
    "	swap	%d0\n"
    "	ext.l	%d0\n"
    "	move.l	%d0,104(%a5)\n"
    "	tst.l	%d1\n"
    "	jne	.Ls_exp\n"
    "	clr.l	100(%a5)\n"
    "	jra	.Ls_factored\n"
    ".Ls_exp:\n"
    "	mvz.w	%d1,%d0\n"
    "	move.l	%d0,%d5\n"
    "	lsr.l	#8,%d5\n"
    "	lsl.l	#2,%d5\n"
    "	move.l	%a2,132(%a5)\n"
    "	move.l	120(%a5),%a2\n"
    "	move.l	(%a2,%d5.l),%d7\n"
    "	move.l	4(%a2,%d5.l),%d5\n"
    "	move.l	132(%a5),%a2\n"
    "	sub.l	%d7,%d5\n"
    "	and.l	#255,%d0\n"
    "	mulu.l	%d0,%d5\n"
    "	add.l	#128,%d5\n"
    "	lsr.l	#8,%d5\n"
    "	add.l	%d7,%d5\n"
    "	move.l	%d5,100(%a5)\n"
    ".Ls_factored:\n"
    "	move.l	100(%a5),(%a2)+\n"
    "	move.l	104(%a5),(%a2)+\n"
    "	tst.l	112(%a5)\n"               /* CONTROL: oscillator 3's own increment */
    "	jeq	.Ls_keepinc\n"
    "	tst.l	%d1\n"
    "	jne	.Ls_modulate\n"
    "	move.l	(%a5),%d0\n"
    "	move.l	4(%a5),%d1\n"
    "	jra	.Ls_given\n"
    ".Ls_keepinc:\n"
    "	move.l	%d4,%d0\n"
    "	move.l	%a1,%d1\n"
    "	jra	.Ls_given\n"
    ".Ls_modulate:\n"
    "	move.l	%d2,52(%a5)\n"
    "	move.l	%d3,56(%a5)\n"
    "	move.l	%d4,76(%a5)\n"
    "	move.l	(%a5),%d2\n"
    "	move.l	100(%a5),%d4\n"
    "	move.l	%d2,%d0\n"
    "	clr.w	%d0\n"
    "	swap	%d0\n"
    "	mulu.l	%d4,%d0\n"
    "	add.l	%d0,%d0\n"
    "	mvz.w	%d2,%d1\n"
    "	mulu.l	%d4,%d1\n"
    "	move.l	%d1,%d3\n"
    "	moveq	#15,%d5\n"
    "	lsr.l	%d5,%d3\n"
    "	add.l	%d3,%d0\n"
    "	moveq	#17,%d5\n"
    "	lsl.l	%d5,%d1\n"
    "	move.l	4(%a5),%d3\n"
    "	clr.w	%d3\n"
    "	swap	%d3\n"
    "	mulu.l	%d4,%d3\n"
    "	moveq	#15,%d5\n"
    "	lsr.l	%d5,%d3\n"
    "	move.l	%d3,%d5\n"
    "	clr.w	%d5\n"
    "	swap	%d5\n"
    "	add.l	%d5,%d0\n"
    "	swap	%d3\n"
    "	clr.w	%d3\n"
    "	add.l	%d3,%d1\n"
    "	moveq	#0,%d5\n"
    "	addx.l	%d5,%d0\n"
    "	move.l	104(%a5),%d2\n"              /* whole octaves */
    "	jle	.Ls_down\n"
    ".Ls_up:\n"
    "	cmp.l	16(%a5),%d0\n"
    "	jcs	.Ls_shl\n"
    "	jne	.Ls_clamp\n"
    "	cmp.l	20(%a5),%d1\n"
    "	jcc	.Ls_clamp\n"
    ".Ls_shl:\n"
    "	add.l	%d1,%d1\n"
    "	addx.l	%d0,%d0\n"
    "	subq.l	#1,%d2\n"
    "	jgt	.Ls_up\n"
    "	jra	.Ls_clamp\n"
    ".Ls_down:\n"
    "	jeq	.Ls_clamp\n"
    ".Ls_downloop:\n"
    "	cmp.l	8(%a5),%d0\n"
    "	jhi	.Ls_shr\n"
    "	jcs	.Ls_clamp\n"
    "	cmp.l	12(%a5),%d1\n"
    "	jls	.Ls_clamp\n"
    ".Ls_shr:\n"
    "	lsr.l	#1,%d1\n"
    "	move.l	%d0,%d3\n"
    "	moveq	#31,%d5\n"
    "	lsl.l	%d5,%d3\n"
    "	or.l	%d3,%d1\n"
    "	and.l	#0xffff0000,%d1\n"
    "	lsr.l	#1,%d0\n"
    "	addq.l	#1,%d2\n"
    "	jlt	.Ls_downloop\n"
    ".Ls_clamp:\n"
    "	cmp.l	8(%a5),%d0\n"
    "	jhi	.Ls_nolo\n"
    "	jcs	.Ls_lo\n"
    "	cmp.l	12(%a5),%d1\n"
    "	jcc	.Ls_nolo\n"
    ".Ls_lo:\n"
    "	move.l	8(%a5),%d0\n"
    "	move.l	12(%a5),%d1\n"
    "	jra	.Ls_bounded\n"
    ".Ls_nolo:\n"
    "	cmp.l	16(%a5),%d0\n"
    "	jcs	.Ls_bounded\n"
    "	jhi	.Ls_hi\n"
    "	cmp.l	20(%a5),%d1\n"
    "	jls	.Ls_bounded\n"
    ".Ls_hi:\n"
    "	move.l	16(%a5),%d0\n"
    "	move.l	20(%a5),%d1\n"
    ".Ls_bounded:\n"
    "	move.l	52(%a5),%d2\n"
    "	move.l	56(%a5),%d3\n"
    "	move.l	76(%a5),%d4\n"
    /* New increment d0:d1, old d4:a1: a line's residual of the rate change, as advance. */
    ".Ls_given:\n"
    "	moveq	#4,%d5\n"
    "	cmp.l	(%a0),%d5\n"
    "	jls	.Ls_set\n"                  /* pulses: none */
    "	move.l	%d1,%d5\n"
    "	sub.l	%a1,%d5\n"
    "	move.l	%d0,%d7\n"
    "	subx.l	%d4,%d7\n"                 /* delta: the difference's high word */
    "	move.l	%d7,%d5\n"
    "	jpl	.Ls_abs\n"
    "	neg.l	%d5\n"
    ".Ls_abs:\n"
    "	cmp.l	#4096,%d5\n"
    "	jcs	.Ls_set\n"
    "	move.l	%d0,64(%a5)\n"
    "	move.l	%d1,68(%a5)\n"
    "	move.l	%d7,72(%a5)\n"
    "	move.l	%d2,%d1\n"                 /* b of the line under the phase */
    "	add.l	%d1,%d1\n"
    "	subx.l	%d1,%d1\n"
    "	and.l	#4,%d1\n"
    "	move.l	16(%a0,%d1.l),%d1\n"
    "	move.l	%d1,%d0\n"                 /* mulhi(b, |delta|) */
    "	swap	%d0\n"
    "	ext.l	%d0\n"
    "	mvz.w	%d1,%d1\n"
    "	move.l	%d5,%d4\n"
    "	clr.w	%d4\n"
    "	swap	%d4\n"
    "	mvz.w	%d5,%d5\n"
    "	move.l	%d0,%d7\n"
    "	muls.l	%d4,%d7\n"
    "	muls.l	%d5,%d0\n"
    "	mulu.l	%d1,%d4\n"
    "	mulu.l	%d1,%d5\n"
    "	move.l	%d0,%d1\n"
    "	swap	%d1\n"
    "	ext.l	%d1\n"
    "	add.l	%d1,%d7\n"
    "	move.l	%d4,%d1\n"
    "	clr.w	%d1\n"
    "	swap	%d1\n"
    "	add.l	%d1,%d7\n"
    "	mvz.w	%d0,%d0\n"
    "	mvz.w	%d4,%d4\n"
    "	add.l	%d4,%d0\n"
    "	clr.w	%d5\n"
    "	swap	%d5\n"
    "	add.l	%d5,%d0\n"
    "	clr.w	%d0\n"
    "	swap	%d0\n"
    "	add.l	%d0,%d7\n"
    "	tst.l	72(%a5)\n"
    "	jpl	.Ls_pos\n"
    "	neg.l	%d7\n"
    ".Ls_pos:\n"
    "	move.l	%d7,%d0\n"                 /* previous += mul16(change, 10923) */
    "	swap	%d0\n"
    "	ext.l	%d0\n"
    "	move.l	#10923,%d1\n"
    "	muls.l	%d1,%d0\n"
    "	mvz.w	%d7,%d7\n"
    "	mulu.l	%d1,%d7\n"
    "	clr.w	%d7\n"
    "	swap	%d7\n"
    "	add.l	%d7,%d0\n"
    "	add.l	%d0,%d6\n"
    "	move.l	64(%a5),%d0\n"
    "	move.l	68(%a5),%d1\n"
    ".Ls_set:\n"
    "	move.l	%d0,%d4\n"
    "	move.l	%d1,%a1\n"
    "	moveq	#4,%d5\n"
    "	cmp.l	(%a0),%d5\n"
    "	jls	.Ls_pstep\n"
    ".Ls_lstep:\n"
    "	move.l	%d2,%d7\n"
    "	add.l	%a1,%d3\n"
    "	addx.l	%d4,%d2\n"
    "	move.l	%d7,%d0\n"
    "	not.l	%d0\n"
    "	cmp.l	%d4,%d0\n"
    "	jcs	.Ls_event\n"               /* the reset */
    "	tst.l	28(%a0)\n"
    "	jeq	.Ls_value\n"
    "	move.l	#0x7fffffff,%d0\n"
    "	sub.l	%d7,%d0\n"
    "	cmp.l	%d4,%d0\n"
    "	jcs	.Ls_event\n"               /* the knee */
    ".Ls_value:\n"
    "	move.l	%d2,%d1\n"
    "	add.l	%d1,%d1\n"
    "	subx.l	%d1,%d1\n"
    "	and.l	#4,%d1\n"
    "	move.l	16(%a0,%d1.l),%d5\n"       /* b */
    "	move.l	8(%a0,%d1.l),%d1\n"        /* a */
    "	move.l	%d5,%d0\n"
    "	swap	%d0\n"
    "	ext.l	%d0\n"
    "	move.l	%d2,%d7\n"
    "	clr.w	%d7\n"
    "	swap	%d7\n"
    "	mvz.w	%d5,%d5\n"
    "	mulu.l	%d7,%d5\n"
    "	clr.w	%d5\n"
    "	swap	%d5\n"
    "	add.l	%d5,%d1\n"
    "	muls.l	%d0,%d7\n"
    "	add.l	%d7,%d1\n"
    "	mvz.w	%d2,%d7\n"
    "	muls.l	%d0,%d7\n"
    "	swap	%d7\n"
    "	ext.l	%d7\n"
    "	add.l	%d7,%d1\n"
    "	move.l	%d6,%d0\n"
    "	and.l	116(%a5),%d0\n"
    "	move.l	%d0,(%a3)+\n"
    "	move.l	%d1,%d6\n"
    "	cmpa.l	%a4,%a3\n"
    "	jne	.Ls_ltop\n"
    "	jra	.Ls_done\n"
    ".Ls_pstep:\n"
    "	move.l	%d2,%d7\n"
    "	add.l	%a1,%d3\n"
    "	addx.l	%d4,%d2\n"
    "	move.l	44(%a0),%d0\n"
    "	sub.l	%d7,%d0\n"
    "	subq.l	#1,%d0\n"
    "	cmp.l	%d4,%d0\n"
    "	jcs	.Ls_event\n"               /* the fall */
    "	move.l	40(%a0),%d0\n"
    "	sub.l	%d7,%d0\n"
    "	subq.l	#1,%d0\n"
    "	cmp.l	%d4,%d0\n"
    "	jcs	.Ls_event\n"               /* the rise */
    "	cmp.l	44(%a0),%d2\n"
    "	jcs	.Ls_high\n"
    "	cmp.l	40(%a0),%d2\n"
    "	jcc	.Ls_high\n"
    "	move.l	36(%a0),%d1\n"
    "	jra	.Ls_held\n"
    ".Ls_high:\n"
    "	move.l	32(%a0),%d1\n"
    ".Ls_held:\n"
    "	move.l	%d6,%d0\n"
    "	and.l	116(%a5),%d0\n"
    "	move.l	%d0,(%a3)+\n"
    "	move.l	%d1,%d6\n"
    "	cmpa.l	%a4,%a3\n"
    "	jne	.Ls_ptop\n"
    "	jra	.Ls_done\n"
    /* An event: undo the step and let steady take the sub-sample. */
    ".Ls_event:\n"
    "	sub.l	%a1,%d3\n"
    "	subx.l	%d4,%d2\n"
    "	move.l	%d2,(%a6)\n"
    "	move.l	%d3,4(%a6)\n"
    "	move.l	%d4,8(%a6)\n"
    "	move.l	%a1,12(%a6)\n"
    "	move.l	%d6,16(%a6)\n"
    "	move.l	%a1,-(%sp)\n"
    "	move.l	%d4,-(%sp)\n"
    "	move.l	%a0,-(%sp)\n"
    "	move.l	%a6,-(%sp)\n"
    "	jsr	modeldigi_event_step\n"
    "	lea	16(%sp),%sp\n"
    "	and.l	116(%a5),%d0\n"
    "	move.l	%d0,(%a3)+\n"
    "	move.l	%a3,60(%a5)\n"
    "	move.l	(%a6),%d2\n"
    "	move.l	4(%a6),%d3\n"
    "	move.l	16(%a6),%d6\n"
    "	move.l	36(%a5),%a0\n"
    "	move.l	12(%a6),%a1\n"
    "	cmpa.l	%a4,%a3\n"
    "	jeq	.Ls_done\n"
    "	moveq	#4,%d5\n"
    "	cmp.l	(%a0),%d5\n"
    "	jls	.Ls_ptop\n"
    "	jra	.Ls_ltop\n"
    ".Ls_done:\n"
    "	move.l	%d2,(%a6)\n"
    "	move.l	%d3,4(%a6)\n"
    "	move.l	%d4,8(%a6)\n"
    "	move.l	%a1,12(%a6)\n"
    "	move.l	%d6,16(%a6)\n"
    "	cmpa.l	60(%a5),%a3\n"
    "	jeq	.Ls_out\n"
    "	move.l	%d6,20(%a6)\n"
    ".Ls_out:\n"
    "	movem.l	(%sp),%d2-%d7/%a2-%a6\n"
    "	lea	44(%sp),%sp\n"
    "	rts\n"
    "	.size	modeldigi_source_run, .-modeldigi_source_run\n"
    "	.popsection\n");
#endif

/* Run voice v for m sub-samples into mix (added, unless first); mod holds each sub-sample's
 * modulation factor (t = 0: none), or is null. The modulated increment is recomputed only where
 * the factor changes. */
static void run_voice(struct modeldigi_osc_voice *o, const voice_lines *d, uint64_t step,
                      const octave_factor *mod, uint64_t lo, uint64_t hi,
                      int32_t *mix, unsigned m, int fresh, int first)
{
    voice_run r;
    if (d->silent && !fresh && o->primed) {
        uint64_t phase = o->phase, inc = step;
        if (mod) {
            for (unsigned i = 0; i < m; ++i) {
                if (!i || changed(mod[i], mod[i - 1]))
                    inc = mod[i].t ? modulate(step, mod[i], lo, hi) : step;
                phase += inc;
            }
        } else {
            phase += times(step, m);
        }
        o->phase = phase;
        o->increment = inc;
        o->previous = o->held = 0;
        if (first)
            for (unsigned i = 0; i < m; ++i)
                mix[i] = 0;
        return;
    }
    uint64_t inc = mod && mod[0].t ? modulate(step, mod[0], lo, hi) : step;
    begin(o, &r, d, inc, fresh);
    if (!mod) {
        run_plain(&r, d, inc, mix, m, first);
        finish(o, &r);
        return;
    }
#ifdef VOICE_ASM
    if (!first) {
        voice_job j = { step, lo, hi, inc, &r, d, mod, mix, m, { 0 } };
        modeldigi_voice_run(&j);
        finish(o, &r);
        return;
    }
#endif
    for (unsigned i = 0; i < m; ++i) {
        int32_t out;
        if (!i || changed(mod[i], mod[i - 1])) {
            if (i)
                inc = mod[i].t ? modulate(step, mod[i], lo, hi) : step;
            out = advance(&r, d, inc);
        } else {
            out = steady(&r, d, inc);   /* the increment is held with the factor */
        }
        mix[i] = first ? out : mix[i] + out;
    }
    finish(o, &r);
}

/* ColdFire builds run the allpass sections on the EMAC, which the render enters in fractional,
 * truncating, unsaturated mode; every ColdFire the engine targets has one. */
#if defined(__mcoldfire__) && !defined(MODELDIGI_NO_EMAC)
#define EMAC 1
#define EMAC_MODE 0x20u   /* MACSR: fractional (F/I), truncate, no saturation */
#else
#define EMAC 0
#endif

/* One branch: cascaded y = a (x - y') + x' sections, each s + floor(a (x - y') / 2^16); s[i] is
 * section i's previous input and s[i + 1] its previous output. The EMAC's fractional a 2^15 (x - y')
 * keeps the product's upper bits and truncates the rest, the same floor. */
HOT int32_t branch(int32_t *s, const uint16_t *a, unsigned first, unsigned count, int32_t x)
{
    unsigned k = 0;
#pragma GCC unroll 8
    for (unsigned i = first; i < count; i += 2, ++k) {
#if EMAC
        __asm__ volatile ("move.l %0,%%acc0\n\tmac.l %1,%2,%%acc0"
                          : : "r"(s[k]), "r"((int32_t)a[i] << 15), "r"(x - s[k + 1]));
        s[k] = x;   /* between the MAC and its result, which is not ready the next cycle */
        __asm__ volatile ("movclr.l %%acc0,%0" : "=d"(x));
#else
        int32_t d = x - s[k + 1];
        int32_t y = (d >> 16) * (int32_t)a[i] + (int32_t)((((uint32_t)d & 0xffff) * a[i]) >> 16) + s[k];
        s[k] = x;
        x = y;
#endif
    }
    s[k] = x;
    return x;
}

/* Decimate n samples by two in place: the first sample of each pair enters branch 1, the second
 * branch 0, and the pair yields one output. Stage 0 runs at the highest rate. Passes always hold
 * whole output frames, so pairs never straddle a pass. */
HOT void decimate_filter(struct modeldigi_osc_filter *f, const uint16_t *a, unsigned count,
                         int32_t *x, unsigned n)
{
    for (unsigned i = 0; i < n; i += 2) {
        int32_t odd = branch(f->state[1], a, 1, count, x[i]);
        x[i / 2] = (branch(f->state[0], a, 0, count, x[i + 1]) >> 1) + (odd >> 1);
    }
}

/* Fixed stage sizes let the compiler unroll the allpass branches. */
static void decimate(modeldigi_osc_state *s, unsigned stage, int32_t *x, unsigned n)
{
    struct modeldigi_osc_filter *f = &s->filter[stage];
    unsigned rate = MODELDIGI_OVERSAMPLE >> stage;
    if (rate == 2)
        decimate_filter(f, allpass96, 6, x, n);
    else if (rate == 4)
        decimate_filter(f, allpass192, 3, x, n);
    else if (rate == 8)
        decimate_filter(f, allpass384, 2, x, n);
    else
        decimate_filter(f, allpass768, 2, x, n);
}

void modeldigi_osc_init(modeldigi_osc_state *state)
{
    unsigned char *bytes = (unsigned char *)state;
    for (size_t i = 0; i < offsetof(modeldigi_osc_state, workspace); ++i) bytes[i] = 0;
    state->voice[1].phase = (ONE / 3) << 16;
    state->voice[2].phase = (2 * (ONE / 3)) << 16;
}

void modeldigi_osc_render(modeldigi_osc_state *s, const modeldigi_osc_params *p,
                         int32_t *output, size_t frames)
{
    if (!frames) return;
#if EMAC
    /* The firmware's render uses the EMAC too: keep its MACSR. */
    uint32_t macsr;
    __asm__ volatile ("move.l %%macsr,%0\n\tmove.l %1,%%macsr" : "=&d"(macsr) : "d"(EMAC_MODE));
#endif
    voice_lines lines[3], control;
    uint64_t step[3];
    int fresh[3];
    for (unsigned v = 0; v < 3; ++v) {
        unsigned shape = p->waveform[v] <= 6 ? p->waveform[v] : 0;
        describe(&lines[v], shape, p->level[v]);
        step[v] = increment(bounded(p->frequency_q16_16[v]));
        fresh[v] = s->voice[v].level != p->level[v] || s->voice[v].waveform != shape;
        s->voice[v].level = p->level[v];
    }
    uint32_t depth = p->modulation_depth_q16_16 > 131072 ? 131072 : p->modulation_depth_q16_16;
    int modulated = p->modulation_enabled && depth;
    int32_t oa[2] = { 0, 0 }, ob[2] = { 0, 0 }, on = 0;
    uint64_t lo = increment(HZ_MIN), hi = increment(HZ_MAX);
    if (modulated) {
        /* Oscillator 3's control tap before its level, in octaves: opposite audio sign, halved,
         * plus bias; pulses are 0..1, high during the audio-low interval. */
        describe(&control, lines[2].shape, 65535);
        if (control.shape < 4) {
            for (int k = 0; k < 2; ++k) {
                oa[k] = mulhi(-line_a[control.shape][k] / 2 + control_bias[control.shape], depth << 6);
                ob[k] = mulhi(-line_b[control.shape][k] / 2, depth << 6);
            }
        } else {
            on = (int32_t)depth;
        }
    }
    int32_t *mix = s->workspace.mix;
    octave_factor *mod = s->workspace.mod;
    for (size_t done = 0; done < frames;) {
        unsigned n = frames - done < CHUNK ? (unsigned)(frames - done) : CHUNK;
        unsigned m = n * MODELDIGI_OVERSAMPLE;
        if (modulated) {
            /* Oscillator 3 first: its phase before a sub-sample sets that sub-sample's factor, which
             * also drives oscillator 3 itself when CONTROL connects it. The factor is evaluated at
             * each output frame's start and wherever oscillator 3 enters another segment, and held
             * between: pulse levels stay exact, and ramp and reset jumps are never delayed. */
            struct modeldigi_osc_voice *o = &s->voice[2];
            voice_run r;
            int primed = o->primed;
            uint64_t inc = step[2];
            octave_factor f = { 0, 0 };
            unsigned segment = 0;
            for (unsigned i = 0; i < m; ++i) {
                uint32_t x = (uint32_t)((i ? r.phase : o->phase) >> 32);
                unsigned k = control.shape < 4 ? x >> 31 : x >= control.fall && x < control.rise;
                int refresh = i % MODELDIGI_OVERSAMPLE == 0 || k != segment;
                if (refresh) {
                    int32_t octaves = control.shape < 4 ? line(oa[k], ob[k], x) : k ? on : 0;
                    segment = k;
                    f = factor(octaves);
                    if (!octaves)
                        f.t = 0;
                    if (p->osc3_control)
                        inc = octaves ? modulate(step[2], f, lo, hi) : step[2];
                }
                mod[i] = f;
                if (!i)
                    begin(o, &r, &lines[2], inc, fresh[2] || !primed);
                /* The increment changes only where the factor is refreshed. */
                int32_t out = refresh ? advance(&r, &lines[2], inc) : steady(&r, &lines[2], inc);
                mix[i] = lines[2].silent && !fresh[2] && primed ? 0 : out;
#ifdef VOICE_ASM
                /* Sub-sample 0 above, after begin; the rest in the routine. */
                if (m > 1) {
                    source_job j;   /* field by field: an initializer's zero fill would call memset */
                    j.v.step = step[2];
                    j.v.lo = lo;
                    j.v.hi = hi;
                    j.v.inc = inc;
                    j.v.r = &r;
                    j.v.d = &lines[2];
                    j.v.mod = mod + 1;
                    j.v.mix = mix + 1;
                    j.v.m = m - 1;
                    j.oa[0] = oa[0];
                    j.oa[1] = oa[1];
                    j.ob[0] = ob[0];
                    j.ob[1] = ob[1];
                    j.on = on;
                    j.f = f;
                    j.segment = segment;
                    j.control = p->osc3_control;
                    j.keep = lines[2].silent && !fresh[2] && primed ? 0 : 0xffffffffu;
                    j.exp2 = exp2_q15;
                    j.frame = MODELDIGI_OVERSAMPLE;
                    j.left = MODELDIGI_OVERSAMPLE;   /* the next frame start is sub-sample MODELDIGI_OVERSAMPLE */
                    modeldigi_source_run(&j);
                    break;
                }
#endif
            }
            finish(o, &r);
            run_voice(&s->voice[0], &lines[0], step[0], mod, lo, hi, mix, m, fresh[0], 0);
            run_voice(&s->voice[1], &lines[1], step[1], mod, lo, hi, mix, m, fresh[1], 0);
        } else {
            for (unsigned v = 0; v < 3; ++v)
                run_voice(&s->voice[v], &lines[v], step[v], 0, lo, hi, mix, m, fresh[v], v == 0);
        }
        fresh[0] = fresh[1] = fresh[2] = 0;
        for (unsigned i = 0; i < m; ++i)
            mix[i] *= 2;   /* Q26 sum to Q31 with the approved 1/16 gain */
        for (unsigned stage = 0, k = m; k > n; ++stage, k /= 2)
            decimate(s, stage, mix, k);
        for (unsigned i = 0; i < n; ++i)
            output[done + i] = mix[i];
        done += n;
    }
#if EMAC
    __asm__ volatile ("move.l %0,%%macsr" : : "d"(macsr));
#endif
}
