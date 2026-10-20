/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "../dsp/oscillator.h"

static modeldigi_osc_params defaults(void)
{
    modeldigi_osc_params p = {0};
    p.frequency_q16_16[0] = p.frequency_q16_16[1] = 440u * 65536;
    p.frequency_q16_16[2] = 33564;
    p.waveform[0] = p.waveform[1] = 3;
    p.level[0] = 65535;
    return p;
}

static void splitting(void)
{
    modeldigi_osc_state a, b, independent;
    modeldigi_osc_params p = defaults();
    int32_t whole[257], parts[257], other[257];
    modeldigi_osc_init(&a); modeldigi_osc_init(&b); modeldigi_osc_init(&independent);
    p.modulation_enabled = p.osc3_control = true;
    p.modulation_depth_q16_16 = 65536;
    for (unsigned shape = 0; shape < 7; ++shape) {
        p.waveform[0] = (uint8_t)shape;
        p.waveform[2] = (uint8_t)(6 - shape);
        modeldigi_osc_state before = a;
        modeldigi_osc_render(&a, &p, NULL, 0);
        assert(!memcmp(&a, &before, sizeof(a)));
        modeldigi_osc_render(&a, &p, whole, 257);
        for (unsigned i = 0; i < 257; ++i) {
            modeldigi_osc_render(&b, &p, parts + i, 1);
            modeldigi_osc_render(&independent, &p, other + i, 1);
        }
        assert(!memcmp(whole, parts, sizeof(whole)));
        assert(!memcmp(parts, other, sizeof(parts)));
        assert(!memcmp(&a, &b, sizeof(a)));
    }
}

static void continuity(void)
{
    modeldigi_osc_state a, b;
    modeldigi_osc_params p = defaults(), muted;
    int32_t samples[1024];
    modeldigi_osc_init(&a); modeldigi_osc_init(&b);
    muted = p; muted.level[0] = 0;
    modeldigi_osc_render(&a, &p, samples, 1024);
    modeldigi_osc_render(&b, &muted, samples, 1024);
    assert(!memcmp(a.voice, b.voice, sizeof(a.voice)));
    for (unsigned i = 0; i < 1024; ++i) assert(samples[i] == 0);
    int32_t restored[1024];
    modeldigi_osc_render(&a, &p, samples, 1024);
    modeldigi_osc_render(&b, &p, restored, 1024);
    /* The decimator's finite tail must settle without a phase reset. */
    assert(!memcmp(samples + 256, restored + 256, 768 * sizeof(*samples)));
    b = a;
    muted = p;
    p.waveform[0] = 6;
    modeldigi_osc_render(&a, &p, samples, 1);
    modeldigi_osc_render(&b, &muted, restored, 1);
    assert(a.voice[0].phase == b.voice[0].phase);
    modeldigi_osc_init(&a); modeldigi_osc_init(&b);
    p = defaults(); muted = p;
    p.modulation_enabled = muted.modulation_enabled = true;
    p.modulation_depth_q16_16 = muted.modulation_depth_q16_16 = 65536;
    muted.level[2] = 65535;
    modeldigi_osc_render(&a, &p, samples, 1024);
    modeldigi_osc_render(&b, &muted, samples, 1024);
    assert(!memcmp(a.voice, b.voice, sizeof(a.voice)));
}

static void bounds(void)
{
    modeldigi_osc_state s;
    modeldigi_osc_params p = defaults();
    int32_t guarded[34];
    uint32_t random = 1;
    int64_t peak = 0;
    modeldigi_osc_init(&s);
    for (unsigned test = 0; test < 1000; ++test) {
        for (unsigned v = 0; v < 3; ++v) {
            random = random * 1664525u + 1013904223u;
            p.frequency_q16_16[v] = test % 2 ? random : 0;
            p.waveform[v] = (uint8_t)(test % 9);
            p.level[v] = test % 3 ? 65535 : 0;
        }
        p.modulation_enabled = test % 2;
        p.osc3_control = test % 3;
        p.modulation_depth_q16_16 = test % 2 ? UINT32_MAX : 0;
        guarded[0] = guarded[33] = INT32_C(1234567);
        modeldigi_osc_render(&s, &p, guarded + 1, 32);
        assert(guarded[0] == 1234567 && guarded[33] == 1234567);
        for (unsigned i = 1; i < 33; ++i) {
            int64_t magnitude = guarded[i] < 0 ? -(int64_t)guarded[i] : guarded[i];
            if (magnitude > peak) peak = magnitude;
            assert(magnitude <= 1076291389); /* -6 dBFS */
        }
    }
    printf("PASS: oscillator split/state/mute/bounds; tested transition peak %.6f FS\n", peak / 2147483648.0);
}

