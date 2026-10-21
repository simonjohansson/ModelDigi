/* SPDX-License-Identifier: GPL-2.0-or-later */
/* ColdFire cost driver: fixed scenarios for one oscillator bank, rendered 32 frames at a time. */
#include "../../dsp/oscillator.h"
#include "../../tests/pulse_routes.h"

int cost_verify_pulses(void) { return pulse_routes(); }

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
