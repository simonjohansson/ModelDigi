/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "digitakt-mk1/core3.h"
#include "controls.h"
#include "graphics.h"

#define MODELDIGI_ID 42
/* BR steps in whole units; the range selectors borrow TUNE's native fine stepping. */
#define LOOK_BR      0x86
#define LOOK_TUNE    0x84

int32_t modeldigi_midi_cc_parameter(int32_t track, int32_t machine, int32_t cc)
{
    if (track < 0 || track >= 8 || machine != MODELDIGI_ID)
        return 0;
    return cc >= 24 && cc <= 30 ? 164 + cc - 24 : 0;
}

void modeldigi_digitakt_render(int32_t *blocks)
{
    static modeldigi_osc_state banks[8];
    static uint8_t active[8];
    for (int track = 0; track < 8; track++) {
        if (fw_render_machine[track] != MODELDIGI_ID) {
            active[track] = 0;
            continue;
        }
        if (!active[track]) {
            modeldigi_osc_init(&banks[track]);
            active[track] = 1;
        }
        int16_t values[MD_VALUES];
        modeldigi_osc_params params;
        for (int k = 0; k < MD_VALUES; ++k)
            values[k] = (int16_t)cm_src_value(track, k);
        modeldigi_decode(values, fw_voice_note[track], &params);
        /* shortcut: one audible oscillator for the measured device trial; enable the bank after qualification. */
        params.level[1] = params.level[2] = 0;
        params.modulation_enabled = false;
        modeldigi_osc_render(&banks[track], &params, &blocks[32 * track], 32);
    }
}

/* Oscillator 3's CONTROL on the active track's sound (slot 47), for FRQ3's wide text. */
static int osc3_free(void)
{
    const uint8_t *sound = fw_kit ? FW_SOUND(fw_kit, fw_active_track) : 0;
    return sound && (int16_t)(sound[0x14 + 2 * 47] << 8 | sound[0x15 + 2 * 47]) < (64 << 8);
}

#define TEXT(fn, kind) \
    static int32_t fn(char *buf, int32_t value, int32_t ctx, int32_t machine) \
    { (void)machine; return modeldigi_text(kind, value, ctx, buf); }
TEXT(range_text, MD_TEXT_RANGE)
TEXT(wave12_text, MD_TEXT_WAVE12)
TEXT(wave3_text, MD_TEXT_WAVE3)
TEXT(frq2_text, MD_TEXT_FRQ)
TEXT(tune_text, MD_TEXT_TUNE)
TEXT(switch_text, MD_TEXT_SWITCH)
TEXT(depth_text, MD_TEXT_DEPTH)

static int32_t frq3_text(char *buf, int32_t value, int32_t ctx, int32_t machine)
{
    (void)machine;
    return modeldigi_text(osc3_free() ? MD_TEXT_FRQ_WIDE : MD_TEXT_FRQ, value, ctx, buf);
}

#define KNOB(label, long_name, max_, def_, flags_, fmt_, draw_) \
    { .name = label, .lname = long_name, .look = LOOK_BR, .flags = CM_RANGE | CM_DEFAULT | (flags_), \
      .min = 0, .max = (max_) << 8, .def = (def_) << 8, .fmt = fmt_, .draw = draw_ }
#define SELECTOR(label, long_name, def_, fmt_, draw_) \
    KNOB(label, long_name, 5, def_, CM_DRAW, fmt_, draw_)
#define RANGE(label, long_name) \
    { .name = label, .lname = long_name, .look = LOOK_TUNE, \
      .flags = CM_RANGE | CM_DEFAULT | CM_DRAW, .min = 0, .max = 5 << 8, .def = 3 << 8, \
      .fmt = range_text, .draw = modeldigi_draw_range }

static const struct cm_src_page pages[] = {
    { .title = "OSC 1/2", .parameter = { MD_RNG1, MD_WAV1, MD_RNG2, MD_FRQ2, MD_WAV2, MD_RNG3, MD_FRQ3, MD_WAV3 } },
    { .title = "MIX 2/2", .parameter = { MD_TUNE, MD_CTRL, MD_MOD, MD_DPTH, MD_LEV1, MD_LEV2, MD_LEV3, CM_SRC_BLANK } },
};

static const struct cm_ui32 modeldigi_page = {
    .base = {
        .abi = cm_ui_v32,
        .page_from = 3,
        .group = "ModelDigi",
        .knob = {
            RANGE("RNG1", "Osc 1 Range"),
            SELECTOR("WAV1", "Osc 1 Wave", 2, wave12_text, modeldigi_draw_wave12),
            RANGE("RNG2", "Osc 2 Range"),
            KNOB("FRQ2", "Osc 2 Frequency", 127, 64, CM_NOT_SAMPLE, frq2_text, 0),
            SELECTOR("WAV2", "Osc 2 Wave", 2, wave12_text, modeldigi_draw_wave12),
            RANGE("RNG3", "Osc 3 Range"),
            KNOB("FRQ3", "Osc 3 Frequency", 127, 64, 0, frq3_text, 0),
            SELECTOR("WAV3", "Osc 3 Wave", 2, wave3_text, modeldigi_draw_wave3),
        },
    },
    .extra = {
        KNOB("TUNE", "Tune", 127, 64, 0, tune_text, 0),
        KNOB("CTRL", "Osc 3 Control", 127, 127, CM_DRAW, switch_text, modeldigi_draw_switch),
        KNOB("MOD", "Modulation", 127, 0, CM_DRAW, switch_text, modeldigi_draw_switch),
        KNOB("DPTH", "Mod Depth", 127, 0, 0, depth_text, 0),
        KNOB("LEV1", "Osc 1 Level", 127, 127, CM_DRAW, 0, modeldigi_draw_level),
        KNOB("LEV2", "Osc 2 Level", 127, 0, CM_DRAW, 0, modeldigi_draw_level),
        KNOB("LEV3", "Osc 3 Level", 127, 0, CM_DRAW, 0, modeldigi_draw_level),
    },
    .page_count = 2,
    .pages = pages,
};

const struct cm_machine modeldigi_machine = {
    .id = MODELDIGI_ID,
    .name = "ModelDigi",
    .sname = "MDIG",
    .icon = &modeldigi_icon,
    .params = 3,
    .render = MODELDIGI_ID,
    .tag = CM_UI_TAG,
    .ui = &modeldigi_page.base,
};
