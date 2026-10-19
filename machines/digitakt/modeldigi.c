/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "digitakt-mk1/core3.h"
#include "../../dsp/dsp.h"

/* shortcut: experimental ID, allocate a permanent ID before publishing sounds. */
#define MODELDIGI_ID 121

void modeldigi_digitakt_render(int32_t *blocks)
{
    for (int track = 0; track < 8; track++)
        if (fw_render_machine[track] == MODELDIGI_ID)
            modeldigi_render(&blocks[32 * track]);
}

static const struct cm_ui modeldigi_page = {
    .abi = cm_ui_v3,
    .page_from = 3,
    .group = "ModelDigi",
    .knob = {
        { .flags = CM_HIDDEN }, { .flags = CM_HIDDEN },
        { .flags = CM_HIDDEN }, { .flags = CM_HIDDEN },
        { .flags = CM_HIDDEN }, { .flags = CM_HIDDEN },
        { .flags = CM_HIDDEN }, { .flags = CM_HIDDEN },
    },
};

const struct cm_machine modeldigi_machine = {
    .id = MODELDIGI_ID,
    .name = "ModelDigi",
    .sname = "MDIG",
    .params = 3,
    .render = MODELDIGI_ID,
    .tag = CM_UI_TAG,
    .ui = &modeldigi_page,
};
