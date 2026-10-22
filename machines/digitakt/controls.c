/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Digitakt control decoding: stored 8.8 values and the trig note to oscillator-bank parameters.
 * Uses 32-bit arithmetic only: a mod links no libgcc 64-bit helpers. */
#include "controls.h"

#define BASE_HZ    17145893   /* 261.6256 Hz (note 60 at 8'), Q16.16 */
#define ONE_HZ     65536
#define CENTER     (64 << 8)
#define TOP        (127 << 8)

/* FRQ table, cents x 100 for |position - 64| = 0..63: 100 (n/21)^2, then 600/42 per step;
 * exact semitones at n = 21, 28, ..., 63. */
static const int32_t frq_cx100[64] = {
    0, 23, 91, 204, 363, 567, 816, 1111, 1451, 1837, 2268, 2744, 3265, 3832, 4444, 5102,
    5805, 6553, 7347, 8186, 9070, 10000, 11429, 12857, 14286, 15714, 17143, 18571, 20000, 21429, 22857, 24286,
    25714, 27143, 28571, 30000, 31429, 32857, 34286, 35714, 37143, 38571, 40000, 41429, 42857, 44286, 45714, 47143,
    48571, 50000, 51429, 52857, 54286, 55714, 57143, 58571, 60000, 61429, 62857, 64286, 65714, 67143, 68571, 70000
};
/* 2^(i/64), Q15. */
static const uint32_t exp2_q15[65] = {
    32768, 33125, 33486, 33850, 34219, 34591, 34968, 35349, 35734, 36123, 36516, 36914, 37316, 37722, 38133, 38548,
    38968, 39392, 39821, 40255, 40693, 41136, 41584, 42037, 42495, 42958, 43425, 43898, 44376, 44859, 45348, 45842,
    46341, 46846, 47356, 47871, 48393, 48920, 49452, 49991, 50535, 51085, 51642, 52204, 52773, 53347, 53928, 54515,
    55109, 55709, 56316, 56929, 57549, 58176, 58809, 59449, 60097, 60751, 61413, 62081, 62757, 63441, 64132, 64830,
    65536
};
/* Ranges LO, 32', 16', 8', 4', 2' in octaves, Q16; LO is the frozen within-mode LO/audio ratio. */
static const int32_t range_q16[6] = { -459156, -131072, -65536, 0, 65536, 131072 };
/* log2 Hz, Q16, of the frozen CONTROL-off LO table at amounts 0, .25, .5, .75, 1. */
static const int32_t lo_log2_q16[5] = { -253494, -160172, -63267, 31929, 127608 };
/* Recorded amount (Q16) to calibrated depth (octaves, Q16). */
static const int32_t depth_amount[7] = { 0, 3277, 6554, 16384, 32768, 49152, 65536 };
static const int32_t depth_q16[7] = { 0, 3736, 7426, 18494, 29035, 49466, 98892 };
static const uint8_t wave12[6] = { 0, 1, 3, 4, 5, 6 };
static const uint8_t wave3[6] = { 0, 2, 3, 4, 5, 6 };

