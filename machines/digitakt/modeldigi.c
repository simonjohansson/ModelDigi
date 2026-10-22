/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "digitakt-mk1/core3.h"
#include "controls.h"
#include "graphics.h"

#define MODELDIGI_ID 42
/* BR steps in whole units; selectors and FRQ borrow TUNE's fine stepping. */
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
    /* Decoding costs about 2% of the device's render period, so it reruns only when a track's
     * values or note change. */
    static struct {
        int16_t values[MD_VALUES];
        int32_t note;
        modeldigi_osc_params params;
    } decoded[8];
    for (int track = 0; track < 8; track++) {
        if (fw_render_machine[track] != MODELDIGI_ID) {
            active[track] = 0;
            continue;
        }
        int32_t note = fw_voice_note[track];
        int changed = !active[track] || note != decoded[track].note;
        if (!active[track]) {
            modeldigi_osc_init(&banks[track]);
            active[track] = 1;
        }
        for (int k = 0; k < MD_VALUES; ++k) {
            int16_t value = (int16_t)cm_src_value(track, k);
            changed |= value != decoded[track].values[k];
            decoded[track].values[k] = value;
        }
        modeldigi_osc_params *params = &decoded[track].params;
        if (changed) {
            decoded[track].note = note;
            modeldigi_decode(decoded[track].values, note, params);
        }
        modeldigi_osc_render(&banks[track], params, &blocks[32 * track], 32);
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
#define FREQUENCY(label, long_name, flags_, fmt_) \
    { .name = label, .lname = long_name, .look = LOOK_TUNE, \
      .flags = CM_RANGE | CM_DEFAULT | (flags_), .min = 0, .max = 127 << 8, \
      .def = 64 << 8, .fmt = fmt_ }
#define SELECTOR(label, long_name, def_, fmt_, draw_) \
    { .name = label, .lname = long_name, .look = LOOK_TUNE, \
      .flags = CM_RANGE | CM_DEFAULT | CM_DRAW, .min = 0, .max = 5 << 8, .def = (def_) << 8, \
      .fmt = fmt_, .draw = draw_ }

static const struct cm_src_page pages[] = {
    { .title = "OSC 1/2", .parameter = { MD_RNG1, MD_WAV1, MD_RNG2, MD_FRQ2, MD_WAV2, MD_RNG3, MD_FRQ3, MD_WAV3 } },
    { .title = "MIX 2/2", .parameter = { MD_ANLG, MD_CTRL, MD_MOD, MD_DPTH, MD_LEV1, MD_LEV2, MD_LEV3, CM_SRC_BLANK } },
};

static const struct cm_ui32 modeldigi_page = {
    .base = {
        .abi = cm_ui_v32,
        .page_from = 3,
        .group = "ModelDigi",
        .knob = {
            SELECTOR("RNG1", "Osc 1 Range", 3, range_text, modeldigi_draw_range),
            SELECTOR("WAV1", "Osc 1 Wave", 2, wave12_text, modeldigi_draw_wave12),
            SELECTOR("RNG2", "Osc 2 Range", 3, range_text, modeldigi_draw_range),
            FREQUENCY("FRQ2", "Osc 2 Frequency", CM_NOT_SAMPLE, frq2_text),
            SELECTOR("WAV2", "Osc 2 Wave", 2, wave12_text, modeldigi_draw_wave12),
            SELECTOR("RNG3", "Osc 3 Range", 3, range_text, modeldigi_draw_range),
            FREQUENCY("FRQ3", "Osc 3 Frequency", 0, frq3_text),
            SELECTOR("WAV3", "Osc 3 Wave", 2, wave3_text, modeldigi_draw_wave3),
        },
    },
    .extra = {
        KNOB("ANLG", "Analog", 127, 127, CM_DRAW, switch_text, modeldigi_draw_switch),
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
