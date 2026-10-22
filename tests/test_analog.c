/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The analog build's declared behaviour; built with MODELDIGI_ANALOG. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../dsp/oscillator.c"   /* the private mixer functions too */

#define CENT (16777216.0 / 1200)   /* 2^-24 octave units per cent */

static modeldigi_osc_params bank(void)
{
    modeldigi_osc_params p = {0};
    p.frequency_q16_16[0] = 440u * 65536;
    p.frequency_q16_16[1] = 331u * 65536;
    p.frequency_q16_16[2] = 1000u * 65536;
    p.waveform[0] = 3; p.waveform[1] = 4; p.waveform[2] = 0;
    p.level[0] = p.level[1] = p.level[2] = 40000;
    return p;
}

/* Any split of a render gives the same output and state as one call. */
static void splitting(void)
{
    modeldigi_osc_params p = bank();
    modeldigi_osc_state a, b;
    modeldigi_osc_init(&a);
    b = a;
    static int32_t whole[4801], parts[4801];
    modeldigi_osc_render(&a, &p, whole, 4801);
    static const unsigned sizes[] = { 1, 7, 33, 31, 64, 5, 100, 32 };
    for (unsigned at = 0, k = 0; at < 4801; ++k) {
        unsigned n = sizes[k % 8] < 4801 - at ? sizes[k % 8] : 4801 - at;
        modeldigi_osc_render(&b, &p, parts + at, n);
        at += n;
    }
    assert(!memcmp(whole, parts, sizeof whole) && !memcmp(&a, &b, sizeof a));
}

/* Over five minutes: bounded, about 2 cents RMS, seconds-long correlation, independent oscillators;
 * each block's pitch step follows the drift. */
static void wander(void)
{
    enum { BLOCKS = 1500 * 300 };
    static float d[3][BLOCKS];
    modeldigi_osc_params p = bank();
    modeldigi_osc_state s;
    int32_t out[32];
    modeldigi_osc_init(&s);
    for (unsigned b = 0; b < BLOCKS; ++b) {
        uint32_t before[3] = { s.phase[0], s.phase[1], s.phase[2] };
        modeldigi_osc_render(&s, &p, out, 32);
        for (unsigned v = 0; v < 3; ++v) {
            assert(s.drift[v] >= -6 * CENT - 1 && s.drift[v] <= 6 * CENT + 1);
            d[v][b] = (float)(s.drift[v] / CENT);
        }
        if (b % 997 == 0) {
            /* The block stepped by its drifted increment. */
            double nominal[3] = { 440, 331, 1000 };
            for (unsigned v = 0; v < 3; ++v) {
                double cycles = (uint32_t)(s.phase[v] - before[v]) / 4294967296.0;
                double expected = nominal[v] * 32 / 48000 * pow(2, s.drift[v] / 16777216.0);
                double frac = expected - floor(expected);
                double e = fabs(cycles - frac); e = e > .5 ? 1 - e : e;
                /* Within 0.1 cent: the engine's 2^x factor is accurate to about 0.05 cents. */
                assert(e < expected * 0.1 / 1731);
            }
        }
    }
    for (unsigned v = 0; v < 3; ++v) {
        double sum = 0, sq = 0;
        for (unsigned b = 0; b < BLOCKS; ++b) { sum += d[v][b]; sq += d[v][b] * d[v][b]; }
        double mean = sum / BLOCKS, rms = sqrt(sq / BLOCKS - mean * mean);
        assert(rms > 1.0 && rms < 3.0 && fabs(mean) < 1.5);
        /* Correlation time: lag where the autocorrelation falls below 1/e, in seconds. */
        unsigned lag = 1;
        for (; lag < 1500 * 30; lag *= 2) {
            double c = 0;
            for (unsigned b = 0; b + lag < BLOCKS; b += 7) c += (d[v][b] - mean) * (d[v][b + lag] - mean);
            c /= (BLOCKS - lag) / 7.0 * rms * rms;
            if (c < exp(-1)) break;
        }
        assert(lag >= 1500 / 2 && lag <= 1500 * 16);
        printf("oscillator %u: drift RMS %.2f cents, correlation time ~%.1f s\n", v + 1, rms, lag / 1500.0);
    }
    for (unsigned v = 0; v < 3; ++v)
        for (unsigned w = v + 1; w < 3; ++w) {
            double c = 0, a = 0, b = 0;
            for (unsigned k = 0; k < BLOCKS; ++k) { c += d[v][k] * d[w][k]; a += d[v][k] * d[v][k]; b += d[w][k] * d[w][k]; }
            assert(fabs(c / sqrt(a * b)) < 0.35);
        }
    /* A second instance wanders differently. */
    modeldigi_osc_state t;
    modeldigi_osc_init(&t);
    for (unsigned b = 0; b < 1500 * 5; ++b) { modeldigi_osc_render(&s, &p, out, 32); modeldigi_osc_render(&t, &p, out, 32); }
    assert(memcmp(s.drift, t.drift, sizeof s.drift));
}

