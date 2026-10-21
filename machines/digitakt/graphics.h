/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MODELDIGI_GRAPHICS_H
#define MODELDIGI_GRAPHICS_H
#include "digitakt-mk1/core3.h"

int32_t modeldigi_draw_range(void *, int32_t, int32_t, int32_t, int32_t);
int32_t modeldigi_draw_wave12(void *, int32_t, int32_t, int32_t, int32_t);
int32_t modeldigi_draw_wave3(void *, int32_t, int32_t, int32_t, int32_t);
int32_t modeldigi_draw_switch(void *, int32_t, int32_t, int32_t, int32_t);
int32_t modeldigi_draw_level(void *, int32_t, int32_t, int32_t, int32_t);
extern const struct modeldigi_icon {
    const void *vt;
    int32_t width, height, one;
    const uint32_t *px, *mask;
    int32_t zero;
} modeldigi_icon;
#endif
