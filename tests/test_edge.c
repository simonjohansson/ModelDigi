/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The engine's private edge helpers, checked directly. */
#include <assert.h>
#include <stdio.h>
#include "../dsp/oscillator.c"

static uint32_t next(uint32_t *x)
{
    *x ^= *x << 13; *x ^= *x >> 17; *x ^= *x << 5;
    return *x;
}

/* The edge fraction equals exact 64-bit division for every step size class. */
int main(void)
{
    uint32_t x = 12345, incs[64];
    unsigned n = 0;
    for (unsigned k = 0; k < 31; ++k) {
        incs[n++] = 1u << k;
        incs[n++] = (1u << k) + 1;
    }
    incs[n++] = 5592;          /* 1/16 Hz */
    incs[n++] = 1789569706;    /* 20000 Hz */
    for (unsigned t = 0; t < 2000000; ++t) {
        uint32_t inc = t < n * 1000 ? incs[t % n] : next(&x) % 1789569706u + 1;
        uint32_t d = t % 4 == 0 ? inc - 1 : t % 4 == 1 ? 0 : next(&x) % inc;
        assert(fraction(d, inc) == (uint32_t)(((uint64_t)d << 16) / inc));
    }
    puts("PASS: edge fraction matches exact division");
    return 0;
}
