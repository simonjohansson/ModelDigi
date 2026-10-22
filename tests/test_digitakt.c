/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "digitakt-mk1/core3.h"
#include "../machines/digitakt/controls.h"

volatile uint8_t fw_render_machine[8];
volatile int32_t fw_voice_note[8];
volatile uint32_t fw_voice_start;
volatile uint32_t fw_active_track;
uint8_t *volatile fw_kit;
const char cm_ui_v32[] = "UI32";
const char fw_bitmap_vt[] = "Bitmap";
static uint8_t bitmap[17][17];
void fw_fillrect(void *bmp, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t colour)
{
    assert(bmp == bitmap && x0 >= 1 && x1 <= 17 && y0 >= 0 && y1 <= 16);
    assert(x1 >= x0 && y1 >= y0 && (colour == 0 || colour == 1));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) bitmap[y][x - 1] = (uint8_t)colour;
}
extern const struct cm_machine modeldigi_machine;
extern void modeldigi_digitakt_render(int32_t *blocks);
extern int32_t modeldigi_midi_cc_parameter(int32_t track, int32_t machine, int32_t cc);
static int16_t live_values[8][MD_VALUES];

int32_t cm_src_value(int32_t track, int32_t parameter)
{
    assert(track >= 0 && track < 8 && parameter >= 0 && parameter < MD_VALUES);
    return live_values[track][parameter];
}

static const struct cm_ui32 *page(void)
{
    return (const struct cm_ui32 *)modeldigi_machine.ui;
}

static const struct cm_knob *knob(int value)
{
    return value < 8 ? &page()->base.knob[value] : &page()->extra[value - 8];
}

static void defaults(int16_t v[MD_VALUES])
{
    for (int k = 0; k < MD_VALUES; ++k)
        v[k] = (int16_t)knob(k)->def;
}

static double hz(uint32_t q16)
{
    return q16 / 65536.0;
}

static void near_cents(uint32_t actual, double expected)
{
    assert(fabs(1200 * log2(hz(actual) / expected)) < 0.05);
}

static void isolation(void)
{
    int32_t samples[8 * 32 + 2];
    for (int track = 0; track < 8; ++track) {
        defaults(live_values[track]);
        fw_voice_note[track] = 60 << 16;
    }
    for (int selected = -1; selected < 8; selected++) {
        for (int i = 0; i < 8 * 32 + 2; i++)
            samples[i] = 12345;
        for (int track = 0; track < 8; track++)
            fw_render_machine[track] = track == selected ? modeldigi_machine.id : 0;
        modeldigi_digitakt_render(&samples[1]);
        assert(samples[0] == 12345 && samples[8 * 32 + 1] == 12345);
        int audible = 0;
        for (int track = 0; track < 8; track++)
            for (int i = 0; i < 32; i++)
                if (track == selected) {
                    int32_t sample = samples[1 + 32 * track + i];
                    assert(sample > -1076291389 && sample < 1076291389);
                    audible |= sample != 0;
                } else {
                    assert(samples[1 + 32 * track + i] == 12345);
                }
        assert(audible == (selected >= 0));
    }
}

