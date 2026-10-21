/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "../dsp/oscillator.h"

static modeldigi_osc_params bank(unsigned shape)
{
    modeldigi_osc_params p = {0};
    p.frequency_q16_16[0] = 440u * 65536;
    p.frequency_q16_16[1] = 331u * 65536;
    p.frequency_q16_16[2] = 1000u * 65536;
    for (unsigned v = 0; v < 3; ++v) {
        p.waveform[v] = (uint8_t)((shape + 2 * v) % 7);
        p.level[v] = 65535;
    }
    return p;
}

/* Rendering in one call or sample by sample gives the same output and state. */
static void splitting(void)
{
    for (unsigned shape = 0; shape < 7; ++shape) {
        modeldigi_osc_params p = bank(shape);
        modeldigi_osc_state a, b;
        int32_t whole[257], parts[257];
        modeldigi_osc_init(&a); modeldigi_osc_init(&b);
        modeldigi_osc_state before = a;
        modeldigi_osc_render(&a, &p, NULL, 0);
        assert(!memcmp(&a, &before, sizeof a));
        modeldigi_osc_render(&a, &p, whole, 257);
        for (unsigned i = 0; i < 257; ++i)
            modeldigi_osc_render(&b, &p, parts + i, 1);
        assert(!memcmp(whole, parts, sizeof whole) && !memcmp(&a, &b, sizeof a));
    }
}

/* The bank is exactly the sum of each voice alone; muted voices keep running. */
static void mixing(void)
{
    for (unsigned shape = 0; shape < 7; ++shape) {
        modeldigi_osc_params p = bank(shape);
        modeldigi_osc_state all, alone[3];
        int32_t mix[300], part[3][300];
        modeldigi_osc_init(&all);
        modeldigi_osc_render(&all, &p, mix, 300);
        for (unsigned v = 0; v < 3; ++v) {
            modeldigi_osc_params q = p;
            for (unsigned w = 0; w < 3; ++w)
                if (w != v) q.level[w] = 0;
            modeldigi_osc_init(&alone[v]);
            modeldigi_osc_render(&alone[v], &q, part[v], 300);
            assert(!memcmp(&alone[v], &all, sizeof all));
        }
        int audible[3] = {0};
        for (unsigned i = 0; i < 300; ++i) {
            assert(mix[i] == part[0][i] + part[1][i] + part[2][i]);
            for (unsigned v = 0; v < 3; ++v) audible[v] |= part[v][i] != 0;
        }
        assert(audible[0] && audible[1] && audible[2]);
    }
    modeldigi_osc_params p = bank(0);
    p.level[0] = p.level[1] = p.level[2] = 0;
    modeldigi_osc_state s;
    int32_t out[64];
    modeldigi_osc_init(&s);
    modeldigi_osc_render(&s, &p, out, 64);
    for (unsigned i = 0; i < 64; ++i) assert(out[i] == 0);
    assert(s.phase[0] != 0);
}

/* One sample's phase step matches the frequency, clamped to 1/16..20000 Hz; 440 Hz wraps 440 times a second. */
static void pitch(void)
{
    static const uint32_t cases[] = { 1, 4096, 440u * 65536, 17145893, 20000u * 65536, UINT32_MAX };
    for (unsigned c = 0; c < sizeof cases / sizeof *cases; ++c) {
        uint32_t f = cases[c] < 4096 ? 4096 : cases[c] > 20000u * 65536 ? 20000u * 65536 : cases[c];
        modeldigi_osc_params p = bank(3);
        p.frequency_q16_16[0] = cases[c];
        modeldigi_osc_state s;
        int32_t out[1];
        modeldigi_osc_init(&s);
        modeldigi_osc_render(&s, &p, out, 1);
        double expected = f / 65536.0 / 48000.0 * 4294967296.0;
        assert(s.phase[0] > expected - 1 && s.phase[0] <= expected);
    }
    modeldigi_osc_params p = bank(3);
    modeldigi_osc_state s;
    int32_t out[1];
    unsigned wraps = 0;
    modeldigi_osc_init(&s);
    for (unsigned i = 0; i < 48000; ++i) {
        uint32_t before = s.phase[0];
        modeldigi_osc_render(&s, &p, out, 1);
        wraps += s.phase[0] < before;
    }
    assert(wraps == 440 || wraps == 439);
}

/* Full-level pulses on all three voices stay within three narrow-pulse highs, 3 * 2 * 0.805 / 16 of full
 * scale; bad shapes are triangles. */
static void bounds(void)
{
    modeldigi_osc_params p = bank(0);
    p.waveform[0] = p.waveform[1] = p.waveform[2] = 6;
    p.frequency_q16_16[0] = p.frequency_q16_16[1] = p.frequency_q16_16[2] = 1000u * 65536;   /* 48 samples a cycle */
    modeldigi_osc_state s;
    int32_t out[48000];
    modeldigi_osc_init(&s);
    modeldigi_osc_render(&s, &p, out, 48000);
    int32_t lo = 0, hi = 0;
    for (unsigned i = 0; i < 48000; ++i) { lo = out[i] < lo ? out[i] : lo; hi = out[i] > hi ? out[i] : hi; }
    assert(hi <= 3 * 0.805 * 2 / 16 * 2147483648.0 + 3 && hi > 0 && lo < 0);
    modeldigi_osc_params a = bank(0), b = bank(0);
    a.waveform[0] = 0;
    b.waveform[0] = 200;
    modeldigi_osc_state x, y;
    int32_t ox[256], oy[256];
    modeldigi_osc_init(&x); modeldigi_osc_init(&y);
    modeldigi_osc_render(&x, &a, ox, 256);
    modeldigi_osc_render(&y, &b, oy, 256);
    assert(!memcmp(ox, oy, sizeof ox));
}

int main(void)
{
    splitting();
    mixing();
    pitch();
    bounds();
    puts("PASS: naive bank split/mix/mute/pitch/bounds");
    return 0;
}
