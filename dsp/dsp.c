/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "dsp.h"

void modeldigi_render(int32_t block[32])
{
    for (int i = 0; i < 32; i++)
        block[i] = 0;
}