static void exact_controls(void)
{
    modeldigi_osc_state a, b;
    modeldigi_osc_params p = defaults(), q;
    int32_t x[1024], y[1024];
    for (unsigned test = 0; test < 4; ++test) {
        p = defaults(); q = p;
        if (test == 0) { p.modulation_enabled = true; p.modulation_depth_q16_16 = 0; }
        if (test == 1) { p.waveform[0] = 255; q.waveform[0] = 0; }
        if (test == 2) { p.frequency_q16_16[0] = 0; q.frequency_q16_16[0] = 4096; }
        if (test == 3) { p.frequency_q16_16[0] = UINT32_MAX; q.frequency_q16_16[0] = 20000u * 65536; }
        modeldigi_osc_init(&a); modeldigi_osc_init(&b);
        modeldigi_osc_render(&a, &p, x, 1024);
        modeldigi_osc_render(&b, &q, y, 1024);
        assert(!memcmp(x, y, sizeof(x)));
    }
    p = defaults(); q = p; q.level[0] = 32768;
    modeldigi_osc_init(&a); modeldigi_osc_init(&b);
    modeldigi_osc_render(&a, &p, x, 1024);
    modeldigi_osc_render(&b, &q, y, 1024);
    for (unsigned i = 0; i < 1024; ++i) {
        int64_t error = y[i] - (int64_t)x[i] * 32768 / 65535;
        assert(error > -128 && error < 128);
    }
}

static void bank_sum_and_pulse_edges(void)
{
    modeldigi_osc_state bank, isolated[3];
    modeldigi_osc_params p = defaults(), q;
    int32_t all[256], one[3][256];
    modeldigi_osc_init(&bank);
    for (unsigned v = 0; v < 3; ++v) {
        p.level[v] = 65535;
        p.waveform[v] = (uint8_t)(4 + v);
        p.frequency_q16_16[v] = (375 + 125 * v) * 65536;
        modeldigi_osc_init(&isolated[v]);
    }
    modeldigi_osc_render(&bank, &p, all, 256);
    for (unsigned v = 0; v < 3; ++v) {
        q = p;
        for (unsigned j = 0; j < 3; ++j) q.level[j] = v == j ? 65535 : 0;
        modeldigi_osc_render(&isolated[v], &q, one[v], 256);
    }
    for (unsigned i = 0; i < 256; ++i) {
        int64_t error = (int64_t)all[i] - one[0][i] - one[1][i] - one[2][i];
        assert(error > -128 && error < 128);
    }
    p = defaults(); p.waveform[0] = 6; p.frequency_q16_16[0] = 16000u * 65536;
    modeldigi_osc_init(&bank);
    int64_t total = 0;
    for (unsigned i = 0; i < 4000; ++i) {
        modeldigi_osc_render(&bank, &p, all, 1);
        if (i >= 1000) total += all[0];
    }
    /* At factor 1 this crosses both narrow-pulse edges in one interval. */
    assert(total > -INT64_C(3000) * 21475 && total < INT64_C(3000) * 21475);
}

static void modulation_routing(void)
{
    modeldigi_osc_state dry, open, feedback;
    modeldigi_osc_params p = defaults();
    int32_t samples[256];
    modeldigi_osc_init(&dry); modeldigi_osc_init(&open); modeldigi_osc_init(&feedback);
    p.frequency_q16_16[2] = 120u * 65536;
    p.waveform[2] = 5;
    modeldigi_osc_render(&dry, &p, samples, 256);
    p.modulation_enabled = true; p.modulation_depth_q16_16 = 65536;
    modeldigi_osc_render(&open, &p, samples, 256);
    assert(open.voice[0].phase != dry.voice[0].phase);
    assert(open.voice[1].phase != dry.voice[1].phase);
    assert(!memcmp(&open.voice[2], &dry.voice[2], sizeof(open.voice[2])));
    p.osc3_control = true;
    modeldigi_osc_render(&feedback, &p, samples, 256);
    assert(feedback.voice[2].phase != open.voice[2].phase);
}

static void carrier_phase_does_not_change_pitch(void)
{
    modeldigi_osc_state a, b;
    modeldigi_osc_params p = defaults();
    int32_t sample;
    const uint64_t mask = (UINT64_C(1) << 48) - 1;
    modeldigi_osc_init(&a); modeldigi_osc_init(&b);
    b.voice[0].phase = UINT64_C(3) << 46;
    a.voice[2].phase = b.voice[2].phase = UINT64_C(3) << 46;
    /* Frozen slow-feedback condition in the engine's resolved integer units. */
    p.frequency_q16_16[0] = 17154938;
    p.frequency_q16_16[2] = 133659;
    p.waveform[2] = 2;
    p.modulation_depth_q16_16 = 29035;
    p.modulation_enabled = p.osc3_control = true;
    for (unsigned i = 0; i < 48000; ++i) {
        uint64_t before_a = a.voice[0].phase, before_b = b.voice[0].phase;
        modeldigi_osc_render(&a, &p, &sample, 1);
        modeldigi_osc_render(&b, &p, &sample, 1);
        assert(((a.voice[0].phase - before_a) & mask) == ((b.voice[0].phase - before_b) & mask));
        assert(!memcmp(&a.voice[2], &b.voice[2], sizeof(a.voice[2])));
    }
}

int main(void)
{
    splitting(); continuity(); bounds(); exact_controls(); bank_sum_and_pulse_edges(); modulation_routing(); carrier_phase_does_not_change_pitch();
    return 0;
}
