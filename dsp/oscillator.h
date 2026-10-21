/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MODELDIGI_OSCILLATOR_H
#define MODELDIGI_OSCILLATOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t frequency_q16_16[3]; /* Hz * 65536, bounded to 1/16..20000 Hz */
    uint8_t waveform[3]; /* triangle, combination, reverse saw, saw, square, wide, narrow */
    uint16_t level[3]; /* exact multiplier level / 65535 */
    uint32_t modulation_depth_q16_16; /* 0..2 octaves * 65536, not recorded amount */
    bool modulation_enabled;
    bool osc3_control;
} modeldigi_osc_params;

/* Private layout: not a sound format or a stable ABI. */
typedef struct {
    uint32_t phase[3]; /* Q0.32 cycles */
} modeldigi_osc_state;

/* Initializes persistent state. */
void modeldigi_osc_init(modeldigi_osc_state *state);
/* 48 kHz mono Q31 with fixed 1/16 bank gain; no firmware dependencies.
 * Output may be NULL for zero frames, which changes nothing. State/params
 * must be valid. Parameters are constant per call. Frequencies clamp to the
 * bounds above, invalid shapes become triangle. Modulation is not implemented:
 * its parameters are ignored. */
void modeldigi_osc_render(modeldigi_osc_state *state,
                         const modeldigi_osc_params *params,
                         int32_t *output, size_t frames);

#endif
