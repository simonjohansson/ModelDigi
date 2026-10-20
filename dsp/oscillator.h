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

/* Private scratch layout: not a sound format or a stable ABI. */
struct modeldigi_osc_voice {
    uint64_t phase, increment; /* Q0.64 cycles, low 16 bits zero */
    int32_t previous, held, slope[2];
    uint16_t level;
    uint8_t waveform;
    bool primed;
};

struct modeldigi_osc_filter {
    int32_t state[2][4]; /* each allpass branch's section memories */
};

struct modeldigi_osc_factor {
    uint32_t t;
    int32_t whole;
};

struct modeldigi_osc_workspace {
    int32_t mix[64];
    struct modeldigi_osc_factor mod[64];
};

typedef struct {
    struct modeldigi_osc_voice voice[3];
    struct modeldigi_osc_filter filter[4];
    /* Transient working memory, owned by this bank and overwritten before use. */
    struct modeldigi_osc_workspace workspace;
} modeldigi_osc_state;

/* Initializes persistent state; transient workspace does not need initialization. */
void modeldigi_osc_init(modeldigi_osc_state *state);
/* 48 kHz mono Q31 with fixed 1/16 bank gain; no firmware dependencies.
 * Output may be NULL for zero frames, which changes nothing. State/params
 * must be valid. Parameters are constant per call; callers own control ramps.
 * Bounds clamp deterministically, invalid shapes become triangle. Clamped
 * instantaneous-frequency trajectories are outside the fidelity domain. */
void modeldigi_osc_render(modeldigi_osc_state *state,
                         const modeldigi_osc_params *params,
                         int32_t *output, size_t frames);

#endif