static void render_lifetime(void)
{
    int32_t samples[8 * 32] = {0}, expected[32];
    modeldigi_osc_state direct[2];
    for (int track = 0; track < 8; ++track) fw_render_machine[track] = 0;
    modeldigi_digitakt_render(samples);
    for (int track = 0; track < 2; ++track) {
        defaults(live_values[track]);
        live_values[track][MD_ANLG] = 0;   /* compare deterministic clean output */
        fw_render_machine[track] = 42;
        modeldigi_osc_init(&direct[track]);
        /* All three oscillators sound. */
        live_values[track][MD_LEV2] = live_values[track][MD_LEV3] = 127 << 8;
        live_values[track][MD_MOD] = live_values[track][MD_DPTH] = 127 << 8;
    }
    for (int block = 0; block < 12; ++block) {
        fw_voice_start = block & 1 ? 3 : 0;
        fw_voice_note[0] = (block < 4 ? 60 : 72) << 16;
        fw_voice_note[1] = 67 << 16;
        live_values[0][MD_WAV1] = (block < 6 ? 2 : 3) << 8;
        live_values[0][MD_LEV1] = (block == 8 || block == 9 ? 0 : 127) << 8;
        modeldigi_digitakt_render(samples);
        for (int track = 0; track < 2; ++track) {
            modeldigi_osc_params p;
            modeldigi_decode(live_values[track], fw_voice_note[track], &p);
            modeldigi_osc_render(&direct[track], &p, expected, 32);
            assert(!memcmp(samples + 32 * track, expected, sizeof expected));
        }
    }
    fw_render_machine[0] = 0;
    modeldigi_digitakt_render(samples);
    fw_render_machine[0] = 42;
    modeldigi_osc_init(&direct[0]);
    modeldigi_osc_params p;
    modeldigi_decode(live_values[0], fw_voice_note[0], &p);
    modeldigi_osc_render(&direct[0], &p, expected, 32);
    modeldigi_digitakt_render(samples);
    assert(!memcmp(samples, expected, sizeof expected));
}

static void descriptor(void)
{
    static const uint8_t shown[2][8] = {
        { MD_RNG1, MD_WAV1, MD_RNG2, MD_FRQ2, MD_WAV2, MD_RNG3, MD_FRQ3, MD_WAV3 },
        { MD_ANLG, MD_CTRL, MD_MOD, MD_DPTH, MD_LEV1, MD_LEV2, MD_LEV3, CM_SRC_BLANK },
    };
    static const int16_t def[MD_VALUES] = { 3, 2, 3, 64, 2, 3, 64, 2, 127, 127, 0, 0, 127, 0, 0 };
    assert(modeldigi_machine.id == 42 && modeldigi_machine.render == 42 && modeldigi_machine.params == 3);
    assert(page()->base.abi == cm_ui_v32 && page()->base.page_from == 3 && page()->page_count == 2);
    for (int p = 0; p < 2; ++p) {
        assert(!memcmp(page()->pages[p].parameter, shown[p], 8));
        assert(page()->pages[p].stock == 0);
    }
    for (int k = 0; k < MD_VALUES; ++k) {
        const struct cm_knob *n = knob(k);
        int selector = k < 8 && k != MD_FRQ2 && k != MD_FRQ3;
        assert((n->flags & (CM_RANGE | CM_DEFAULT)) == (CM_RANGE | CM_DEFAULT) && !(n->flags & CM_HIDDEN));
        assert(n->min == 0 && n->max == (selector ? 5 << 8 : 0x7f00) && n->def == def[k] << 8);
        assert(n->name && strlen(n->name) <= 4 && n->lname && n->look == (selector || k == MD_FRQ2 || k == MD_FRQ3 ? 0x84 : 0x86));
        int stock_dial = k == MD_FRQ2 || k == MD_FRQ3 || k == MD_DPTH;
        assert(!!(n->flags & CM_DRAW) == !stock_dial);
        assert(!!n->draw == !stock_dial);
        assert(!(n->flags & CM_NOT_SAMPLE) == (k != MD_FRQ2));   /* knob D */
    }
}

