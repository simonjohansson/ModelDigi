/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "graphics.h"

static int position(int32_t value, int max)
{
    if (value <= 0) return 0;
    if (value >= max * 256) return max;
    return (value + 128) >> 8;
}

static void clear(uint32_t rows[17])
{
    for (int i = 0; i < 17; ++i) rows[i] = 0;
}

static void pixel(uint32_t rows[17], int x, int y)
{
    rows[y] |= 1u << (16 - x);
}

static void line(uint32_t rows[17], int x, int y, int end_x, int end_y)
{
    int dx = end_x > x ? end_x - x : x - end_x, sx = x < end_x ? 1 : -1;
    int dy = end_y > y ? y - end_y : end_y - y, sy = y < end_y ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        pixel(rows, x, y);
        if (x == end_x && y == end_y) break;
        int twice = 2 * error;
        if (twice >= dy) { error += dy; x += sx; }
        if (twice <= dx) { error += dx; y += sy; }
    }
}

static void box(uint32_t rows[17], int left, int top, int right, int bottom)
{
    line(rows, left, top, right, top); line(rows, left, bottom, right, bottom);
    line(rows, left, top, left, bottom); line(rows, right, top, right, bottom);
}

/* As for native knob graphics, rows run top-first while bitmap y runs bottom-up. */
static int32_t draw(void *bmp, int32_t x, int32_t y, const uint32_t rows[17])
{
    fw_fillrect(bmp, x + 1, y, x + 17, y + 16, 0);
    for (int r = 0; r < 17; ++r)
        for (int c = 0; c < 17;) {
            if (!(rows[r] & (1u << (16 - c)))) { ++c; continue; }
            int first = c++;
            while (c < 17 && (rows[r] & (1u << (16 - c)))) ++c;
            fw_fillrect(bmp, x + 1 + first, y + 16 - r, x + c, y + 16 - r, 1);
        }
    return 1;
}

static int32_t wave(void *bmp, int32_t x, int32_t y, int shape)
{
    uint32_t rows[17];
    clear(rows);
    if (shape == 0 || shape == 1) {
        line(rows, 1, 13, 8, 3);
        line(rows, 8, 3, 15, shape == 0 ? 13 : 8);
        if (shape == 1) line(rows, 15, 8, 15, 13);
    } else if (shape < 4) {
        int start = shape == 2 ? 3 : 13, end = 16 - start;
        line(rows, 1, start, 15, end); line(rows, 15, end, 15, start);
    } else {
        int edge = shape == 4 ? 8 : shape == 5 ? 6 : 4;
        line(rows, 1, 12, 1, 4); line(rows, 1, 4, edge, 4);
        line(rows, edge, 4, edge, 12); line(rows, edge, 12, 15, 12);
        line(rows, 15, 12, 15, 4);
    }
    return draw(bmp, x, y, rows);
}

int32_t modeldigi_draw_wave12(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    static const uint8_t shapes[] = {0, 1, 3, 4, 5, 6};
    (void)flag;
    return wave(bmp, x, y, shapes[position(value, 5)]);
}

int32_t modeldigi_draw_wave3(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    static const uint8_t shapes[] = {0, 2, 3, 4, 5, 6};
    (void)flag;
    return wave(bmp, x, y, shapes[position(value, 5)]);
}

int32_t modeldigi_draw_range(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    static const char *const labels[] = {"LO", "32'", "16'", "8'", "4'", "2'"};
    static const char letters[] = "123468LO'";
    static const uint16_t glyphs[] = {0x2c97, 0x62a7, 0x628e, 0x5bc9, 0x39ef, 0x7bef, 0x4927, 0x7b6f, 0x2400};
    const char *text = labels[position(value, 5)];
    int length = text[2] ? 3 : 2;
    uint32_t rows[17];
    clear(rows);
    (void)flag;
    box(rows, 0, 4, 16, 12);
    for (int c = 0; c < length; ++c) {
        int k = 0;
        while (letters[k] != text[c]) ++k;
        for (int r = 0; r < 5; ++r)
            for (int col = 0; col < 3; ++col)
                if (glyphs[k] & (1u << (14 - 3 * r - col)))
                    pixel(rows, (17 - (4 * length - 1)) / 2 + 4 * c + col, 6 + r);
    }
    return draw(bmp, x, y, rows);
}

int32_t modeldigi_draw_switch(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    uint32_t rows[17];
    clear(rows);
    int left = value >= (64 << 8) ? 9 : 2;
    (void)flag;
    box(rows, 1, 5, 15, 11);
    for (int r = 6; r <= 10; ++r) line(rows, left, r, left + 5, r);
    return draw(bmp, x, y, rows);
}

int32_t modeldigi_draw_level(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    uint32_t rows[17];
    clear(rows);
    int height = (position(value, 127) * 13 + 63) / 127;
    (void)flag;
    box(rows, 4, 0, 12, 16);
    for (int r = 15; r > 15 - height; --r) line(rows, 6, r, 10, r);
    return draw(bmp, x, y, rows);
}

static const uint32_t icon_pixels[11] = {
    0x42000000,0x24000000,0x18000000,0x24000000,0x42000000,0,
    0x7e000000,0x40000000,0x40000000,0x40000000,0x7e000000
};
static const uint32_t icon_mask[11] = {
    0xfe000000,0xfe000000,0xfe000000,0xfe000000,0xfe000000,0xfe000000,
    0xfe000000,0xfe000000,0xfe000000,0xfe000000,0xfe000000
};
const struct modeldigi_icon modeldigi_icon = {fw_bitmap_vt, 11, 7, 1, icon_pixels, icon_mask, 0};
