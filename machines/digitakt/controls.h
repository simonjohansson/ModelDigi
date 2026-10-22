/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MODELDIGI_CONTROLS_H
#define MODELDIGI_CONTROLS_H

#include <stdint.h>
#include "../../dsp/oscillator.h"

/* The fifteen stored values, in storage order; page 1 shows 0-7, page 2 8-14. */
enum {
    MD_RNG1, MD_WAV1, MD_RNG2, MD_FRQ2, MD_WAV2, MD_RNG3, MD_FRQ3, MD_WAV3,
    MD_ANLG, MD_CTRL, MD_MOD, MD_DPTH, MD_LEV1, MD_LEV2, MD_LEV3, MD_VALUES
};

/* Value-text kinds for the SRC page. */
enum {
    MD_TEXT_RANGE, MD_TEXT_WAVE12, MD_TEXT_WAVE3, MD_TEXT_FRQ, MD_TEXT_FRQ_WIDE,
    MD_TEXT_SWITCH, MD_TEXT_DEPTH
};

/* values: 8.8 after locks and LFOs; note: the trig's note, 16.16. 32-bit arithmetic only. */
void modeldigi_decode(const int16_t values[MD_VALUES], int32_t note, modeldigi_osc_params *params);
/* Writes a value's text (popup: about 7 characters beside the long name, else about 5); returns 1. */
int modeldigi_text(int kind, int32_t value, int popup, char *buf);

/* Whether a stored sound's CONTROL is off, widening FRQ3. The editor follows the stored value,
 * not the one after locks and LFOs that the sound uses. */
int modeldigi_osc3_free(const uint8_t *sound);

/* Private device tuning helpers; values remain in the existing 8.8 format. */
int32_t modeldigi_frq_cents(int32_t value);
int32_t modeldigi_frq_step(int32_t value, int direction, int wide);
int modeldigi_frq_coarse(int *remainder, int counts, int wide);
int modeldigi_frq_anchor(int32_t value, int wide);

#endif