static void decoding(void)
{
    int16_t v[MD_VALUES];
    modeldigi_osc_params p;
    defaults(v);
    modeldigi_decode(v, 60 << 16, &p);
    for (int o = 0; o < 3; ++o)
        near_cents(p.frequency_q16_16[o], 261.6256);
    assert(p.waveform[0] == 3 && p.waveform[1] == 3 && p.waveform[2] == 3);
    assert(p.level[0] == 65535 && p.level[1] == 0 && p.level[2] == 0);
    assert(!p.modulation_enabled && p.modulation_depth_q16_16 == 0 && p.osc3_control && p.analog);

    /* Ranges and note. */
    static const double range_ratio[6] = { 0.0077792, 0.25, 0.5, 1, 2, 4 };
    for (int r = 0; r < 6; ++r) {
        v[MD_RNG1] = (int16_t)(r << 8);
        modeldigi_decode(v, 69 << 16, &p);
        near_cents(p.frequency_q16_16[0], 440 * range_ratio[r]);
    }
    defaults(v);
    v[MD_ANLG] = 63 << 8;
    modeldigi_decode(v, 60 << 16, &p);
    assert(!p.analog);
    near_cents(p.frequency_q16_16[0], 261.6256);

    /* FRQ: fine near unison, exact semitones, symmetric, interpolated between steps. */
    static const struct { int position; double cents; } frq[] = {
        { 64, 0 }, { 65, 0.23 }, { 67, 2.04 }, { 71, 11.11 }, { 85, 100 }, { 92, 200 }, { 113, 500 },
        { 127, 700 }, { 63, -0.23 }, { 43, -100 }, { 1, -700 }, { 0, -700 },
    };
    defaults(v);
    for (unsigned i = 0; i < sizeof frq / sizeof *frq; ++i) {
        v[MD_FRQ2] = (int16_t)(frq[i].position << 8);
        modeldigi_decode(v, 60 << 16, &p);
        near_cents(p.frequency_q16_16[1], 261.6256 * pow(2, frq[i].cents / 1200));
    }
    v[MD_FRQ2] = (int16_t)((84 << 8) + 128);
    modeldigi_decode(v, 60 << 16, &p);
    double between = 1200 * log2(hz(p.frequency_q16_16[1]) / 261.6256);
    assert(between > 90.7 && between < 100);

    /* OSC 3 CONTROL off: no note, C2, five-times sweep; at LO the frozen table. */
    defaults(v);
    v[MD_CTRL] = 63 << 8;
    modeldigi_decode(v, 84 << 16, &p);
    assert(!p.osc3_control);
    near_cents(p.frequency_q16_16[2], 65.40639);
    near_cents(p.frequency_q16_16[0], 1046.502);
    v[MD_FRQ3] = 127 << 8;
    modeldigi_decode(v, 84 << 16, &p);
    near_cents(p.frequency_q16_16[2], 65.40639 * pow(2, 3500 / 1200.0));
    v[MD_RNG3] = 0;
    static const struct { int position; double hz; } lo[] = { { 64, 0.5121456 }, { 1, 0.0685426 }, { 127, 3.8529 } };
    for (unsigned i = 0; i < 3; ++i) {
        v[MD_FRQ3] = (int16_t)(lo[i].position << 8);
        modeldigi_decode(v, 84 << 16, &p);
        assert(fabs(hz(p.frequency_q16_16[2]) / lo[i].hz - 1) < 0.002);
    }

    /* Selectors and switches round; waveforms map per oscillator. */
    defaults(v);
    v[MD_WAV1] = (1 << 8) + 127;
    v[MD_WAV3] = (1 << 8) + 129;
    v[MD_MOD] = 64 << 8;
    modeldigi_decode(v, 60 << 16, &p);
    assert(p.waveform[0] == 1 && p.waveform[2] == 3 && p.modulation_enabled);
    v[MD_WAV3] = 1 << 8;
    modeldigi_decode(v, 60 << 16, &p);
    assert(p.waveform[2] == 2);

    /* Depth follows the recorded amount points; levels are square law. */
    static const struct { int position; double octaves; } depth[] = { { 0, 0 }, { 64, 0.4479 }, { 127, 1.50897 } };
    for (unsigned i = 0; i < 3; ++i) {
        v[MD_DPTH] = (int16_t)(depth[i].position << 8);
        modeldigi_decode(v, 60 << 16, &p);
        assert(fabs(p.modulation_depth_q16_16 / 65536.0 - depth[i].octaves) < 0.001);
    }
    v[MD_LEV2] = 64 << 8;
    v[MD_LEV3] = 1 << 8;
    modeldigi_decode(v, 60 << 16, &p);
    assert(abs(p.level[1] - 16644) <= 1 && p.level[2] == 4);
}

