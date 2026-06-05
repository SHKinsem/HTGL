/* vendored from /engine — keep in sync with engine/draw.c */
#include "htgl_internal.h"
#include "font8x8_basic.h"

void htgl_fill_rect(uint16_t *band, int band_w, int band_y0, int band_h,
                    int rx, int ry, int rw, int rh, uint16_t color) {
    int x0 = rx, x1 = rx + rw;
    int y0 = ry, y1 = ry + rh;
    if (x0 < 0) x0 = 0;
    if (y0 < band_y0) y0 = band_y0;
    if (x1 > band_w) x1 = band_w;
    if (y1 > band_y0 + band_h) y1 = band_y0 + band_h;
    for (int y = y0; y < y1; y++) {
        uint16_t *row = band + (y - band_y0) * band_w;
        for (int x = x0; x < x1; x++) row[x] = color;
    }
}

static void draw_glyph(uint16_t *band, int band_w, int band_y0, int band_h,
                       int gx, int gy, unsigned char ch, int scale,
                       uint16_t color) {
    if (ch >= 128) ch = '?';
    const char *bits = font8x8_basic[ch];
    for (int row = 0; row < 8; row++) {
        unsigned char rb = (unsigned char)bits[row];
        for (int col = 0; col < 8; col++) {
            if (!(rb & (1u << col))) continue;        /* LSB = leftmost */
            int px = gx + col * scale;
            int py = gy + row * scale;
            htgl_fill_rect(band, band_w, band_y0, band_h,
                           px, py, scale, scale, color);
        }
    }
}

void htgl_draw_text(uint16_t *band, int band_w, int band_y0, int band_h,
                    int tx, int ty, const char *text, int len,
                    int scale, uint16_t color) {
    int advance = 8 * scale;
    for (int i = 0; i < len; i++) {
        draw_glyph(band, band_w, band_y0, band_h,
                   tx + i * advance, ty, (unsigned char)text[i], scale, color);
    }
}
