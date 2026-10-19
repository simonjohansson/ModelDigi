/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include "../dsp/dsp.h"

int main(void)
{
    int32_t samples[34];
    for (int i = 0; i < 34; i++)
        samples[i] = 12345;
    modeldigi_render(&samples[1]);
    assert(samples[0] == 12345 && samples[33] == 12345);
    for (int i = 1; i < 33; i++)
        assert(samples[i] == 0);
    return 0;
}