static void text(void)
{
    char buf[16];
    static const struct { int kind; int32_t value; int popup; const char *text; } cases[] = {
        { MD_TEXT_RANGE, 0, 0, "LO" }, { MD_TEXT_RANGE, 3 << 8, 1, "8'" },
        { MD_TEXT_WAVE12, 1 << 8, 0, "TRSW" }, { MD_TEXT_WAVE3, 1 << 8, 1, "REVERSE SAW" },
        { MD_TEXT_FRQ, 64 << 8, 0, "0.0" }, { MD_TEXT_FRQ, 67 << 8, 0, "+2.0" },
        { MD_TEXT_FRQ, 67 << 8, 1, "+2.04c" }, { MD_TEXT_FRQ, 127 << 8, 0, "+700" },
        { MD_TEXT_FRQ_WIDE, 1 << 8, 1, "-3500c" }, { MD_TEXT_FRQ, 127 << 8, 1, "+700.0c" },
        { MD_TEXT_SWITCH, 63 << 8, 0, "OFF" }, { MD_TEXT_SWITCH, 64 << 8, 0, "ON" },
        { MD_TEXT_DEPTH, 127 << 8, 1, "1.508 OCT" }, { MD_TEXT_DEPTH, 0, 0, "0.00" },
    };
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; ++i) {
        assert(modeldigi_text(cases[i].kind, cases[i].value, cases[i].popup, buf));
        assert(!strcmp(buf, cases[i].text) && strlen(buf) <= 15);
    }
    /* FRQ3's text widens with CONTROL off on the active track's sound. */
    static uint8_t kit[0x20 + 0xa2 * 8];
    const struct cm_knob *frq3 = knob(MD_FRQ3);
    fw_kit = kit;
    fw_active_track = 2;
    uint8_t *control = FW_SOUND(kit, 2) + 0x14 + 2 * 47;
    control[0] = 127;
    assert(frq3->fmt(buf, 127 << 8, 0, 42) && !strcmp(buf, "+700"));
    control[0] = 0;
    assert(frq3->fmt(buf, 127 << 8, 0, 42) && !strcmp(buf, "+3500"));
    fw_kit = 0;
}

static void graphics(void)
{
    static const int32_t values[] = {INT32_MIN, -32768, 0, 128, 256, 512, 768, 1024, 1280,
                                     63 << 8, 64 << 8, 127 << 8, 32767, INT32_MAX};
    uint8_t images[6][17][17];
    for (int k = 0; k < MD_VALUES; ++k) {
        if (!(knob(k)->flags & CM_DRAW)) continue;
        for (unsigned v = 0; v < sizeof values / sizeof *values; ++v) {
            memset(bitmap, 0xa5, sizeof bitmap);
            assert(knob(k)->draw(bitmap, 0, 0, values[v], 0));
            uint8_t before[17][17]; memcpy(before, bitmap, sizeof bitmap);
            assert(knob(k)->draw(bitmap, 0, 0, values[v], 1));
            assert(!memcmp(before, bitmap, sizeof bitmap));
            int lit = 0;
            for (int y = 0; y < 17; ++y)
                for (int x = 0; x < 17; ++x) { assert(bitmap[y][x] <= 1); lit += bitmap[y][x]; }
            assert(lit > 0 && lit < 17 * 17);
        }
    }
    for (int i = 0; i < 6; ++i) {
        knob(MD_WAV1)->draw(bitmap, 0, 0, i << 8, 0);
        memcpy(images[i], bitmap, sizeof bitmap);
        for (int j = 0; j < i; ++j) assert(memcmp(images[j], bitmap, sizeof bitmap));
        knob(MD_WAV3)->draw(bitmap, 0, 0, i << 8, 0);
        assert((memcmp(images[i], bitmap, sizeof bitmap) != 0) == (i == 1));
    }
    /* Native bitmap y runs bottom-up; match the panel's upward-pointing glyphs. */
    for (int i = 0; i < 2; ++i)
        assert(images[i][13][8] && !images[i][3][8]);
    assert(images[2][3][1] && !images[2][13][1]);
    knob(MD_WAV3)->draw(bitmap, 0, 0, 1 << 8, 0);
    assert(bitmap[13][1] && !bitmap[3][1]);
    knob(MD_CTRL)->draw(bitmap, 0, 0, 63 << 8, 0); memcpy(images[0], bitmap, sizeof bitmap);
    knob(MD_CTRL)->draw(bitmap, 0, 0, 64 << 8, 0); assert(memcmp(images[0], bitmap, sizeof bitmap));
    assert(modeldigi_machine.icon);
}

