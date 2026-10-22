/* SPDX-License-Identifier: GPL-2.0-or-later */
/* ColdFire cost driver: fixed scenarios for one oscillator bank, rendered 32 frames at a time. */
#include "../../dsp/oscillator.h"

modeldigi_osc_state cost_state;
modeldigi_osc_params cost_params;
int32_t cost_output[32];

#define HZ(x) ((uint32_t)((x) * 65536.0))

/* 0 one saw; 1 three sources; 2 slow modulation; 3 audio-rate feedback FM; 4 high narrow pulses;
 * 5 saw feedback FM. */
void cost_setup(int scenario)
{
    modeldigi_osc_params p = {0};
    p.frequency_q16_16[0] = HZ(220);
    p.frequency_q16_16[1] = HZ(330.5);
    p.frequency_q16_16[2] = HZ(441.25);
    p.waveform[0] = 3;
    p.waveform[1] = 4;
    p.waveform[2] = 6;
    p.level[0] = 65535;
    if (scenario >= 1)
        p.level[1] = p.level[2] = 40000;
    if (scenario == 2) {
        p.frequency_q16_16[2] = HZ(5.5);
        p.waveform[2] = 0;
        p.modulation_enabled = true;
        p.modulation_depth_q16_16 = 32768;
    }
    if (scenario == 3 || scenario == 5) {
        p.frequency_q16_16[2] = HZ(200);
        p.waveform[2] = scenario == 3 ? 4 : 3;
        p.modulation_enabled = p.osc3_control = true;
        p.modulation_depth_q16_16 = 65536;
    }
    if (scenario == 4) {
        p.frequency_q16_16[0] = HZ(4186);
        p.frequency_q16_16[1] = HZ(6271);
        p.frequency_q16_16[2] = HZ(8372);
        p.waveform[0] = p.waveform[1] = p.waveform[2] = 6;
        p.modulation_enabled = true;
        p.modulation_depth_q16_16 = 16384;
    }
    cost_params = p;
    modeldigi_osc_init(&cost_state);
}

void cost_block(void)
{
    modeldigi_osc_render(&cost_state, &cost_params, cost_output, 32);
}

/* Random parameters for equivalence checks: every shape plus an invalid one, frequencies from below
 * 1/16 Hz to above 20 kHz on a rough log scale, any level and depth, both switches. */
void cost_random(uint32_t seed)
{
    uint32_t x = seed * 2654435761u + 1;
#define NEXT (x ^= x << 13, x ^= x >> 17, x ^= x << 5, x)
    modeldigi_osc_params p = {0};
    for (unsigned v = 0; v < 3; ++v) {
        unsigned octave = NEXT % 20;
        uint32_t base = 1u << (11 + octave);
        p.frequency_q16_16[v] = base + (NEXT & (base - 1));
        p.waveform[v] = (uint8_t)(NEXT % 8);
        p.level[v] = (uint16_t)NEXT;
    }
    p.modulation_enabled = NEXT & 1;
    p.osc3_control = NEXT & 1;
    p.modulation_depth_q16_16 = NEXT % 160000;
#undef NEXT
    cost_params = p;
    modeldigi_osc_init(&cost_state);
}