/* The mixer curve over the bank's range (sums stay below about 0.35 of full scale): odd, monotone,
 * unity for small signals, tanh(drive * s) / drive within 2e-5 of full scale at the table's
 * effective drive, bounded by 1 / drive engine units. */
/* The mixer's whole curve, tanh(drive * s) / drive in output Q31. */
static int32_t saturate(int32_t q)
{
    return q + 6 * distortion6(q);
}

static void mixer_curve(void)
{
    const double unit = 2147483648.0 / 16, drive = MIX_DRIVE * unit / (4096 * 16777216.0);   /* 0.5996 */
    int32_t previous = INT32_MIN;
    for (int64_t q = -860000000; q <= 860000000; q += 16411) {
        int32_t y = saturate((int32_t)q);
        assert(saturate((int32_t)-q) == -y);
        assert((int64_t)y >= (int64_t)previous - 2048);   /* monotone to fixed-point quantization, 2^11 units (-120 dBFS) */
        previous = y;
        double exact = tanh(drive * (double)q / unit) / drive * unit;
        assert(fabs(y - exact) < 2e-5 * 2147483648.0);
        assert(fabs((double)y) <= unit / drive + 1);
        if (q > -(1 << 22) && q < (1 << 22))
            assert(fabs(y - (double)q) <= fabs((double)q) * 1e-3 + 2);
    }
}

/* The DC blocker settles a constant to zero, and a saturated narrow pulse keeps a zero mean (one
 * voice: several detuned pulses make intermodulation products a few hertz from DC, which pass). */
static void dc(void)
{
    modeldigi_osc_state s;
    modeldigi_osc_init(&s);
    int32_t y = 0;
    for (unsigned i = 0; i < 48000; ++i)
        y = dc_block(&s, 100000000);
    assert(y < 1024 && y > -2048);   /* the leak rounds to zero below 2^10 units */
    modeldigi_osc_params p = bank();
    p.waveform[0] = 6;
    p.frequency_q16_16[0] = 1000u * 65536;
    p.level[0] = 65535;
    p.level[1] = p.level[2] = 0;
    static int32_t out[48000 * 5];
    modeldigi_osc_init(&s);
    modeldigi_osc_render(&s, &p, out, 48000 * 5);
    double sum = 0;
    for (unsigned i = 48000; i < 48000 * 5; ++i) sum += out[i];
    /* Bound: a few cycles' worth of window error on a 0.1 full-scale pulse over 4000 cycles. */
    assert(fabs(sum / (48000 * 4) / 2147483648.0) < 1e-4);
}

int main(void)
{
    splitting();
    wander();
    mixer_curve();
    dc();
    puts("PASS: analog drift split/bounds/rate/independence, mixer curve and DC");
    return 0;
}