static void midi_lookup(void)
{
    for (int track = -1; track <= 16; ++track)
        for (int cc = 0; cc < 128; ++cc) {
            int expected = track >= 0 && track < 8 && cc >= 24 && cc <= 30 ? 164 + cc - 24 : 0;
            assert(modeldigi_midi_cc_parameter(track, 42, cc) == expected);
            assert(modeldigi_midi_cc_parameter(track, 3, cc) == 0);
        }
}

static void coarse_motion(void)
{
    for (int wide = 0; wide <= 1; wide++) {
        int remainder = 0, steps = 0, last = 0;
        for (int detent = 1; detent <= 128; ++detent) {
            int n = modeldigi_frq_coarse(&remainder, 4, wide);
            assert(n == 0 || n == 1);
            if (n) {
                int gap = detent-last;
                assert(wide ? gap == 1 || gap == 2 : gap == 9 || gap == 10);
                last = detent;
            }
            steps += n;
        }
        assert(steps == (wide ? 70 : 14) && remainder == 0);
        assert(modeldigi_frq_coarse(&remainder, 512, wide) == steps && remainder == 0);
    }
}

static void tuning_steps(void)
{
    for (int wide = 0; wide <= 1; wide++) {
        int limit = wide ? 35 : 7;
        int32_t value = 64 << 8;
        for (int n = 1; n <= limit; n++) {
            int32_t next = modeldigi_frq_step(value, 1, wide);
            assert(next > value && modeldigi_frq_anchor(next, wide));
            assert(abs((wide ? 5 : 1) * modeldigi_frq_cents(next) - 10000 * n) <= 15);
            assert(modeldigi_frq_step(next - 1, 1, wide) == next);
            if (next < (127 << 8)) assert(modeldigi_frq_step(next + 1, -1, wide) == next);
            value = next;
        }
        assert(modeldigi_frq_step(value, 1, wide) == value);
        for (int n = limit - 1; n >= -limit; n--) {
            value = modeldigi_frq_step(value, -1, wide);
            assert(abs((wide ? 5 : 1) * modeldigi_frq_cents(value) - 10000 * n) <= 15);
        }
        assert(value == (1 << 8));
        assert(modeldigi_frq_step(0, 1, wide) == modeldigi_frq_step(value, 1, wide));
        assert(modeldigi_frq_step(value, -1, wide) == value);
        for (int v = 256; v < 127 * 256; v++) {
            assert(modeldigi_frq_step(v, 1, wide) > v);
            assert(modeldigi_frq_step(v, -1, wide) < v || v == 256);
        }
    }
}

int main(void)
{
    coarse_motion();
    tuning_steps();
    midi_lookup();
    graphics();
    isolation();
    render_lifetime();
    descriptor();
    decoding();
    text();
    return 0;
}
