/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include "digitakt-mk1/core3.h"

volatile uint8_t fw_render_machine[8];
const char cm_ui_v3[] = "UI30";
extern const struct cm_machine modeldigi_machine;
extern void modeldigi_digitakt_render(int32_t *blocks);

int main(void)
{
    int32_t samples[8 * 32 + 2];
    for (int selected = -1; selected < 8; selected++) {
        for (int i = 0; i < 8 * 32 + 2; i++)
            samples[i] = 12345;
        for (int track = 0; track < 8; track++)
            fw_render_machine[track] = track == selected ? modeldigi_machine.id : 0;
        modeldigi_digitakt_render(&samples[1]);
        assert(samples[0] == 12345 && samples[8 * 32 + 1] == 12345);
        for (int track = 0; track < 8; track++)
            for (int i = 0; i < 32; i++)
                assert(samples[1 + 32 * track + i] == (track == selected ? 0 : 12345));
    }
    assert(modeldigi_machine.id == 121);
    assert(modeldigi_machine.render == modeldigi_machine.id);
    for (int knob = 0; knob < 8; knob++)
        assert(modeldigi_machine.ui->knob[knob].flags == CM_HIDDEN);
    return 0;
}
