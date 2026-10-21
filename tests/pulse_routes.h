/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MODELDIGI_TEST_PULSE_ROUTES_H
#define MODELDIGI_TEST_PULSE_ROUTES_H
#include "../dsp/oscillator.h"

/* The first voice's C pulse loop is the oracle for additive ColdFire pulse routing. */
static int pulse_routes(void)
{
    static modeldigi_osc_state first, added;
    static int32_t a[32], b[32];
    const uint64_t phases[] = {0, UINT64_C(0x7fffffffffff0000), UINT64_C(0xffffffffffff0000)};
    const uint32_t hz[] = {4096, 65536, 8090812, 17145813, 410976256, 1310720000};
    const uint16_t levels[] = {0, 1, 32575, 65535};
    const uint8_t shapes[] = {4, 5, 6, 3, 6, 4, 0, 5};
    const unsigned lengths[] = {1, 7, 8, 16, 31, 32};
    for (unsigned voice = 1; voice < 3; ++voice)
        for (unsigned phase = 0; phase < 3; ++phase) {
            modeldigi_osc_init(&first);
            modeldigi_osc_init(&added);
            for (unsigned v = 0; v < 3; ++v)
                first.voice[v].phase = added.voice[v].phase = phases[phase];
            for (unsigned step = 0; step < 96; ++step) {
                modeldigi_osc_params p = {0}, q;
                for (unsigned v = 0; v < 3; ++v) {
                    p.frequency_q16_16[v] = hz[(step / 3) % 6];
                    p.waveform[v] = shapes[(step / 6) % 8];
                }
                p.level[0] = levels[(step / 2) % 4];
                q = p;
                q.level[voice] = p.level[0];
                q.level[0] = 0;
                unsigned n = lengths[step % 6];
                modeldigi_osc_render(&first, &p, a, n);
                modeldigi_osc_render(&added, &q, b, n);
                for (unsigned i = 0; i < n; ++i)
                    if (a[i] != b[i]) return 0;
                const unsigned char *x = (const unsigned char *)&first.voice[0];
                const unsigned char *y = (const unsigned char *)&added.voice[voice];
                for (unsigned i = 0; i < sizeof(first.voice[0]); ++i)
                    if (x[i] != y[i]) return 0;
                x = (const unsigned char *)first.filter;
                y = (const unsigned char *)added.filter;
                for (unsigned i = 0; i < sizeof(first.filter); ++i)
                    if (x[i] != y[i]) return 0;
            }
        }
    return 1;
}
#endif