static int32_t clamp(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static int selector(int32_t v)
{
    return (int)clamp((v + 128) >> 8, 0, 5);
}

/* Signed cents x 100 of a FRQ position; fractional positions interpolate. */
int32_t modeldigi_frq_cents(int32_t v)
{
    int32_t x = clamp(v, 0, TOP) - CENTER, a = x < 0 ? -x : x;
    int32_t i = a >> 8, c = i >= 63 ? frq_cx100[63]
        : frq_cx100[i] + (frq_cx100[i + 1] - frq_cx100[i]) * (a & 255) / 256;
    return x < 0 ? -c : c;
}

/* Nearest stored value to a whole semitone, using the existing tuning curve. */
static int32_t semitone_value(int semitone, int wide)
{
    if (!semitone) return CENTER;
    if (!wide || semitone % 5 == 0) {
        int n = wide ? semitone / 5 : semitone;
        int offset = (21 + ((n < 0 ? -n : n) - 1) * 7) << 8;
        return CENTER + (n < 0 ? -offset : offset);
    }
    int32_t lo = 1 << 8, hi = TOP, target = semitone * 10000;
    int factor = wide ? 5 : 1;
    while (lo < hi) {
        int32_t mid = (lo + hi) / 2;
        if (factor * modeldigi_frq_cents(mid) < target) lo = mid + 1;
        else hi = mid;
    }
    if (lo > (1 << 8)) {
        int32_t above = factor * modeldigi_frq_cents(lo) - target;
        int32_t below = target - factor * modeldigi_frq_cents(lo - 1);
        if (below < above) --lo;
    }
    return lo;
}

int32_t modeldigi_frq_step(int32_t value, int direction, int wide)
{
    value = clamp(value, 1 << 8, TOP);
    int32_t cents = (wide ? 5 : 1) * modeldigi_frq_cents(value);
    int nearest = (cents + (cents < 0 ? -5000 : 5000)) / 10000;
    int32_t anchor = semitone_value(nearest, wide);
    if ((direction > 0 && value >= anchor) || (direction < 0 && value <= anchor))
        nearest += direction;
    int limit = wide ? 35 : 7;
    return semitone_value(clamp(nearest, -limit, limit), wide);
}

int modeldigi_frq_coarse(int *remainder, int counts, int wide)
{
    *remainder += counts * (wide ? 70 : 14);
    int steps = *remainder / 512;
    *remainder %= 512;
    return steps;
}

int modeldigi_frq_anchor(int32_t value, int wide)
{
    int32_t cents = (wide ? 5 : 1) * modeldigi_frq_cents(value);
    int nearest = (cents + (cents < 0 ? -5000 : 5000)) / 10000;
    return value == semitone_value(nearest, wide);
}

static int32_t octaves(int32_t cx100)
{
    return cx100 * 1024 / 1875;   /* cents x 100 to octaves, Q16 */
}

/* base * 2^octaves; Q16.16 Hz, saturating. Linear interpolation keeps error below 0.03 cents. */
static uint32_t scale_hz(uint32_t base, int32_t oct)
{
    int32_t whole = oct >> 16;
    uint32_t frac = (uint32_t)oct & 0xffff, i = frac >> 10;
    uint32_t t = exp2_q15[i] + (((exp2_q15[i + 1] - exp2_q15[i]) * (frac & 1023)) >> 10);
    uint32_t hz = (((base >> 16) * t) << 1) + (((base & 0xffff) * t) >> 15);
    if (whole >= 0)
        return whole > 15 || hz > (UINT32_MAX >> whole) ? UINT32_MAX : hz << whole;
    return whole < -31 ? 0 : hz >> -whole;
}

/* Oscillator 3 at LO with CONTROL off: the frozen five-point table, log-interpolated. */
static int32_t lo_free_log2(int32_t wide_cx100)
{
    int32_t pos = clamp(wide_cx100 + 350250, 0, 700500), i = pos / 175125;
    if (i > 3)
        i = 3;
    return lo_log2_q16[i] + (lo_log2_q16[i + 1] - lo_log2_q16[i]) * ((pos - 175125 * i) >> 4) / 10945;
}

static uint32_t depth(int32_t v)
{
    int32_t a = (int32_t)((uint32_t)clamp(v, 0, TOP) * 65536u / TOP), k = 1;
    while (k < 6 && a > depth_amount[k])
        ++k;
    return (uint32_t)(depth_q16[k - 1] + (depth_q16[k] - depth_q16[k - 1]) * (a - depth_amount[k - 1])
                      / (depth_amount[k] - depth_amount[k - 1]));
}

static uint16_t level(int32_t v)
{
    uint32_t x = (uint32_t)clamp(v, 0, TOP);
    uint32_t l = (x * x + 8064) / 16129;   /* 65535 (v/127)^2 */
    return (uint16_t)(l > 65535 ? 65535 : l);
}

void modeldigi_decode(const int16_t values[MD_VALUES], int32_t note, modeldigi_osc_params *p)
{
    static const uint8_t range_at[3] = { MD_RNG1, MD_RNG2, MD_RNG3 }, wave_at[3] = { MD_WAV1, MD_WAV2, MD_WAV3 };
    int control = values[MD_CTRL] >= CENTER;
    int32_t keyed = (clamp(note, 0, 127 << 16) - (60 << 16)) / 12;
    for (int v = 0; v < 3; ++v) {
        int range = selector(values[range_at[v]]), wave = selector(values[wave_at[v]]);
        int32_t oct = range_q16[range] + keyed;
        uint32_t base = BASE_HZ;
        p->waveform[v] = v < 2 ? wave12[wave] : wave3[wave];
        p->level[v] = level(values[MD_LEV1 + v]);
        if (v == 1)
            oct += octaves(modeldigi_frq_cents(values[MD_FRQ2]));
        if (v == 2 && control)
            oct += octaves(modeldigi_frq_cents(values[MD_FRQ3]));
        if (v == 2 && !control) {
            /* CONTROL off: no note, a five-times wider sweep, C2 for the audio ranges. */
            int32_t wide = 5 * modeldigi_frq_cents(values[MD_FRQ3]);
            if (range == 0) {
                base = ONE_HZ;
                oct = lo_free_log2(wide);
            } else {
                oct = range_q16[range] - (2 << 16) + octaves(wide);
            }
        }
        p->frequency_q16_16[v] = scale_hz(base, oct);
    }
    p->modulation_depth_q16_16 = depth(values[MD_DPTH]);
    p->modulation_enabled = values[MD_MOD] >= CENTER;
    p->analog = values[MD_ANLG] >= CENTER;
    p->osc3_control = control;
}

int modeldigi_osc3_free(const uint8_t *sound)
{
    const uint8_t *word = sound + 0x14 + 2 * (46 + MD_CTRL - 8);   /* the second page's values: slots 46-52 */
    return (int16_t)(word[0] << 8 | word[1]) < CENTER;
}

/* ---- value text ------------------------------------------------------------------------------ */

static char *put_text(char *buf, const char *s)
{
    while (*s)
        *buf++ = *s++;
    *buf = 0;
    return buf;
}

/* value / 10^decimals with that many decimals, an explicit sign if signed. */
static char *put_number(char *buf, int32_t value, int decimals, int is_signed)
{
    char digits[12];
    int n = 0;
    uint32_t a = (uint32_t)(value < 0 ? -value : value);
    if (value < 0)
        *buf++ = '-';
    else if (is_signed && value > 0)
        *buf++ = '+';
    do {
        digits[n++] = (char)('0' + a % 10);
        a /= 10;
        if (n == decimals)
            digits[n++] = '.';
    } while (a || n <= decimals + (decimals > 0));   /* a digit before the point */
    while (n)
        *buf++ = digits[--n];
    *buf = 0;
    return buf;
}

/* The pop-up shares the title bar with the long name: about 7 characters remain. */
static void cents_text(char *buf, int32_t cx100, int popup)
{
    int32_t a = cx100 < 0 ? -cx100 : cx100;
    if (popup && a < 10000)
        put_text(put_number(buf, cx100, 2, 1), "c");
    else if (popup && a < 100000)
        put_text(put_number(buf, cx100 / 10, 1, 1), "c");
    else if (popup)
        put_text(put_number(buf, cx100 / 100, 0, 1), "c");
    else if (a < 1000)
        put_number(buf, cx100 / 10, 1, 1);
    else
        put_number(buf, cx100 / 100, 0, 1);
}

int modeldigi_text(int kind, int32_t v, int popup, char *buf)
{
    static const char *const range[6] = { "LO", "32'", "16'", "8'", "4'", "2'" };
    static const char *const wave12_short[6] = { "TRI", "TRSW", "SAW", "SQR", "RECT", "NARW" };
    static const char *const wave12_long[6] = { "TRIANGLE", "TRI/SAW", "SAW", "SQUARE", "WIDE RECT", "NARROW RECT" };
    static const char *const wave3_short[6] = { "TRI", "RSAW", "SAW", "SQR", "RECT", "NARW" };
    static const char *const wave3_long[6] = { "TRIANGLE", "REVERSE SAW", "SAW", "SQUARE", "WIDE RECT", "NARROW RECT" };
    switch (kind) {
    case MD_TEXT_RANGE: put_text(buf, range[selector(v)]); break;
    case MD_TEXT_WAVE12: put_text(buf, (popup ? wave12_long : wave12_short)[selector(v)]); break;
    case MD_TEXT_WAVE3: put_text(buf, (popup ? wave3_long : wave3_short)[selector(v)]); break;
    case MD_TEXT_FRQ: cents_text(buf, modeldigi_frq_cents(v), popup); break;
    case MD_TEXT_FRQ_WIDE: cents_text(buf, 5 * modeldigi_frq_cents(v), popup); break;
    case MD_TEXT_SWITCH: put_text(buf, v >= CENTER ? "ON" : "OFF"); break;
    default: {
        int32_t milli = (int32_t)(depth(v) * 1000 / 65536);
        if (popup)
            put_text(put_number(buf, milli, 3, 0), " OCT");
        else
            put_number(buf, milli / 10, 2, 0);
    }
    }
    return 1;
}
